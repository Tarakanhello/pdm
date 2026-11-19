#include "onnx_stamp_recognizer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/core/utils/logger.hpp>
#include <opencv2/imgproc.hpp>
#include <clipper.hpp>

#include "image.h"

#include <QDebug>
#include <QString>


namespace pdm::infrastructure
{

std::string OnnxStampRecognizer::normalizeExecutionSuffix(std::string_view rawSuffix)
{
    if (rawSuffix.size() == 1)
    {
        const char upper{ static_cast<char>(std::toupper(static_cast<unsigned char>(rawSuffix[0]))) };

        // В литере исполнения латинская 'B' соответствует кириллической 'Б', а 'G' или 'R' — 'Г'
        if (upper == 'B')
        {
            return "Б";
        }
        if (upper == 'G' || upper == 'R' || upper == '?')
        {
            return "Г";
        }

        // Остальные латинские двойники (А, С, Е, Н, К, М, О, Р, Т, Х) берём из единого справочника
        const std::string_view key{ &upper, 1 };
        for (const auto& [lat, cyr] : kNameLatinToCyrillic)
        {
            if (lat == key && lat != "W")
            {
                return std::string(cyr);
            }
        }

        return "";
    }

    // Если это одиночная кириллическая буква в UTF-8 (2 байта) — приводим к заглавному регистру (например, 'г' -> 'Г')
    if (rawSuffix.size() == kUtf8CyrillicCharBytes && (rawSuffix[0] == '\xD0' || rawSuffix[0] == '\xD1'))
    {
        return QString::fromUtf8(rawSuffix.data(), static_cast<qsizetype>(rawSuffix.size()))
        .toUpper()
            .toStdString();
    }

    return "";
}




std::vector<cv::Point2f> OnnxStampRecognizer::unclipBox(const std::vector<cv::Point2f>& box, float unclipRatio)
{
     // 1. Вычисляем площадь и периметр сжатого контура DBNet (защищаемся от деления на ноль)
    double area{ std::abs(cv::contourArea(box)) };
    double length{ cv::arcLength(box, true) };
    if(length <= 1e-5)
    {
        return box;
    }

    // 2. Рассчитываем величину равномерного отступа наружу в пикселях по формуле из статьи DBNet
    double distance{ area * unclipRatio / length };

    // 3. Переводим вершины в целочисленный формат библиотеки ClipperLib
    ClipperLib::ClipperOffset offset{};
    ClipperLib::Path path{};
    for(const auto& pt : box)
    {
        path << ClipperLib::IntPoint(ClipperLib::cInt(pt.x),
                                     ClipperLib::cInt(pt.y));
    }

    // 4. Выполняем эквидистантное расширение замкнутого полигона наружу на distance пикселей
    offset.AddPath(path, ClipperLib::jtRound, ClipperLib::etClosedPolygon);

    ClipperLib::Paths solution{};
    offset.Execute(solution, distance);

    if(solution.empty())
    {
        return box;
    }

    // 5. Конвертируем расширенный полигон обратно в координаты OpenCV
    std::vector<cv::Point2f> result{};
    result.reserve(solution.front().size());
    for(const auto& pt : solution.front())
    {
        result.emplace_back(float(pt.X), float(pt.Y));
    }

    return result;
}




std::vector<cv::Point2f> OnnxStampRecognizer::orderPointsClockwise(std::vector<cv::Point2f> pts)
{
    if(pts.size() != kMinContourPoints)
    {
        return pts;
    }

    // 1. Сортируем все 4 вершины слева направо, чтобы отделить левую пару углов от правой
    std::sort(pts.begin(), pts.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.x < b.x;
    });

    std::vector<cv::Point2f> left = { pts.at(0), pts.at(1) };
    std::vector<cv::Point2f> right = { pts.at(2), pts.at(3) };

    // 2. В каждой паре сортируем точки сверху вниз (в OpenCV ось Y направлена вниз)
    std::sort(left.begin(), left.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.y < b.y;
    });
    std::sort(right.begin(), right.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.y < b.y;
    });

    // 3. Возвращаем вершины по часовой стрелке: top-left, top-right, bottom-right, bottom-left
    return { left.at(0), right.at(0), right.at(1), left.at(1) };
}




cv::Mat OnnxStampRecognizer::getRotateCropImage(const cv::Mat& src, const std::vector<cv::Point2f>& box)
{
    // На чертежах текст в штампе горизонтален, поэтому используем быстрый прямой прямоугольник
    // вместо перспективного поворота (warpPerspective), чтобы не размывать мелкие шрифты
    cv::Rect rect{ cv::boundingRect(box) };

    // Добавляем асимметричные отступы (35 px слева, 16 px справа) для захвата первых букв ("Б", "О", "Щ")
    // и ограничиваем координаты размерами исходной матрицы во избежание выхода за пределы памяти
    int x1{ std::max(0, rect.x - kCropPadLeftPx) };
    int y1{ std::max(0, rect.y - kCropPadTopPx) };
    int x2{ std::min(src.cols, rect.x + rect.width + kCropPadRightPx) };
    int y2{ std::min(src.rows, rect.y + rect.height + kCropPadBottomPx) };

    if(x2 <= x1 || y2 <= y1)
    {
        return {};
    }

    // Вызываем .clone(), чтобы скопировать фрагмент в непрерывный буфер памяти
    // (оператор () в OpenCV возвращает только ссылку на исходную матрицу src)
    return src(cv::Rect(x1, y1, x2 - x1, y2 - y1)).clone();
}




