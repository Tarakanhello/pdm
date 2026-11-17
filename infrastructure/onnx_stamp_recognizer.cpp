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

namespace
{

constexpr int kOffsetX{ 30 };
constexpr int kOffsetY{ 90 };
constexpr int kCropW{ 1050 };
constexpr int kCropH{ 580 };

struct TextBox
{
    std::vector<cv::Point2f> box;
    float score{ 0.0f };
    std::string text;
};

struct ParsedNumberInfo
{
    std::string number;
    pdm::shared::DocumentTypeCode code{ pdm::shared::DocumentTypeCode::Unknown };
};

std::vector<cv::Point2f> unclipBox(const std::vector<cv::Point2f>& box, float unclipRatio)
{
    double area = std::abs(cv::contourArea(box));
    double length = cv::arcLength(box, true);
    if (length <= 1e-5)
    {
        return box;
    }

    double distance = area * unclipRatio / length;

    ClipperLib::ClipperOffset offset;
    ClipperLib::Path path;
    for (const auto& pt : box)
    {
        path << ClipperLib::IntPoint(ClipperLib::cInt(pt.x), ClipperLib::cInt(pt.y));
    }
    offset.AddPath(path, ClipperLib::jtRound, ClipperLib::etClosedPolygon);

    ClipperLib::Paths solution;
    offset.Execute(solution, distance);
    if (solution.empty())
    {
        return box;
    }

    std::vector<cv::Point2f> result;
    result.reserve(solution.front().size());
    for (const auto& pt : solution.front())
    {
        result.emplace_back(float(pt.X), float(pt.Y));
    }
    return result;
}

std::vector<cv::Point2f> orderPointsClockwise(std::vector<cv::Point2f> pts)
{
    std::sort(pts.begin(), pts.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.x < b.x;
    });

    std::vector<cv::Point2f> left = { pts.at(0), pts.at(1) };
    std::vector<cv::Point2f> right = { pts.at(2), pts.at(3) };

    std::sort(left.begin(), left.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.y < b.y;
    });
    std::sort(right.begin(), right.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.y < b.y;
    });

    return { left.at(0), right.at(0), right.at(1), left.at(1) };
}

cv::Mat getRotateCropImage(const cv::Mat& src, const std::vector<cv::Point2f>& box)
{
    cv::Rect rect = cv::boundingRect(box);

    // Расширяем рамку на 35 пикселей влево и 16 вправо, чтобы не обрезалась первая буква ("Б", "О")
    int x1 = std::max(0, rect.x - 35);
    int y1 = std::max(0, rect.y - 4);
    int x2 = std::min(src.cols, rect.x + rect.width + 16);
    int y2 = std::min(src.rows, rect.y + rect.height + 4);

    if (x2 <= x1 || y2 <= y1)
    {
        return cv::Mat();
    }
    return src(cv::Rect(x1, y1, x2 - x1, y2 - y1)).clone();
}