std::optional<OnnxStampRecognizer::TextBox> OnnxStampRecognizer::
    contourToTextBox(const std::vector<cv::Point>& contour,
                     const cv::Mat& probMap,
                     float scaleX, float scaleY,
                     float boxScoreThresh, float unclipRatio)
{
    if(contour.size() < kMinContourPoints)
    {
        return std::nullopt;
    }

    // 1. Отсеиваем слишком мелкие артефакты (точки, обрывки линий рамки)
    const cv::RotatedRect rect{ cv::minAreaRect(contour) };
    if(std::min(rect.size.width, rect.size.height) < kMinRawBoxSizePx)
    {
        return std::nullopt;
    }

    // 2. Вычисляем среднюю уверенность нейросети строго внутри пикселей текущего контура
    const cv::Rect bounding{ cv::boundingRect(contour) & cv::Rect(0, 0, probMap.cols, probMap.rows) };
    if(bounding.width <= 0 || bounding.height <= 0)
    {
        return std::nullopt;
    }

    cv::Mat mask{ cv::Mat::zeros(bounding.size(), CV_8UC1) };
    std::vector<cv::Point> shiftedContour{};
    shiftedContour.reserve(contour.size());
    for(const auto& pt : contour)
    {
        shiftedContour.emplace_back(pt.x - bounding.x, pt.y - bounding.y);
    }
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{ shiftedContour }, cv::Scalar(1));

    const float meanScore{ static_cast<float>(*(cv::mean(probMap(bounding), mask).val)) };
    if(meanScore < boxScoreThresh)
    {
        return std::nullopt;
    }

    // 3. Компенсируем сжатие DBNet: расширяем контур наружу через библиотеку Clipper
    std::array<cv::Point2f, kMinContourPoints> vertices{};
    rect.points(vertices.data());
    const std::vector<cv::Point2f> boxPts(vertices.begin(), vertices.end());

    const auto unclipped{ unclipBox(boxPts, unclipRatio) };
    if(unclipped.size() < kMinContourPoints)
    {
        return std::nullopt;
    }

    const cv::RotatedRect unclippedRect{ cv::minAreaRect(unclipped) };
    if(std::min(unclippedRect.size.width, unclippedRect.size.height) < kMinUnclippedBoxSizePx)
    {
        return std::nullopt;
    }

    // 4. Масштабируем координаты из сетки 1024x1024 обратно в пиксели исходного штампа
    unclippedRect.points(vertices.data());
    std::vector<cv::Point2f> finalBox{};
    finalBox.reserve(kMinContourPoints);
    for(const auto& v : vertices)
    {
        const float x{ std::clamp(v.x * scaleX, 0.0f, static_cast<float>(probMap.cols) * scaleX - 1.0f) };
        const float y{ std::clamp(v.y * scaleY, 0.0f, static_cast<float>(probMap.rows) * scaleY - 1.0f) };
        finalBox.emplace_back(x, y);
    }

    return TextBox{ orderPointsClockwise(finalBox), meanScore, "" };
}




std::vector<OnnxStampRecognizer::TextBox> OnnxStampRecognizer::findTextBoxes(const cv::Mat& probMap, float scaleX, float scaleY,
                                   float boxThresh, float boxScoreThresh,
                                   float unclipRatio)
{
    // 1. Бинаризуем карту вероятностей: пиксели выше boxThresh (0.3) становятся белыми (текст)
    cv::Mat bitMap{};
    cv::threshold(probMap, bitMap, boxThresh, 255.0, cv::THRESH_BINARY);
    bitMap.convertTo(bitMap, CV_8UC1);

    // 2. Находим замкнутые контуры текстовых пятен на бинарной маске
    std::vector<std::vector<cv::Point>> contours{};
    cv::findContours(bitMap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    // 3. Преобразуем каждый валидный контур в текстовый блок
    std::vector<TextBox> boxes{};
    for(const auto& contour : contours)
    {
        if(auto box{ contourToTextBox(contour, probMap, scaleX, scaleY, boxScoreThresh, unclipRatio) })
        {
            boxes.push_back(std::move(*box));
        }
    }

    // 4. Сортируем боксы в порядке чтения: сверху вниз, а на одной строке — слева направо
    std::sort(boxes.begin(), boxes.end(), [](const TextBox& a, const TextBox& b) {
        if(std::abs(a.box.front().y - b.box.front().y) > kSameLineThresholdPx)
        {
            return a.box.front().y < b.box.front().y;
        }
        return a.box.front().x < b.box.front().x;
    });

    return boxes;
}




void OnnxStampRecognizer::replaceAll(std::string& s, std::string_view from, std::string_view to)
{
    if(from.empty())
    {
        return;
    }
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
}




void OnnxStampRecognizer::trimInPlace(std::string& s, std::string_view chars)
{
    const size_t first{ s.find_first_not_of(chars) };
    if (first == std::string::npos)
    {
        s.clear();
        return;
    }

    const size_t last{ s.find_last_not_of(chars) };
    s.erase(last + 1);
    s.erase(0, first);
}




std::string OnnxStampRecognizer::cleanRussianName(std::string s)
{
    for (const auto& [lat, cyr] : kNameLatinToCyrillic)
    {
        replaceAll(s, lat, cyr);
    }

    for (const auto& [typo, fixed] : kNameWordCorrections)
    {
        replaceAll(s, typo, fixed);
    }

    trimInPlace(s, ".,- ");

    return s;
}




std::string OnnxStampRecognizer::normalizeSuffixLetter(char ch)
{
    const char upper{ static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) };

    // В литере исполнения децимального номера латинская 'B' соответствует кириллической 'Б' (в отличие от наименований)
    if (upper == 'B')
    {
        return "Б";
    }

    // Остальные визуальные двойники (А, С, Е, Н, К, М, О, Р, Т, Х) берём из единой таблицы замен
    const std::string_view key{ &upper, 1 };
    for (const auto& [lat, cyr] : kNameLatinToCyrillic)
    {
        if (lat == key && lat != "W")
        {
            return std::string(cyr);
        }
    }

    return "";
}




std::string OnnxStampRecognizer::fixDecimalDigitArtifacts(const std::string& rawText)
{
    static const std::regex kDotSpacesRegex         { R"(\s*\.\s*)" };
    static const std::regex kColonSeparatorRegex    { R"((\d)\s*[:,;]\s*(\d))" };
    static const std::regex kDotColonArtifactRegex  { R"(\.:|:\.)" };

    // 1. Нормализуем разделители групп (убираем пробелы у точек, меняем ':' и ';' между цифрами на '.')
    std::string s{ std::regex_replace(rawText, kDotSpacesRegex, ".") };
    s = std::regex_replace(s, kColonSeparatorRegex, "$1.$2");
    s = std::regex_replace(s, kDotColonArtifactRegex, ".");

    if (std::count(s.begin(), s.end(), '.') < kMinDecimalDots)
    {
        return s;
    }

    // 2. Находим границу 5-й группы (3 цифры после 4-й точки), чтобы не исказить букву исполнения и код документа
    size_t dots{ 0 };
    size_t afterLastGroup{ 0 };
    for (size_t i{ 0 }; i < s.size(); ++i)
    {
        if (s[i] == '.')
        {
            ++dots;
        }
        if (dots == kLastDecimalDotIndex)
        {
            size_t digitsCount{ 0 };
            size_t j{ i + 1 };
            while (j < s.size() && digitsCount < kLastGroupDigitsCount)
            {
                if (s[j] != ' ')
                {
                    ++digitsCount;
                }
                ++j;
            }
            afterLastGroup = j;
            break;
        }
    }

    // 3. Заменяем буквенные двойники цифр строго в пределах цифровой части номера
    const size_t limit{ (afterLastGroup > 0) ? std::min(afterLastGroup, s.size()) : s.size() };
    for (size_t i{ 0 }; i < limit; ++i)
    {
        char& ch{ s[i] };
        if (ch == ':' || ch == 'I' || ch == 'l' || ch == 'T' || ch == '!')
        {
            ch = '1';
        }
        else if (ch == 'O' || ch == 'o' || ch == 'U' || ch == 'D' || ch == 'Q' || ch == 'C' || ch == 'c')
        {
            ch = '0';
        }
        else if (ch == '$' || ch == 'S')
        {
            ch = '5';
        }
        else if (ch == 'B')
        {
            ch = '8';
        }
    }

    return s;
}




pdm::shared::DocumentTypeCode OnnxStampRecognizer::extractTrailingDocCode(std::string& tail)
{
    trimInPlace(tail, " .,");

    for (const auto& [codeStr, codeEnum] : kTrailingDocCodes)
    {
        if (tail.size() >= codeStr.size() &&
            tail.compare(tail.size() - codeStr.size(), codeStr.size(), codeStr) == 0)
        {
            tail.erase(tail.size() - codeStr.size());
            while (!tail.empty() && tail.back() == ' ')
            {
                tail.pop_back();
            }
            return codeEnum;
        }
    }

    return pdm::shared::DocumentTypeCode::Unknown;
}




OnnxStampRecognizer::ParsedNumberInfo OnnxStampRecognizer::parseDecimalAndCode(const std::string& rawText)
{
    static const std::regex kBaseNumRegex{ R"((\d{3}\s*\.\s*\d{2}\s*\.\s*\d{2}\s*\.\s*\d{1,2}\s*\.\s*\d{3}))" };
    static const std::regex kCompactNumRegex{ R"(([\d\.\s]{12,18}))" };
    static const std::regex kWhitespaceRegex{ R"(\s+)" };

    ParsedNumberInfo info{};
    const std::string cleaned{ fixDecimalDigitArtifacts(rawText) };
    std::smatch match{};
    std::string tail{};

    if (std::regex_search(cleaned, match, kBaseNumRegex))
    {
        // 1а. Стандартный формат с разделительными точками
        info.number = std::regex_replace(match.str(1), kWhitespaceRegex, "");
        tail = cleaned.substr(static_cast<size_t>(match.position() + match.length()));
    }
    else if (std::regex_search(cleaned, match, kCompactNumRegex))
    {
        // 1б. Резервный разбор, если OCR проглотил точки между группами (например, "519.111100.001A")
        std::string digitsOnly{};
        for (char c : match.str(1))
        {
            if (std::isdigit(static_cast<unsigned char>(c)))
            {
                digitsOnly += c;
            }
        }

        if (digitsOnly.size() != kStandardDecimalDigits)
        {
            return info;
        }

        info.number = digitsOnly.substr(0, 3) + "." +
                      digitsOnly.substr(3, 2) + "." +
                      digitsOnly.substr(5, 2) + "." +
                      digitsOnly.substr(7, 2) + "." +
                      digitsOnly.substr(9, 3);
        tail = cleaned.substr(static_cast<size_t>(match.position() + match.length()));
    }
    else
    {
        return info;
    }

    // 2. Извлекаем код документа (СБ, ТУ) из конца хвоста
    info.code = extractTrailingDocCode(tail);
    trimInPlace(tail, " ");

    // 3. Нормализуем литеру исполнения (любую кириллическую букву или латинский двойник)
    const std::string suffix{ normalizeExecutionSuffix(tail) };
    if (!suffix.empty())
    {
        info.number += suffix;
    }

    return info;
}