std::vector<TextBox> findTextBoxes(const cv::Mat& probMap, float scaleX, float scaleY,
                                   float boxThresh = 0.3f, float boxScoreThresh = 0.5f,
                                   float unclipRatio = 2.1f)
{
    cv::Mat bitMap;
    cv::threshold(probMap, bitMap, boxThresh, 255.0, cv::THRESH_BINARY);
    bitMap.convertTo(bitMap, CV_8UC1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bitMap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    std::vector<TextBox> boxes;
    for (const auto& contour : contours)
    {
        if (contour.size() < 4)
        {
            continue;
        }

        cv::RotatedRect rect = cv::minAreaRect(contour);
        if (std::min(rect.size.width, rect.size.height) < 3.0f)
        {
            continue;
        }

        cv::Rect bounding = cv::boundingRect(contour) & cv::Rect(0, 0, probMap.cols, probMap.rows);
        if (bounding.width <= 0 || bounding.height <= 0)
        {
            continue;
        }

        cv::Mat mask = cv::Mat::zeros(bounding.size(), CV_8UC1);
        std::vector<cv::Point> shiftedContour;
        shiftedContour.reserve(contour.size());
        for (const auto& pt : contour)
        {
            shiftedContour.emplace_back(pt.x - bounding.x, pt.y - bounding.y);
        }
        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{ shiftedContour }, cv::Scalar(1));
        float meanScore = float(*(cv::mean(probMap(bounding), mask).val));
        if (meanScore < boxScoreThresh)
        {
            continue;
        }

        std::array<cv::Point2f, 4> vertices{};
        rect.points(vertices.data());
        std::vector<cv::Point2f> boxPts(vertices.begin(), vertices.end());

        auto unclipped = unclipBox(boxPts, unclipRatio);
        if (unclipped.size() < 4)
        {
            continue;
        }

        cv::RotatedRect unclippedRect = cv::minAreaRect(unclipped);
        if (std::min(unclippedRect.size.width, unclippedRect.size.height) < 5.0f)
        {
            continue;
        }

        unclippedRect.points(vertices.data());
        std::vector<cv::Point2f> finalBox;
        finalBox.reserve(4);
        for (const auto& v : vertices)
        {
            float x = std::clamp(v.x * scaleX, 0.0f, float(probMap.cols) * scaleX - 1.0f);
            float y = std::clamp(v.y * scaleY, 0.0f, float(probMap.rows) * scaleY - 1.0f);
            finalBox.emplace_back(x, y);
        }

        boxes.push_back({ orderPointsClockwise(finalBox), meanScore, "" });
    }

    std::sort(boxes.begin(), boxes.end(), [](const TextBox& a, const TextBox& b) {
        if (std::abs(a.box.front().y - b.box.front().y) > 12.0f)
        {
            return a.box.front().y < b.box.front().y;
        }
        return a.box.front().x < b.box.front().x;
    });

    return boxes;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to)
{
    if (from.empty())
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

std::string cleanRussianName(std::string s)
{
    const std::vector<std::pair<std::string, std::string>> replacements = {
       {"A", "А"}, {"B", "В"}, {"C", "С"}, {"E", "Е"}, {"H", "Н"},
       {"K", "К"}, {"M", "М"}, {"O", "О"}, {"P", "Р"}, {"T", "Т"}, {"X", "Х"}, {"W", "Щ"},
       {"a", "а"}, {"b", "в"}, {"c", "с"}, {"e", "е"}, {"k", "к"},
       {"m", "т"}, {"o", "о"}, {"p", "р"}, {"u", "и"}, {"x", "х"}, {"y", "у"}, {"w", "щ"}
    };

    for (const auto& [lat, cyr] : replacements)
    {
        replaceAll(s, lat, cyr);
    }

    replaceAll(s, "Коиплект", "Комплект");
    replaceAll(s, "Соорочной", "Сборочный");
    replaceAll(s, "Сбороиной", "Сборочный");
    replaceAll(s, "Сдорочный", "Сборочный");
    replaceAll(s, "Осв", "Ось");
    replaceAll(s, "Деталв", "Деталь");
    replaceAll(s, "берхн", "верхн");
    replaceAll(s, "Берхн", "Верхн");
    replaceAll(s, "8zе3d", "Въезд");
    replaceAll(s, "8ze3d", "Въезд");
    replaceAll(s, "инбентарный", "инвентарный");
    replaceAll(s, "иньснтарный", "инвентарный");
    replaceAll(s, "РРебро", "Ребро");
    replaceAll(s, "Опорабереговая", "Опора береговая");
    replaceAll(s, "дереговая", "береговая");

    while (!s.empty() && (s.back() == '.' || s.back() == '-' || s.back() == ' ' || s.back() == ','))
    {
        s.pop_back();
    }
    while (!s.empty() && (s.front() == '.' || s.front() == '-' || s.front() == ' '))
    {
        s.erase(s.begin());
    }

    return s;
}

std::string normalizeSuffixLetter(char ch)
{
    switch (ch)
    {
    case 'A': case 'a': return "А";
    case 'B': case 'b': return "Б";
    case 'C': case 'c': return "С";
    case 'E': case 'e': return "Е";
    case 'H': case 'h': return "Н";
    case 'K': case 'k': return "К";
    case 'M': case 'm': return "М";
    case 'O': case 'o': return "О";
    case 'P': case 'p': return "Р";
    case 'T': case 't': return "Т";
    case 'X': case 'x': return "Х";
    default: return "";
    }
}

ParsedNumberInfo parseDecimalAndCode(const std::string& rawText)
{
    ParsedNumberInfo info;

    std::string s = std::regex_replace(rawText, std::regex(R"(\s*\.\s*)"), ".");
    s = std::regex_replace(s, std::regex(R"((\d)\s*[:,;]\s*(\d))"), "$1.$2");
    s = std::regex_replace(s, std::regex(R"(\.:|:\.)"), ".");

    // Исправляем частые OCR-ошибки цифр только в цифровой части (до конца 5-й группы)
    if (std::count(s.begin(), s.end(), '.') >= 3)
    {
        size_t dots = 0;
        size_t afterLastGroup = 0;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '.')
            {
                ++dots;
            }
            if (dots == 4)
            {
                size_t digitsCount = 0;
                size_t j = i + 1;
                while (j < s.size() && digitsCount < 3)
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

        const size_t limit = (afterLastGroup > 0) ? std::min(afterLastGroup, s.size()) : s.size();
        for (size_t i = 0; i < limit; ++i)
        {
            char& ch = s[i];
            if (ch == ':' || ch == 'I' || ch == 'l' || ch == 'T' || ch == '!')
            {
                ch = '1';
            }
            else if(ch == 'O' || ch == 'o' || ch == 'U' || ch == 'D' || ch == 'Q' || ch == 'C' || ch == 'c')
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
    }

    std::regex baseNumRegex(R"((\d{3}\s*\.\s*\d{2}\s*\.\s*\d{2}\s*\.\s*\d{1,2}\s*\.\s*\d{3}))");
    std::smatch match;
    if (!std::regex_search(s, match, baseNumRegex))
    {
        return info;
    }

    info.number = std::regex_replace(match.str(1), std::regex(R"(\s+)"), "");

    std::string tail = s.substr(size_t(match.position() + match.length()));
    while (!tail.empty() && tail.front() == ' ')
    {
        tail.erase(tail.begin());
    }

    if (tail.empty())
    {
        return info;
    }

    auto extractDocCode = [](std::string& t) -> pdm::shared::DocumentTypeCode {
        while (!t.empty() && (t.back() == ' ' || t.back() == '.' || t.back() == ','))
        {
            t.pop_back();
        }
        const std::vector<std::pair<std::string, pdm::shared::DocumentTypeCode>> codes = {
            {"СБ", pdm::shared::DocumentTypeCode::AssemblyDrawing},
            {"CB", pdm::shared::DocumentTypeCode::AssemblyDrawing},
            {"C5", pdm::shared::DocumentTypeCode::AssemblyDrawing},
            {"C6", pdm::shared::DocumentTypeCode::AssemblyDrawing},
            {"ТУ", pdm::shared::DocumentTypeCode::TechnicalSpecifications},
            {"TY", pdm::shared::DocumentTypeCode::TechnicalSpecifications}
        };
        for (const auto& [codeStr, codeEnum] : codes)
        {
            if (t.size() >= codeStr.size() &&
                t.compare(t.size() - codeStr.size(), codeStr.size(), codeStr) == 0)
            {
                t.erase(t.size() - codeStr.size());
                while (!t.empty() && t.back() == ' ')
                {
                    t.pop_back();
                }
                return codeEnum;
            }
        }
        return pdm::shared::DocumentTypeCode::Unknown;
    };

    info.code = extractDocCode(tail);

    while (!tail.empty() && tail.front() == ' ')
    {
        tail.erase(tail.begin());
    }
    while (!tail.empty() && tail.back() == ' ')
    {
        tail.pop_back();
    }

    // Если осталась одна заглавная буква (например "A", "А", "Б", "В", "Г") — прикрепляем её к номеру
    if (tail.size() == 1 && std::isalpha(static_cast<unsigned char>(tail[0])))
    {
        std::string norm = normalizeSuffixLetter(tail[0]);
        info.number += norm.empty() ? tail : norm;
    }
    else if (tail.size() == 2 && (tail[0] == '\xD0' || tail[0] == '\xD1'))
    {
        info.number += tail;
    }

    return info;
}

std::string recognizeCrop(cv::dnn::Net& recNet, const cv::Mat& crop, const std::vector<std::string>& keys)
{
    if (crop.empty() || crop.rows <= 0 || crop.cols <= 0)
    {
        return "";
    }

    constexpr int recH = 48;
    constexpr int maxRecW = 640;
    int targetW = std::min(maxRecW, std::max(8, int(std::round(float(crop.cols) * recH / crop.rows))));

    cv::Mat resized;
    cv::resize(crop, resized, cv::Size(targetW, recH), 0, 0, cv::INTER_CUBIC);

    cv::Mat padded(recH, maxRecW, CV_8UC3, cv::Scalar(128, 128, 128));
    resized.copyTo(padded(cv::Rect(0, 0, targetW, recH)));

    cv::Mat recInput;
    padded.convertTo(recInput, CV_32FC3, 1.0 / 255.0);
    cv::subtract(recInput, cv::Scalar(0.5, 0.5, 0.5), recInput);
    cv::divide(recInput, cv::Scalar(0.5, 0.5, 0.5), recInput);

    cv::Mat recBlob = cv::dnn::blobFromImage(recInput, 1.0, cv::Size(), cv::Scalar(), false, false);
    recNet.setInput(recBlob);
    cv::Mat recOut = recNet.forward();

    if (recOut.empty() || recOut.dims < 3)
    {
        return "";
    }

    const int seqLen = recOut.size[1];
    const int numClasses = recOut.size[2];
    const float* data = reinterpret_cast<const float*>(recOut.data);

    std::string lineText;
    int prevIdx = 0;
    for (int t = 0; t < seqLen; ++t)
    {
        const float* stepScores = data + t * numClasses;
        int maxIdx = int(std::max_element(stepScores, stepScores + numClasses) - stepScores);

        if (maxIdx > 0 && maxIdx != prevIdx && maxIdx < int(keys.size()))
        {
            lineText += keys.at(size_t(maxIdx));
        }
        prevIdx = maxIdx;
    }

    return lineText;
}

int parseSheetNumber(const std::string& raw, bool isForm2aCell = false)
{
    if (raw.find('A') != std::string::npos || raw.find("А") != std::string::npos ||
        raw.find("340") != std::string::npos || raw.find("310") != std::string::npos)
    {
        return 0;
    }

    std::string s = raw;
    while (!s.empty() && (s.front() == 'I' || s.front() == 'l' || s.front() == '|' || s.front() == '/' || s.front() == ' '))
    {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == 'I' || s.back() == 'l' || s.back() == '|' || s.back() == '/' || s.back() == ']' || s.back() == ' '))
    {
        s.pop_back();
    }

    std::string digits;
    for (char c : s)
    {
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            digits += c;
        }
        else if (c == 'O' || c == 'o' || c == 'D')
        {
            digits += '0';
        }
        else if (c == 'B')
        {
            digits += '8';
        }
    }

    if (isForm2aCell && digits.size() == 2 && digits.front() == '1' && digits[1] >= '2' && digits[1] <= '9')
    {
        digits.erase(digits.begin());
    }
    else if (isForm2aCell && digits.size() == 2 && digits.back() == '1' && digits[0] >= '2' && digits[0] <= '9')
    {
        digits.pop_back();
    }

    if (digits.empty())
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

bool isStopWord(const std::string& s)
{
    return s.find("Копиров") != std::string::npos ||
           s.find("Копироб") != std::string::npos ||
           s.find("Конивоб") != std::string::npos ||
           s.find("Формат") != std::string::npos  ||
           s.find("Фопмот") != std::string::npos  ||
           s.find("Сборочн") != std::string::npos ||
           s.find("Соорочн") != std::string::npos ||
           s.find("ГОСТ") != std::string::npos    ||
           s.find("Масса") != std::string::npos   ||
           s.find("Масштаб") != std::string::npos ||
           s.find("Дата") != std::string::npos    ||
           s.find("Цата") != std::string::npos    ||
           s.find("чертеж") != std::string::npos;
}

void detectDocumentCode(const std::string& text, pdm::shared::DocumentTypeCode& code)
{
    if (code != pdm::shared::DocumentTypeCode::Unknown)
    {
        return;
    }

    if (text.find("СБ") != std::string::npos ||
        text.find("C6") != std::string::npos ||
        text.find("C5") != std::string::npos ||
        text.find("CB") != std::string::npos ||
        text.find("Сборочный") != std::string::npos ||
        text.find("чертеж") != std::string::npos)
    {
        code = pdm::shared::DocumentTypeCode::AssemblyDrawing;
    }
    else if (text.find("ТУ") != std::string::npos ||
             text.find("TY") != std::string::npos)
    {
        code = pdm::shared::DocumentTypeCode::TechnicalSpecifications;
    }
}

} // namespace