cv::Mat OnnxStampRecognizer::prepareRecBlob(const cv::Mat& crop)
{
    // 1. Вычисляем ширину с сохранением пропорций текста (высота всегда фиксирована — 48 px)
    const float aspectRatio{ static_cast<float>(crop.cols) / static_cast<float>(crop.rows) };
    const int scaledWidth{ static_cast<int>(std::round(aspectRatio * kRecInputHeight)) };
    const int targetW{ std::clamp(scaledWidth, kRecMinWidth, kRecMaxWidth) };

    // 2. Масштабируем строку бикубической интерполяцией для сохранения чёткости мелких цифр и букв
    cv::Mat resized{};
    cv::resize(crop, resized, cv::Size(targetW, kRecInputHeight), 0, 0, cv::INTER_CUBIC);

    // 3. Дополняем кадр справа до 640 px нейтральным серым цветом (128).
    // После нормализации (128/255 - 0.5)/0.5 он даёт ~0.0, чтобы модель не галлюцинировала шум на пустом месте
    cv::Mat padded{ kRecInputHeight, kRecMaxWidth, CV_8UC3,
                   cv::Scalar(kRecPadGrayValue, kRecPadGrayValue, kRecPadGrayValue) };
    resized.copyTo(padded(cv::Rect(0, 0, targetW, kRecInputHeight)));

    // 4. Приводим пиксели из [0..255] к диапазону [-1.0..1.0] по контракту PP-OCRv5
    cv::Mat recInput{};
    padded.convertTo(recInput, CV_32FC3, kPixelScale);
    cv::subtract(recInput, kRecNormMean, recInput);
    cv::divide(recInput, kRecNormStd, recInput);

    // 5. Упаковываем в 4D-тензор NCHW (1x3x48x640); swapRB = false, так как модель обучена на BGR
    return cv::dnn::blobFromImage(recInput, 1.0, cv::Size(), cv::Scalar(), false, false);
}




std::string OnnxStampRecognizer::decodeCtcOutput(const cv::Mat& recOut, const std::vector<std::string>& keys)
{
    if(recOut.empty() || recOut.dims < kRecOutputMinDims)
    {
        return "";
    }

    // Размерности выхода: [0] = Batch (1), [1] = длина последовательности шагов (SeqLen), [2] = размер словаря (NumClasses)
    const int seqLen{ recOut.size[1] };
    const int numClasses{ recOut.size[2] };
    const float* data{ reinterpret_cast<const float*>(recOut.data) };

    std::string lineText{};
    int prevIdx{ kCtcBlankIndex };

    // Жадное CTC-декодирование (Greedy Search):
    // на каждом временном шаге берём символ с максимальной вероятностью, отбрасываем blank-токены (индекс 0)
    // и схлопываем подряд идущие дубликаты одного и того же символа (например, "С-С-#-Б" -> "СБ")
    for(int t{ 0 }; t < seqLen; ++t)
    {
        const float* stepScores{ data + static_cast<size_t>(t) * numClasses };
        const int maxIdx{ static_cast<int>(std::max_element(stepScores, stepScores + numClasses) - stepScores) };

        if(maxIdx > kCtcBlankIndex && maxIdx != prevIdx && maxIdx < static_cast<int>(keys.size()))
        {
            lineText += keys.at(static_cast<size_t>(maxIdx));
        }
        prevIdx = maxIdx;
    }

    return lineText;
}




std::string OnnxStampRecognizer::recognizeCrop(cv::dnn::Net& recNet, const cv::Mat& crop, const std::vector<std::string>& keys)
{
    if(crop.empty() || crop.rows <= 0 || crop.cols <= 0)
    {
        return "";
    }

    // Подготавливаем тензор, запускаем инференс распознавателя и декодируем CTC-последовательность в текст
    const cv::Mat recBlob{ prepareRecBlob(crop) };
    recNet.setInput(recBlob);
    const cv::Mat recOut{ recNet.forward() };

    return decodeCtcOutput(recOut, keys);
}




int OnnxStampRecognizer::parseSheetNumber(const std::string& raw, bool isForm2aCell)
{
    if(raw.find('A') != std::string::npos || raw.find("А") != std::string::npos ||
        raw.find("340") != std::string::npos || raw.find("310") != std::string::npos)
    {
        return 0;
    }

    std::string s{ raw };
    trimInPlace(s, "Il|/ ]");

    // Извлекаем цифры номера листа и восстанавливаем типовые визуальные двойники OCR
    std::string digits;
    for(char c : s)
    {
        if(std::isdigit(static_cast<unsigned char>(c)))
        {
            digits += c;
        }
        else if(c == 'O' || c == 'o' || c == 'D')
        {
            // Овальные контуры латинских букв часто возникают вместо нуля в двузначных номерах (например, 10, 20)
            digits += '0';
        }
        else if(c == 'B')
        {
            // Заглавная латинская 'B' (или кириллическая 'В') — характерный двойник цифры '8' при низком контрасте скана
            digits += '8';
        }
    }

    if(isForm2aCell && digits.size() == 2 && digits.front() == '1' && digits[1] >= '2' && digits[1] <= '9')
    {
        digits.erase(digits.begin());
    }
    else if(isForm2aCell && digits.size() == 2 && digits.back() == '1' && digits[0] >= '2' && digits[0] <= '9')
    {
        digits.pop_back();
    }

    if(digits.empty())
    {
        return 0;
    }

    try
    {
        return std::stoi(digits);
    }
    catch (...)
    {
        return 0;
    }
}





bool OnnxStampRecognizer::isStopWord(const std::string& s)
{
    return s.find("Копиров") != std::string::npos ||
           s.find("Копироб") != std::string::npos ||
           s.find("Конивоб") != std::string::npos ||
           s.find("Формат") != std::string::npos  ||
           s.find("Фопмот") != std::string::npos  ||
           s.find("Сборочн") != std::string::npos ||
           s.find("Соорочн") != std::string::npos ||
           s.find("чертеж") != std::string::npos  ||
           s.find("ГОСТ") != std::string::npos    ||
           s.find("Масса") != std::string::npos   ||
           s.find("Масштаб") != std::string::npos ||
           s.find("Дата") != std::string::npos    ||
           s.find("Цата") != std::string::npos    ||
           s.find("loma") != std::string::npos    ||
           s.find("lomo") != std::string::npos    ||
           s.find("Дoma") != std::string::npos;
}




void OnnxStampRecognizer::detectDocumentCode(const std::string& text, pdm::shared::DocumentTypeCode& code)
{
    if(code != pdm::shared::DocumentTypeCode::Unknown)
    {
        return;
    }

    // 1. Проверяем короткие коды документов из единого справочника (исключая чисто цифровой артефакт "05",
    //    который валиден только в конце децимального номера, но опасен в тексте наименования)
    for(const auto& [pattern, docCode] : kTrailingDocCodes)
    {
        if(pattern != "05" && text.find(pattern) != std::string::npos)
        {
            code = docCode;
            return;
        }
    }

    // 2. Проверяем полнотекстовые маркеры в графе наименования
    for(const auto& [keyword, docCode] : kNameDocCodeKeywords)
    {
        if(text.find(keyword) != std::string::npos)
        {
            code = docCode;
            return;
        }
    }
}




OnnxStampRecognizer::OnnxStampRecognizer(const std::filesystem::path& modelsDir)
{
    // Отключаем аппаратные SIMD-оптимизации OpenCV (AVX2/SSE4).
    // В связке Windows + MinGW 64-bit векторные инструкции внутри cv::dnn могут
    // вызывать Segmentation Fault из-за несовпадения выравнивания стека (Stack Alignment).
    cv::setUseOptimized(false);

    // Подавляем информационные сообщения OpenCV (INFO/WARNING), оставляя только критические ошибки
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);

    try
    {
        qDebug().noquote() << "[OpenCV-DNN] Initializing models from:"
                           << QString::fromStdString(modelsDir.string());

        // Загружаем только детектор (DBNet) и распознаватель (PP-OCRv5).
        // Классификатор угла (cls.onnx) не используется, так как сканы чертежей имеют фиксированную ориентацию
        m_detNet = cv::dnn::readNetFromONNX((modelsDir / "det.onnx").string());
        m_recNet = cv::dnn::readNetFromONNX((modelsDir / "rec.onnx").string());

        std::ifstream dictFile(modelsDir / "cyrillic_dict.txt");
        if(!dictFile.is_open())
        {
            std::cerr << "[OpenCV-DNN] ERROR: Cannot open cyrillic_dict.txt!\n" << std::flush;
            return;
        }

        // Формируем словарь символов для CTC-декодирования строго по контракту PaddleOCR:
        // - индекс 0 зарезервирован под служебный blank-токен ("#");
        // - далее идут 517 символов из файла словаря;
        // - последний индекс (518) — символ пробела (" "), который отсутствует в файле как отдельная строка.
        m_keys.clear();
        m_keys.push_back("#");
        std::string line;
        while (std::getline(dictFile, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            {
                line.pop_back();
            }
            if(!line.empty())
            {
                m_keys.push_back(line);
            }
        }
        m_keys.push_back(" ");

        qDebug() << "[OpenCV-DNN] Loaded dictionary keys:" << m_keys.size();
        m_initialized = (!m_detNet.empty() && !m_recNet.empty());

        if(m_initialized)
        {
            qDebug().noquote() << "[OpenCV-DNN] ONNX models loaded successfully!\n";
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[OpenCV-DNN] Init exception: " << e.what() << "\n" << std::flush;
        m_initialized = false;
    }
}




cv::Mat OnnxStampRecognizer::prepareStampImage(const std::filesystem::path& imagePath)
{
    // 1. Загрузка скана и вырезка области основной надписи
    Image img{ imagePath };
    int cropX{ std::max(0, img.width() - kOffsetX - kCropW) };
    int cropY{ std::max(0, img.height() - kOffsetY - kCropH) };
    Image stamp{ img.getPartOfImage(cropX, cropY, kCropW, kCropH) };

    const int width{ stamp.width()  };
    const int height{ stamp.height() };
    const int channels{ stamp.channels() };
    const unsigned char* rawData{ stamp.get() };

    if(!rawData || width <= 0 || height <= 0 || (channels != 1 && channels != 3 && channels != 4))
    {
        return {};
    }

    // 2. Перевод буфера stb_image в одноканальный серый формат OpenCV
    // Оборачиваем сырой буфер stb_image без выделения памяти и конвертируем в BGR штатным cv::cvtColor
    const int srcType{ (channels == 1) ? CV_8UC1 : (channels == 4 ? CV_8UC4 : CV_8UC3) };
    const int toGrayCode{ (channels == 1) ? -1
                                          : (channels == 4 ? cv::COLOR_RGBA2GRAY
                                                           : cv::COLOR_RGB2GRAY) };

    const cv::Mat srcMat{ height, width, srcType, const_cast<unsigned char*>(rawData) };
    cv::Mat gray{};
    if(toGrayCode == -1)
    {
        gray = srcMat;
    }
    else
    {
        cv::cvtColor(srcMat, gray, toGrayCode);
    }

    // 3. Локальное выравнивание контраста (CLAHE) и возврат в 3-канальный BGR для ONNX-моделей
    cv::Mat enhanced{};
    cv::Ptr<cv::CLAHE> clahe{ cv::createCLAHE(kClaheClipLimit, cv::Size(kClaheGridSize, kClaheGridSize)) };
    clahe->apply(gray, enhanced);

    cv::Mat bgrMat{};
    cv::cvtColor(enhanced, bgrMat, cv::COLOR_GRAY2BGR);

    return bgrMat;
}




std::vector<OnnxStampRecognizer::TextBox> OnnxStampRecognizer::detectTextBoxes(const cv::Mat& bgrMat)
{
    // 1. Приводим штамп к фиксированному размеру 1024x1024 и переводим пиксели из [0..255] в [0.0..1.0]
    cv::Mat detInput{};
    cv::resize(bgrMat, detInput, cv::Size(kDetInputWidth, kDetInputHeight));
    detInput.convertTo(detInput, CV_32FC3, kPixelScale);

    // 2. Применяем стандартизацию ImageNet: (pixel - mean) / std, как при обучении PP-OCRv4
    cv::subtract(detInput, kImageNetMean, detInput);
    cv::divide(detInput, kImageNetStd, detInput);

    // 3. Преобразуем матрицу из формата HWC в 4D-тензор NCHW (1x3x1024x1024) и меняем BGR -> RGB (swapRB = true)
    cv::Mat detBlob{ cv::dnn::blobFromImage(detInput, 1.0, cv::Size(), cv::Scalar(), true, false) };
    m_detNet.setInput(detBlob);

    // 4. Выполняем прямой проход нейросети, получая карту вероятностей текста размером 1024x1024
    cv::Mat detOut{ m_detNet.forward() };
    if(detOut.empty() || detOut.total() < static_cast<size_t>(kDetInputHeight) * kDetInputWidth)
    {
        std::cerr << "[OpenCV-DNN] ERROR: Invalid detOut shape!\n" << std::flush;
        return {};
    }

    // 5. Оборачиваем выходной буфер в 2D-матрицу без копирования памяти и извлекаем контуры текстовых строк
    const cv::Mat probMap{ kDetInputHeight, kDetInputWidth, CV_32FC1, detOut.data };
    const float scaleX{ static_cast<float>(bgrMat.cols) / static_cast<float>(kDetInputWidth) };
    const float scaleY{ static_cast<float>(bgrMat.rows) / static_cast<float>(kDetInputHeight) };

    return findTextBoxes(probMap, scaleX, scaleY);
}




void OnnxStampRecognizer::recognizeTextBoxes(const cv::Mat& bgrMat, std::vector<TextBox>& boxes)
{
    qDebug().noquote() << "[OpenCV-DNN] DETECTED BLOCKS (x, y):\n";

    // Проходим по всем найденным рамкам: вырезаем каждую строку и прогоняем через модель распознавания
    for(auto& tb : boxes)
    {
        const cv::Mat crop{ getRotateCropImage(bgrMat, tb.box) };
        tb.text = recognizeCrop(m_recNet, crop, m_keys);

        // Логируем координаты (x, y) и распознанный текст для отладки пространственной привязки граф
        if(!tb.text.empty())
        {
            qDebug().noquote() << QString("  (%1, %2): %3").arg(static_cast<int>(tb.box.front().x))
                                                           .arg(static_cast<int>(tb.box.front().y))
                                                           .arg(QString::fromStdString(tb.text));
        }
    }

    qDebug().noquote() << "-------------------\n";
}




std::optional<cv::Point2f> OnnxStampRecognizer::
    locateDecimalNumber(const std::vector<TextBox>& boxes,
                        shared::Document& doc)
{
    cv::Point2f numPos{ 0.0f, kUnsetCoord };

    // Проходим по всем текстовым блокам и ищем децимальный номер.
    // Если в кадре два номера (верхний — случайный захват,
    // нижний — основная надпись), оставляем самый нижний (с наибольшей координатой Y).
    for(const auto& tb : boxes)
    {
        if(tb.text.empty())
        {
            continue;
        }

        const auto parsedNum{ parseDecimalAndCode(tb.text) };
        if(!parsedNum.number.empty())
        {
            const cv::Point2f& topLeft{ tb.box.front() };
            if(topLeft.y > numPos.y)
            {
                doc.number = parsedNum.number;
                doc.code = parsedNum.code;
                numPos = topLeft;
            }
        }
    }

    if(doc.number.empty())
    {
        qWarning().noquote() << "OnnxStampRecognizer::locateDecimalNumber: Decimal number not found.";
        return std::nullopt;
    }

    return numPos;
}




void OnnxStampRecognizer::extractProductName(const std::vector<TextBox>& boxes,
                                             const cv::Point2f& numPos,
                                             shared::Document& doc)
{
    // На узком штампе последующих листов (Форма 2а, высотой 15 мм) номер прижат к нижнему краю кропа
    // (numPos.y > 330 px), а графа наименования по ГОСТ 2.104 отсутствует
    if(numPos.y > kMaxForm1NumberY)
    {
        return;
    }

    // 1. Собираем все подходящие строки из графы наименования строго под децимальным номером
    std::vector<std::pair<float, std::string>> nameParts{};
    for(const auto& tb : boxes)
    {
        const float bx{ tb.box.front().x };
        const float by{ tb.box.front().y };
        const std::string& line{ tb.text };

        if(line.size() < kMinRawLineLength)
        {
            continue;
        }
        if(bx < numPos.x - kNameSearchLeftMarginPx || bx > numPos.x + kNameSearchRightMarginPx)
        {
            continue;
        }
        if(by <= numPos.y + kNameSearchTopOffsetPx || by > numPos.y + kNameSearchBottomOffsetPx)
        {
            continue;
        }

        // Очищаем строку от латинских двойников ("Wum" -> "Щит") и попутно проверяем код документа ("Сборочный чертеж")
        std::string cleanedPart{ cleanRussianName(line) };
        detectDocumentCode(cleanedPart, doc.code);

        // Проверяем наличие кириллических символов в UTF-8 (старшие байты 0xD0 или 0xD1) и фильтруем стоп-слова
        const bool hasCyrillic{ cleanedPart.find('\xD0') != std::string::npos ||
                                cleanedPart.find('\xD1') != std::string::npos };

        if(hasCyrillic && cleanedPart.size() >= kMinCleanedNameBytes && !isStopWord(cleanedPart))
        {
            nameParts.emplace_back(by, std::move(cleanedPart));
        }
    }

    // 2. Упорядочиваем строки названия сверху вниз по координате Y и объединяем через пробел
    std::sort(nameParts.begin(), nameParts.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    for(size_t i{ 0 }; i < nameParts.size(); ++i)
    {
        if(i > 0)
        {
            doc.name += " ";
        }
        doc.name += nameParts[i].second;
    }
}




bool OnnxStampRecognizer::matchKeywordAndExtractTail(const std::string& text,
                                                     std::span<const std::string_view> keywords,
                                                     int& outTrailingNumber)
{
    for (const auto kw : keywords)
    {
        const size_t pos{ text.find(kw) };
        if (pos != std::string::npos)
        {
            const std::string tail{ text.substr(pos + kw.size()) };
            outTrailingNumber = parseSheetNumber(tail, false);
            return true;
        }
    }
    return false;
}




int OnnxStampRecognizer::recognizeRegionNumber(const cv::Mat& bgrMat,
                                               const cv::Rect& roi,
                                               int minSizePx,
                                               bool isForm2aCell)
{
    // Безопасно пересекаем запрошенный прямоугольник с границами изображения штампа
    const cv::Rect validRoi{ roi & cv::Rect(0, 0, bgrMat.cols, bgrMat.rows) };
    if (validRoi.width <= minSizePx || validRoi.height <= minSizePx)
    {
        return 0;
    }

    const std::string cellText{ recognizeCrop(m_recNet, bgrMat(validRoi), m_keys) };
    return parseSheetNumber(cellText, isForm2aCell);
}




void OnnxStampRecognizer::findSheetHeaders(const std::vector<TextBox>& boxes,
                                           const cv::Point2f& numPos,
                                           cv::Point2f& sheetHeaderPos,
                                           cv::Point2f& totalHeaderPos,
                                           bool& sheetFound,
                                           bool& totalFound,
                                           shared::Document& doc)
{
    for (const auto& tb : boxes)
    {
        const float bx{ tb.box.front().x };
        const float by{ tb.box.front().y };
        if (bx <= numPos.x + kSheetHeaderMinOffsetX || by < numPos.y - kSheetHeaderTopToleranceY)
        {
            continue;
        }

        int inlineValue{ 0 };
        if (matchKeywordAndExtractTail(tb.text, kTotalSheetsKeywords, inlineValue))
        {
            totalHeaderPos = { bx, by };
            if (inlineValue > 0 && inlineValue < kMaxReasonableTotalSheets)
            {
                doc.totalSheets = inlineValue;
                totalFound = true;
            }
        }
        else if (matchKeywordAndExtractTail(tb.text, kCurrentSheetKeywords, inlineValue))
        {
            sheetHeaderPos = { bx, by };
            if (!sheetFound && inlineValue > 0 && inlineValue < kMaxReasonableSheetNumber)
            {
                doc.sheet = inlineValue;
                sheetFound = true;
            }
        }
    }
}




std::optional<int> OnnxStampRecognizer::findValueNearHeader(const std::vector<TextBox>& boxes,
                                                            const cv::Point2f& headerPos,
                                                            float minRightOffsetX,
                                                            float maxRightOffsetX,
                                                            bool isForm2aCell,
                                                            int maxValidValue)
{
    if (headerPos.x <= 0.0f)
    {
        return std::nullopt;
    }

    for (const auto& tb : boxes)
    {
        const float bx{ tb.box.front().x };
        const float by{ tb.box.front().y };

        const bool isBelow{ std::abs(bx - headerPos.x) < kCellBelowMaxOffsetX &&
                           by > headerPos.y + kCellBelowMinOffsetY &&
                           by < headerPos.y + kCellBelowMaxOffsetY };

        const bool isRight{ std::abs(by - headerPos.y) < kCellRightMaxOffsetY &&
                           bx > headerPos.x + minRightOffsetX &&
                           bx < headerPos.x + maxRightOffsetX };

        if (isBelow || isRight)
        {
            const int val{ parseSheetNumber(tb.text, isForm2aCell) };
            if (val > 0 && val < maxValidValue)
            {
                return val;
            }
        }
    }

    return std::nullopt;
}




void OnnxStampRecognizer::findSheetValuesInBoxes(const std::vector<TextBox>& boxes,
                                                 const cv::Point2f& sheetHeaderPos,
                                                 const cv::Point2f& totalHeaderPos,
                                                 bool isForm2a,
                                                 bool& sheetFound,
                                                 bool& totalFound,
                                                 shared::Document& doc)
{
    if (!sheetFound)
    {
        if (const auto val{ findValueNearHeader(boxes, sheetHeaderPos,
                                               kSheetRightMinOffsetX, kSheetRightMaxOffsetX,
                                               isForm2a, kMaxReasonableSheetNumber) })
        {
            doc.sheet = *val;
            sheetFound = true;
        }
    }

    if (!totalFound)
    {
        if (const auto val{ findValueNearHeader(boxes, totalHeaderPos,
                                               kTotalRightMinOffsetX, kTotalRightMaxOffsetX,
                                               false, kMaxReasonableTotalSheets) })
        {
            doc.totalSheets = *val;
            totalFound = true;
        }
    }
}




void OnnxStampRecognizer::extractSheetNumbers(const cv::Mat& bgrMat,
                                              const std::vector<TextBox>& boxes,
                                              const cv::Point2f& numPos,
                                              bool isForm2a,
                                              shared::Document& doc)
{
    constexpr int kDefaultSheetNumber{ 1 };

    cv::Point2f sheetHeaderPos{ kUnsetCoord, kUnsetCoord };
    cv::Point2f totalHeaderPos{ kUnsetCoord, kUnsetCoord };

    // На Форме 1 (когда есть наименование, !isForm2a) текущий лист по ГОСТ 2.104 всегда равен 1
    bool sheetFound{ !isForm2a };
    bool totalFound{ false };

    // 1. Находим заголовки «Лист» и «Листов» и проверяем слитные с ними цифры
    findSheetHeaders(boxes, numPos, sheetHeaderPos, totalHeaderPos, sheetFound, totalFound, doc);

    // 2. Ищем значения в соседних распознанных боксах:
    //    - СНИЗУ от заголовка (Форма 2: спецификации, ведомости, текстовые документы)
    //    - СПРАВА от заголовка в той же строке (Форма 1: чертежи деталей и сборочные чертежи)
    findSheetValuesInBoxes(boxes, sheetHeaderPos, totalHeaderPos, isForm2a, sheetFound, totalFound, doc);

    // 3. Прямой дораспознаватель ячеек через m_recNet, если детектор DBNet пропустил одиночную цифру
    if (!sheetFound && sheetHeaderPos.x > 0.0f)
    {
        const cv::Rect rightRoi{ static_cast<int>(sheetHeaderPos.x) + kDirectSheetRightShiftX,
                                static_cast<int>(sheetHeaderPos.y) - kDirectRightTopPadY,
                                kDirectRightCropW, kDirectRightCropH };
        int val{ recognizeRegionNumber(bgrMat, rightRoi, kMinValidCropSizePx, isForm2a) };

        if (val <= 0 || val >= kMaxReasonableSheetNumber)
        {
            const cv::Rect belowRoi{ static_cast<int>(sheetHeaderPos.x) - kDirectBelowLeftPadX,
                                    static_cast<int>(sheetHeaderPos.y) + kDirectBelowShiftY,
                                    kDirectBelowCropW, kDirectBelowCropH };
            val = recognizeRegionNumber(bgrMat, belowRoi, kMinValidCropSizePx, isForm2a);
        }

        if (val > 0 && val < kMaxReasonableSheetNumber)
        {
            doc.sheet = val;
        }
    }

    // Для «Листов» проверяем только область справа от заголовка (снизу стоит название организации «ЗАО»)
    if (!totalFound && totalHeaderPos.x > 0.0f)
    {
        const cv::Rect rightRoi{ static_cast<int>(totalHeaderPos.x) + kDirectTotalRightShiftX,
                                static_cast<int>(totalHeaderPos.y) - kDirectRightTopPadY,
                                kDirectRightCropW, kDirectRightCropH };
        const int val{ recognizeRegionNumber(bgrMat, rightRoi, kMinValidCropSizePx, false) };
        if (val > 0 && val < kMaxReasonableTotalSheets)
        {
            doc.totalSheets = val;
        }
    }

    // 4. Запасной поиск в правом углу Формы 2а, если заголовок «Лист» не распознался
    if (isForm2a && sheetHeaderPos.x < 0.0f && doc.sheet == kDefaultSheetNumber)
    {
        const int cellX{ std::max(static_cast<int>(numPos.x + kForm2aCellMinFromNumX),
                                 bgrMat.cols - kForm2aRightEdgeMarginX) };
        const cv::Rect cornerRoi{ cellX, static_cast<int>(numPos.y) + kForm2aCellShiftY,
                                 kForm2aCellCropW, kForm2aCellCropH };
        const int val{ recognizeRegionNumber(bgrMat, cornerRoi, kMinForm2aCropSizePx, true) };
        if (val > kDefaultSheetNumber && val < kMaxReasonableSheetNumber)
        {
            doc.sheet = val;
        }
    }

    if (isForm2a && doc.sheet == kDefaultSheetNumber)
    {
        doc.sheet = kMinFallbackForm2aSheet;
    }
}




std::optional<shared::Document> OnnxStampRecognizer::recognize(const std::filesystem::path& imagePath)
{
    if(!m_initialized)
    {
        std::cerr << "[OpenCV-DNN] ERROR: Recognizer is not initialized.\n" << std::flush;
        return std::nullopt;
    }

    try
    {
        qDebug().noquote() << "[OpenCV-DNN] 1. Start recognizing:"
                           << QString::fromStdString(imagePath.string());

        // 1. Загружаем скан, вырезаем правый нижний угол (область штампа) и переводим в BGR для OpenCV
        cv::Mat bgrMat{ prepareStampImage(imagePath) };
        if(bgrMat.empty())
        {
            return std::nullopt;
        }

        // 2. Находим координаты всех текстовых блоков в области штампа
        auto boxes{ detectTextBoxes(bgrMat) };
        if(boxes.empty())
        {
            return std::nullopt;
        }

        // 3. Распознаем текстовые поля
        recognizeTextBoxes(bgrMat, boxes);

        // Основной документ
        shared::Document doc{};
        doc.sheet = 1;
        doc.totalSheets = 1;

        // 4.  Извлекаем децимальный номер, наименование и нумерацию листов по ГОСТ 2.104
        const auto numPos{ locateDecimalNumber(boxes, doc) };
        if(!numPos.has_value())
        {
            return std::nullopt;
        }

        // Собираем многострочное наименование (только если номер не у самого низа кропа, т.е. это не Форма 2а)
        extractProductName(boxes, *numPos, doc);
        const bool isForm2a{ doc.name.empty() };

        // Извлекаем номер страницы
        extractSheetNumbers(bgrMat, boxes, *numPos, isForm2a, doc);

        qDebug().noquote()
            << QString("[OpenCV-DNN] PARSED -> Number: [%1] | Name: [%2] | Sheet: %3/%4")
                   .arg(QString::fromStdString(doc.number))
                   .arg(QString::fromStdString(doc.name))
                   .arg(doc.sheet)
                   .arg(doc.totalSheets);

        if(doc.number.empty() || (doc.sheet == 1 && doc.name.empty()))
        {
            qWarning().noquote() << "OnnxStampRecognizer::recognize: Failed to parse text.";
            return std::nullopt;
        }

        return doc;
    }
    catch (const std::exception& e)
    {
        std::cerr << "OnnxStampRecognizer::recognize: Error: " << e.what() << "\n" << std::flush;
        return std::nullopt;
    }
}

} // namespace pdm::infrastructure