namespace pdm::infrastructure
{

OnnxStampRecognizer::OnnxStampRecognizer(const std::filesystem::path& modelsDir)
{
    cv::setUseOptimized(false);
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);

    try
    {
        qDebug().noquote() << "[OpenCV-DNN] Initializing models from:"
                           << QString::fromStdString(modelsDir.string());

        m_detNet = cv::dnn::readNetFromONNX((modelsDir / "det.onnx").string());
        m_recNet = cv::dnn::readNetFromONNX((modelsDir / "rec.onnx").string());

        std::ifstream dictFile(modelsDir / "cyrillic_dict.txt");
        if (!dictFile.is_open())
        {
            std::cerr << "[OpenCV-DNN] ERROR: Cannot open cyrillic_dict.txt!\n" << std::flush;
            return;
        }

        m_keys.clear();
        m_keys.push_back("#");
        std::string line;
        while (std::getline(dictFile, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                m_keys.push_back(line);
            }
        }
        m_keys.push_back(" ");

        qDebug() << "[OpenCV-DNN] Loaded dictionary keys:" << m_keys.size();
        m_initialized = (!m_detNet.empty() && !m_recNet.empty());

        if (m_initialized)
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

std::optional<shared::Document> OnnxStampRecognizer::recognize(const std::filesystem::path& imagePath)
{
    if (!m_initialized)
    {
        std::cerr << "[OpenCV-DNN] ERROR: Recognizer is not initialized.\n" << std::flush;
        return std::nullopt;
    }

    try
    {
        qDebug().noquote() << "[OpenCV-DNN] 1. Start recognizing:"
                           << QString::fromStdString(imagePath.string());
        Image img{ imagePath };
        int cropX{ std::max(0, img.width() - kOffsetX - kCropW) };
        int cropY{ std::max(0, img.height() - kOffsetY - kCropH) };
        Image stamp{ img.getPartOfImage(cropX, cropY, kCropW, kCropH) };

        const int w = stamp.width();
        const int h = stamp.height();
        const int channels = stamp.channels();
        const unsigned char* rawData = stamp.get();

        if (!rawData || w <= 0 || h <= 0)
        {
            return std::nullopt;
        }

        cv::Mat bgrMat(h, w, CV_8UC3);
        for (int y = 0; y < h; ++y)
        {
            unsigned char* dstRow = bgrMat.ptr(y);
            const unsigned char* srcRow = rawData + size_t(y) * size_t(w) * size_t(channels);
            for (int x = 0; x < w; ++x)
            {
                if (channels == 1)
                {
                    const unsigned char g = srcRow[x];
                    dstRow[x * 3 + 0] = g;
                    dstRow[x * 3 + 1] = g;
                    dstRow[x * 3 + 2] = g;
                }
                else
                {
                    dstRow[x * 3 + 0] = srcRow[x * channels + 2];
                    dstRow[x * 3 + 1] = srcRow[x * channels + 1];
                    dstRow[x * 3 + 2] = srcRow[x * channels + 0];
                }
            }
        }

        cv::Mat gray;
        cv::Mat enhanced;
        cv::cvtColor(bgrMat, gray, cv::COLOR_BGR2GRAY);
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
        clahe->apply(gray, enhanced);
        cv::cvtColor(enhanced, bgrMat, cv::COLOR_GRAY2BGR);

        constexpr int detW = 1024;
        constexpr int detH = 1024;

        cv::Mat detInput;
        cv::resize(bgrMat, detInput, cv::Size(detW, detH));
        detInput.convertTo(detInput, CV_32FC3, 1.0 / 255.0);

        cv::subtract(detInput, cv::Scalar(0.485, 0.456, 0.406), detInput);
        cv::divide(detInput, cv::Scalar(0.229, 0.224, 0.225), detInput);

        cv::Mat detBlob = cv::dnn::blobFromImage(detInput, 1.0, cv::Size(), cv::Scalar(), true, false);
        m_detNet.setInput(detBlob);
        cv::Mat detOut = m_detNet.forward();

        if (detOut.empty() || detOut.total() < size_t(detH * detW))
        {
            std::cerr << "[OpenCV-DNN] ERROR: Invalid detOut shape!\n" << std::flush;
            return std::nullopt;
        }

        cv::Mat probMap(detH, detW, CV_32FC1, detOut.data);
        float scaleX = float(w) / float(detW);
        float scaleY = float(h) / float(detH);

        auto boxes = findTextBoxes(probMap, scaleX, scaleY);

        qDebug().noquote() << "[OpenCV-DNN] DETECTED BLOCKS (x, y):\n";
        for (auto& tb : boxes)
        {
            cv::Mat crop = getRotateCropImage(bgrMat, tb.box);
            tb.text = recognizeCrop(m_recNet, crop, m_keys);
            if (!tb.text.empty())
            {
                qDebug().noquote()
                << QString("  (%1, %2): %3")
                        .arg(int(tb.box.front().x))
                        .arg(int(tb.box.front().y))
                        .arg(QString::fromStdString(tb.text));
            }
        }
        qDebug().noquote() << "-------------------\n";

        shared::Document doc;
        doc.sheet = 1;
        doc.totalSheets = 1;

        // 1. Ищем децимальный номер. Если в кадре два номера (верхний — "Первичное применение",
        //    нижний — основной штамп), выбираем НИЖНИЙ (с наибольшим Y).
        float numX = 200.0f;
        float numY = -1.0f;
        for (const auto& tb : boxes)
        {
            if (tb.text.empty())
            {
                continue;
            }
            auto parsedNum = parseDecimalAndCode(tb.text);
            if (!parsedNum.number.empty())
            {
                const float by = tb.box.front().y;
                if (by > numY)
                {
                    doc.number = parsedNum.number;
                    doc.code = parsedNum.code;
                    numX = tb.box.front().x;
                    numY = by;
                }
            }
        }

        if (doc.number.empty())
        {
            qWarning().noquote() << "OnnxStampRecognizer::recognize: Decimal number not found.";
            return std::nullopt;
        }

        // 2. Собираем многострочное наименование (только если номер не у самого низа кропа, т.е. это не Форма 2а)
        std::vector<std::pair<float, std::string>> nameParts;
        if (numY <= 330.0f)
        {
            for (const auto& tb : boxes)
            {
                const float bx = tb.box.front().x;
                const float by = tb.box.front().y;
                const std::string& line = tb.text;

                if (line.size() < 2)
                {
                    continue;
                }
                if (bx < numX - 240.0f || bx > numX + 160.0f)
                {
                    continue;
                }
                if (bx < 75.0f)
                {
                    continue;
                }
                if (by <= numY + 25.0f || by > numY + 220.0f)
                {
                    continue;
                }

                std::string cleanedPart = cleanRussianName(line);
                detectDocumentCode(cleanedPart, doc.code);

                // Проверяем, что после замены латинских двойников ("Wum" -> "Щит") в строке есть кириллица
                if ((cleanedPart.find('\xD0') != std::string::npos || cleanedPart.find('\xD1') != std::string::npos) &&
                    cleanedPart.size() >= 4 && !isStopWord(cleanedPart))
                {
                    nameParts.emplace_back(by, cleanedPart);
                }
            }
        }

        std::sort(nameParts.begin(), nameParts.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        for (size_t i = 0; i < nameParts.size(); ++i)
        {
            if (i > 0)
            {
                doc.name += " ";
            }
            doc.name += nameParts[i].second;
        }

        const bool isForm2a = doc.name.empty();

        // 3. Ищем графы "Лист" и "Листов" правее децимального номера
        float sheetHeaderX = -1.0f;
        float sheetHeaderY = -1.0f;
        float totalHeaderX = -1.0f;
        float totalHeaderY = -1.0f;

        for (const auto& tb : boxes)
        {
            const float bx = tb.box.front().x;
            const float by = tb.box.front().y;
            if (bx <= numX + 250.0f || by < numY - 30.0f)
            {
                continue;
            }

            if (tb.text.find("Листов") != std::string::npos ||
                tb.text.find("Листоб") != std::string::npos ||
                tb.text.find("Лucmob") != std::string::npos ||
                tb.text.find("Лucmoб") != std::string::npos)
            {
                totalHeaderX = bx;
                totalHeaderY = by;
            }
            else if (tb.text.find("Лист") != std::string::npos ||
                     tb.text.find("Лиcт") != std::string::npos ||
                     tb.text.find("Лucm") != std::string::npos ||
                     tb.text.find("llucm") != std::string::npos ||
                     tb.text.find("ucm") != std::string::npos ||
                     tb.text.find("ucт") != std::string::npos ||
                     tb.text.find("ucп") != std::string::npos ||
                     tb.text.find("Шucm") != std::string::npos ||
                     tb.text.find("VZUL") != std::string::npos)
            {
                sheetHeaderX = bx;
                sheetHeaderY = by;
            }
        }

        // На Форме 1 (когда есть наименование, !isForm2a) текущий лист по ГОСТ 2.104 ВСЕГДА равен 1.
        // Искать номер текущего листа нужно только на Форме 2а (последующие листы без наименования).
        bool sheetFoundInBoxes = !isForm2a;
        bool totalFoundInBoxes = false;

        auto extractTrailingNumber = [&](const std::string& headerText, const std::string& keyword) -> int {
            size_t pos = headerText.find(keyword);
            if (pos == std::string::npos)
            {
                return 0;
            }
            std::string tail = headerText.substr(pos + keyword.size());
            return parseSheetNumber(tail, false);
        };

        // 3.1 Проверяем, не приклеилась ли цифра прямо к заголовку ("Листов 1", "Лист 2")
        for (const auto& tb : boxes)
        {
            const float bx = tb.box.front().x;
            const float by = tb.box.front().y;
            if (bx <= numX + 250.0f || by < numY - 30.0f)
            {
                continue;
            }

            if (totalHeaderX > 0.0f && std::abs(bx - totalHeaderX) < 10.0f && std::abs(by - totalHeaderY) < 10.0f)
            {
                int val = extractTrailingNumber(tb.text, "Листов");
                if (val == 0) val = extractTrailingNumber(tb.text, "Листоб");
                if (val == 0) val = extractTrailingNumber(tb.text, "Лucmob");
                if (val == 0) val = extractTrailingNumber(tb.text, "Лucmoб");
                if (val > 0 && val < 100)
                {
                    doc.totalSheets = val;
                    totalFoundInBoxes = true;
                }
            }
            if (!sheetFoundInBoxes && sheetHeaderX > 0.0f && std::abs(bx - sheetHeaderX) < 10.0f && std::abs(by - sheetHeaderY) < 10.0f)
            {
                int val = extractTrailingNumber(tb.text, "Лист");
                if (val == 0) val = extractTrailingNumber(tb.text, "Лиcт");
                if (val == 0) val = extractTrailingNumber(tb.text, "Лucm");
                if (val > 0 && val < 100)
                {
                    doc.sheet = val;
                    sheetFoundInBoxes = true;
                }
            }
        }

        // 3.2 Ищем значения в соседних боксах (снизу для Формы 2, справа для Формы 1)
        for (const auto& tb : boxes)
        {
            const float bx = tb.box.front().x;
            const float by = tb.box.front().y;

            if (!sheetFoundInBoxes && sheetHeaderX > 0.0f)
            {
                bool isBelow = (std::abs(bx - sheetHeaderX) < 60.0f && by > sheetHeaderY + 12.0f && by < sheetHeaderY + 60.0f);
                bool isRight = (std::abs(by - sheetHeaderY) < 22.0f && bx > sheetHeaderX + 35.0f && bx < sheetHeaderX + 140.0f);
                if (isBelow || isRight)
                {
                    int val = parseSheetNumber(tb.text, isForm2a);
                    if (val > 0 && val < 100)
                    {
                        doc.sheet = val;
                        sheetFoundInBoxes = true;
                    }
                }
            }

            if (!totalFoundInBoxes && totalHeaderX > 0.0f)
            {
                bool isBelow = (std::abs(bx - totalHeaderX) < 60.0f && by > totalHeaderY + 12.0f && by < totalHeaderY + 60.0f);
                bool isRight = (std::abs(by - totalHeaderY) < 22.0f && bx > totalHeaderX + 45.0f && bx < totalHeaderX + 160.0f);
                if (isBelow || isRight)
                {
                    int val = parseSheetNumber(tb.text, false);
                    if (val > 0 && val < 100)
                    {
                        doc.totalSheets = val;
                        totalFoundInBoxes = true;
                    }
                }
            }
        }

        // 3.3 Прямой дораспознаватель через recNet (только если значение ещё не найдено в boxes)
        if (!sheetFoundInBoxes && sheetHeaderX > 0.0f)
        {
            int rightX = std::min(bgrMat.cols - 1, int(sheetHeaderX) + 75);
            int rightY = std::max(0, int(sheetHeaderY) - 4);
            int rightW = std::min(bgrMat.cols - rightX, 75);
            int rightH = std::min(bgrMat.rows - rightY, 34);
            if (rightW > 10 && rightH > 10)
            {
                std::string cellText = recognizeCrop(m_recNet, bgrMat(cv::Rect(rightX, rightY, rightW, rightH)), m_keys);
                int val = parseSheetNumber(cellText, isForm2a);
                if (val > 0 && val < 100)
                {
                    doc.sheet = val;
                }
            }

            if (doc.sheet == 1)
            {
                int belowX = std::max(0, int(sheetHeaderX) - 6);
                int belowY = std::max(0, int(sheetHeaderY) + 26);
                int belowW = std::min(bgrMat.cols - belowX, 90);
                int belowH = std::min(bgrMat.rows - belowY, 38);
                if (belowW > 10 && belowH > 10)
                {
                    std::string cellText = recognizeCrop(m_recNet, bgrMat(cv::Rect(belowX, belowY, belowW, belowH)), m_keys);
                    int val = parseSheetNumber(cellText, isForm2a);
                    if (val > 0 && val < 100)
                    {
                        doc.sheet = val;
                    }
                }
            }
        }

        if (!totalFoundInBoxes && totalHeaderX > 0.0f)
        {
            int rightX = std::min(bgrMat.cols - 1, int(totalHeaderX) + 85);
            int rightY = std::max(0, int(totalHeaderY) - 4);
            int rightW = std::min(bgrMat.cols - rightX, 75);
            int rightH = std::min(bgrMat.rows - rightY, 34);
            if (rightW > 10 && rightH > 10)
            {
                std::string cellText = recognizeCrop(m_recNet, bgrMat(cv::Rect(rightX, rightY, rightW, rightH)), m_keys);
                int val = parseSheetNumber(cellText, false);
                if (val > 0 && val < 25)
                {
                    doc.totalSheets = val;
                }
            }
        }

        // 3.4 Поиск в правом углу выполняем ТОЛЬКО для Формы 2а (когда нет наименования)
        if (isForm2a && sheetHeaderX < 0.0f && doc.sheet == 1)
        {
            int cellX = std::max(int(numX + 450.0f), bgrMat.cols - 165);
            cellX = std::min(bgrMat.cols - 1, cellX);
            int cellY = std::max(0, int(numY) + 15);
            int cellW = std::min(bgrMat.cols - cellX, 155);
            int cellH = std::min(bgrMat.rows - cellY, 70);
            if (cellW > 15 && cellH > 15)
            {
                std::string cellText = recognizeCrop(m_recNet, bgrMat(cv::Rect(cellX, cellY, cellW, cellH)), m_keys);
                int val = parseSheetNumber(cellText, true);
                if (val > 1 && val < 100)
                {
                    doc.sheet = val;
                }
            }
        }

        if (isForm2a && doc.sheet == 1)
        {
            doc.sheet = 2;
        }

        qDebug().noquote()
            << QString("[OpenCV-DNN] PARSED -> Number: [%1] | Name: [%2] | Sheet: %3/%4")
                   .arg(QString::fromStdString(doc.number))
                   .arg(QString::fromStdString(doc.name))
                   .arg(doc.sheet)
                   .arg(doc.totalSheets);

        if (doc.number.empty() || (doc.sheet == 1 && doc.name.empty()))
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
