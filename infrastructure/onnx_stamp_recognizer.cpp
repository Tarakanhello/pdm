#include "onnx_stamp_recognizer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/core/utils/logger.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <clipper.hpp>

#include "image.h"

#include <QDebug>
#include <QImage>
#include <QImageReader>
#include <QString>


namespace pdm::infrastructure
{

namespace
{

cv::Mat loadGrayImage(const std::filesystem::path& imagePath)
{
    Image img{ imagePath };
    const int fullW{ img.width() };
    const int fullH{ img.height() };
    const int channels{ img.channels() };
    const unsigned char* rawData{ img.get() };

    if(!rawData || fullW <= 0 || fullH <= 0 || (channels != 1 && channels != 3 && channels != 4))
    {
        return {};
    }

    const int srcType{ (channels == 1) ? CV_8UC1 : (channels == 4 ? CV_8UC4 : CV_8UC3) };
    const int toGrayCode{ (channels == 1) ? -1
                                         : (channels == 4 ? cv::COLOR_RGBA2GRAY
                                                          : cv::COLOR_RGB2GRAY) };

    const cv::Mat srcMat{ fullH, fullW, srcType, const_cast<unsigned char*>(rawData) };
    cv::Mat fullGray{};
    if(toGrayCode == -1)
    {
        fullGray = srcMat.clone();
    }
    else
    {
        cv::cvtColor(srcMat, fullGray, toGrayCode);
    }

    return fullGray;
}

std::pair<double, double> readDpiScale(const std::filesystem::path& imagePath)
{
    constexpr double kReferenceDpi{ 300.0 };
    constexpr double kMetersPerInch{ 0.0254 };

    double scaleX{ 1.0 };
    double scaleY{ 1.0 };
    QImageReader metadataReader{ QString::fromStdString(imagePath.string()) };
    const QImage metadataImage{ metadataReader.read() };
    if(!metadataImage.isNull() &&
        metadataImage.dotsPerMeterX() > 0 &&
        metadataImage.dotsPerMeterY() > 0)
    {
        const double dpiX{ metadataImage.dotsPerMeterX() * kMetersPerInch };
        const double dpiY{ metadataImage.dotsPerMeterY() * kMetersPerInch };
        scaleX = dpiX / kReferenceDpi;
        scaleY = dpiY / kReferenceDpi;
    }

    return { scaleX, scaleY };
}

void saveStampDebugImage(const std::filesystem::path& debugDir,
                         const std::filesystem::path& imagePath,
                         const cv::Mat& fullGray,
                         const cv::Rect& cropRect,
                         const std::optional<cv::Point>& corner)
{
    if(debugDir.empty())
    {
        return;
    }

    cv::Mat fullDebugVis{};
    cv::cvtColor(fullGray, fullDebugVis, cv::COLOR_GRAY2BGR);
    cv::rectangle(fullDebugVis, cropRect, cv::Scalar(255, 0, 0), 3);
    if(corner.has_value())
    {
        cv::drawMarker(fullDebugVis, *corner, cv::Scalar(0, 200, 0), cv::MARKER_CROSS, 80, 6);
    }
    const auto outPath{ debugDir / ("debug_full_" + imagePath.filename().string()) };
    cv::imwrite(outPath.string(), fullDebugVis);
}


struct StampSegment
{
    int start{ 0 };
    int end{ 0 };
    int pos{ 0 };
};

std::vector<StampSegment> extractStampSegments(const cv::Mat& mask, bool horizontal, int minLen)
{
    cv::Mat labels{}, stats{}, centroids{};
    const int n{ cv::connectedComponentsWithStats(mask, labels, stats, centroids) };
    std::vector<StampSegment> segs{};
    segs.reserve(static_cast<size_t>(n));
    for(int i{ 1 }; i < n; ++i)
    {
        const int l{ stats.at<int>(i, cv::CC_STAT_LEFT) };
        const int t{ stats.at<int>(i, cv::CC_STAT_TOP) };
        const int w{ stats.at<int>(i, cv::CC_STAT_WIDTH) };
        const int h{ stats.at<int>(i, cv::CC_STAT_HEIGHT) };
        if(horizontal && w >= minLen)
        {
            segs.push_back({ l, l + w - 1, t + h / 2 });
        }
        else if(!horizontal && h >= minLen)
        {
            segs.push_back({ t, t + h - 1, l + w / 2 });
        }
    }
    return segs;
}

std::optional<int> findStampEdgeByProfile(const std::vector<StampSegment>& segs,
                                          int maxCoord,
                                          int minCoord,
                                          int minJump,
                                          size_t minGroupSize,
                                          int stabilityWidth = 12,
                                          int clusterTol = 25)
{
    if(maxCoord - minCoord <= stabilityWidth || segs.empty())
    {
        return std::nullopt;
    }

    std::vector<int> profile(static_cast<size_t>(maxCoord + 1), 0);
    for(const auto& segment : segs)
    {
        const int a{ std::clamp(segment.start, 0, maxCoord) };
        const int b{ std::clamp(segment.end, 0, maxCoord) };
        for(int coord{ a }; coord <= b; ++coord)
        {
            ++profile[static_cast<size_t>(coord)];
        }
    }

    std::vector<int> edgeSamples{};
    for(int coord{ maxCoord }; coord > std::max(minCoord, maxCoord - 15); --coord)
    {
        edgeSamples.push_back(profile[static_cast<size_t>(coord)]);
    }
    std::sort(edgeSamples.begin(), edgeSamples.end());
    const int baseline{ edgeSamples.empty() ? 0 : edgeSamples[edgeSamples.size() / 2] };

    int stableEntry{ -1 };
    int streak{ 0 };
    for(int coord{ maxCoord }; coord >= minCoord; --coord)
    {
        if(profile[static_cast<size_t>(coord)] - baseline >= minJump)
        {
            ++streak;
            if(streak >= stabilityWidth)
            {
                stableEntry = coord;
                break;
            }
        }
        else
        {
            streak = 0;
        }
    }

    if(stableEntry < 0)
    {
        return std::nullopt;
    }

    std::vector<int> ends{};
    for(const auto& segment : segs)
    {
        if(segment.start <= stableEntry && segment.end >= stableEntry)
        {
            ends.push_back(segment.end);
        }
    }
    if(ends.size() < minGroupSize)
    {
        return std::nullopt;
    }

    std::sort(ends.begin(), ends.end(), std::greater<int>());
    std::vector<int> bestCluster{};
    for(size_t i{ 0 }; i < ends.size(); ++i)
    {
        std::vector<int> cluster{};
        for(size_t j{ i }; j < ends.size(); ++j)
        {
            if(ends[i] - ends[j] <= clusterTol)
            {
                cluster.push_back(ends[j]);
            }
        }
        if(cluster.size() >= minGroupSize)
        {
            bestCluster = std::move(cluster);
            break;
        }
    }

    if(bestCluster.size() < minGroupSize)
    {
        return std::nullopt;
    }

    std::sort(bestCluster.begin(), bestCluster.end());
    return bestCluster[bestCluster.size() / 2];
}

void findStampCornerFallback(const std::vector<StampSegment>& hSegs,
                             const std::vector<StampSegment>& vSegs,
                             int searchBottom,
                             std::optional<int>& foundX,
                             std::optional<int>& foundY)
{
    // Запасной поиск напрямую по длинным отрезкам штампа, если профильный поиск не сработал
    if(!foundY.has_value() || !foundX.has_value())
    {
        // 1. Собираем все горизонтальные отрезки длиннее 100 px
        std::vector<StampSegment> longH{};
        for(const auto& s : hSegs)
        {
            if((s.end - s.start) >= 100 && s.pos <= searchBottom - 5)
            {
                longH.push_back(s);
            }
        }

        // 2. Ищем самый правый кластер из >= 2 горизонтальных линий, заканчивающихся на одной вертикали (±25 px)
        if(!foundX.has_value() && !longH.empty())
        {
            std::sort(longH.begin(), longH.end(), [](const StampSegment& a, const StampSegment& b) {
                return a.end > b.end;
            });

            for(size_t i{ 0 }; i < longH.size(); ++i)
            {
                std::vector<int> clusterX{};
                for(size_t j{ i }; j < longH.size(); ++j)
                {
                    if(longH[i].end - longH[j].end <= 25)
                    {
                        clusterX.push_back(longH[j].end);
                    }
                }
                if(clusterX.size() >= 2)
                {
                    std::sort(clusterX.begin(), clusterX.end());
                    foundX = clusterX[clusterX.size() / 2];
                    break;
                }
            }
        }

        // 3. Зная foundX, ищем нижнюю границу foundY по вертикальным линиям вблизи foundX (±30 px)
        //    или по самой нижней горизонтальной линии из этого кластера
        if(foundX.has_value() && !foundY.has_value())
        {
            int maxVertEndY{ -1 };
            for(const auto& v : vSegs)
            {
                if(std::abs(v.pos - *foundX) <= 30 && v.end <= searchBottom - 5)
                {
                    maxVertEndY = std::max(maxVertEndY, v.end);
                }
            }

            int maxHorizPosY{ -1 };
            for(const auto& h : longH)
            {
                if(std::abs(h.end - *foundX) <= 25)
                {
                    maxHorizPosY = std::max(maxHorizPosY, h.pos);
                }
            }

            if(maxVertEndY > 0 || maxHorizPosY > 0)
            {
                foundY = std::max(maxVertEndY, maxHorizPosY);
            }
        }
    }

}




struct StampLineSegments
{
    std::vector<StampSegment> horizontal{};
    std::vector<StampSegment> vertical{};
};




StampLineSegments extractStampLineSegments(const cv::Mat& zone, int searchRight, int searchBottom)
{
    cv::Mat denoised{};
    cv::medianBlur(zone, denoised, 3);

    cv::Mat binary{};
    cv::adaptiveThreshold(denoised, binary, 255,
                          cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV, 25, 22);

    // Обнуляем найденные чёрные полосы сканера на бинарной маске
    if(searchRight < zone.cols - 1)
    {
        binary.colRange(std::max(0, searchRight + 1), zone.cols).setTo(0);
    }
    if(searchBottom < zone.rows - 1)
    {
        binary.rowRange(std::max(0, searchBottom + 1), zone.rows).setTo(0);
    }

    constexpr int kHorizClosePx{ 35 };
    constexpr int kVertClosePx{ 20 };
    constexpr int kMinHorizSegPx{ 140 };
    constexpr int kMinVertSegPx{ 35 };

    cv::Mat closedH{}, closedV{};
    cv::morphologyEx(binary, closedH, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(kHorizClosePx, 1)));
    cv::morphologyEx(binary, closedV, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, kVertClosePx)));

    cv::Mat horizMask{}, vertMask{};
    cv::morphologyEx(closedH, horizMask, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(kMinHorizSegPx, 1)));
    cv::morphologyEx(closedV, vertMask, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, kMinVertSegPx)));

    const auto hSegs{ extractStampSegments(horizMask, true, kMinHorizSegPx) };
    const auto vSegs{ extractStampSegments(vertMask, false, kMinVertSegPx) };

    return { hSegs, vSegs };
}

} // namespace


void OnnxStampRecognizer::setDebugOutputDir(const std::filesystem::path& debugDir)
{
    m_debugDir = debugDir;
    if(!m_debugDir.empty())
    {
        std::error_code ec{};
        std::filesystem::create_directories(m_debugDir, ec);
    }
}

std::optional<cv::Point> OnnxStampRecognizer::findStampBottomRightCorner(const cv::Mat& fullGray)
{
    if(fullGray.empty() || fullGray.type() != CV_8UC1)
    {
        return std::nullopt;
    }

    // 1. Вырезаем правую нижнюю область поиска
    const int zoneW{ std::min(kSearchZoneWidthPx, fullGray.cols) };
    const int zoneH{ std::min(kSearchZoneHeightPx, fullGray.rows) };
    if(zoneW <= 200 || zoneH <= 200)
    {
        return std::nullopt;
    }

    const int offsetX{ fullGray.cols - zoneW };
    const int offsetY{ fullGray.rows - zoneH };
    const cv::Mat zone{ fullGray(cv::Rect(offsetX, offsetY, zoneW, zoneH)) };

    // 2. Адаптивно определяем границы чёрных полей сканера справа и снизу
    // На реальном чертеже в строке/столбце не бывает > 50% сплошного чёрного цвета;
    // проверяем полосу до 150 px от края, чтобы перешагнуть белые зазоры у кромки.
    constexpr int kMaxBorderScanPx{ 150 };
    constexpr int kDarkPixelThresh{ 70 };

    int searchRight{ zoneW - 1 };
    for(int x{ zoneW - 1 }; x > std::max(zoneW / 2, zoneW - kMaxBorderScanPx); --x)
    {
        const int darkPixels{ zoneH - cv::countNonZero(zone.col(x) > kDarkPixelThresh) };
        if(darkPixels > static_cast<int>(zoneH * 0.50))
        {
            searchRight = x - 6; // отступаем на 6 px внутрь от края тени сканера
        }
    }

    int searchBottom{ zoneH - 1 };
    for(int y{ zoneH - 1 }; y > std::max(zoneH / 2, zoneH - kMaxBorderScanPx); --y)
    {
        const int darkPixels{ zoneW - cv::countNonZero(zone.row(y) > kDarkPixelThresh) };
        if(darkPixels > static_cast<int>(zoneW * 0.50))
        {
            searchBottom = y - 6;
        }
    }

    const StampLineSegments lineSegments{ extractStampLineSegments(zone, searchRight, searchBottom) };
    const auto& hSegs{ lineSegments.horizontal };
    const auto& vSegs{ lineSegments.vertical };

    // 5. Сначала определяем нижнюю координату Y.
    // В нижней части Формы 1 (графа организации) всего 2 вертикальные линии рамки (левая и правая),
    // поэтому используем minJump = 2 (с запасным minJump = 1).
    const int minSearchY{ std::max(0, searchBottom - 350) };
    auto foundY{ findStampEdgeByProfile(vSegs, searchBottom, minSearchY, 2, 2) };
    if(!foundY.has_value())
    {
        foundY = findStampEdgeByProfile(vSegs, searchBottom, minSearchY, 1, 2, 12, 30);
    }

    if(!foundY.has_value())
    {
        return std::nullopt;
    }

    // 6. Оставляем только горизонтальные линии, лежащие по высоте внутри штампа
    constexpr int kMaxStampBandHeightPx{ 340 };
    constexpr int kBottomBandTolPx{ 12 };

    std::vector<StampSegment> stampHSegs{};
    stampHSegs.reserve(hSegs.size());
    for(const auto& s : hSegs)
    {
        if(s.pos >= (*foundY - kMaxStampBandHeightPx) && s.pos <= (*foundY + kBottomBandTolPx))
        {
            stampHSegs.push_back(s);
        }
    }

    // Определяем правую координату X по горизонтальным линиям внутри полосы штампа
    const int minSearchX{ std::max(0, searchRight - 900) };
    auto foundX{ findStampEdgeByProfile(stampHSegs, searchRight, minSearchX, 2, 3) };
    if(!foundX.has_value())
    {
        foundX = findStampEdgeByProfile(stampHSegs, searchRight, minSearchX, 2, 2);
    }
    if(!foundX.has_value())
    {
        foundX = findStampEdgeByProfile(stampHSegs, searchRight, minSearchX, 1, 2, 12, 30);
    }

    findStampCornerFallback(hSegs, vSegs, searchBottom, foundX, foundY);

    // Проверяем, что найденный правый край штампа (foundX) подтверждён вертикальной линией рамки
    if(foundX.has_value())
    {
        const bool hasVertBorder{ std::any_of(vSegs.begin(), vSegs.end(), [&foundX](const StampSegment& v) {
            return std::abs(v.pos - *foundX) <= 25;
        }) };

        if(!hasVertBorder)
        {
            int bestEndX{ -1 };
            for(const auto& h : stampHSegs)
            {
                if((h.end - h.start) >= 250 && h.end > bestEndX)
                {
                    bestEndX = h.end;
                }
            }
            if(bestEndX > 0)
            {
                foundX = bestEndX;
            }
        }
    }

    if(foundX.has_value())
    {
        int vertCount{ 0 };
        int maxVertLen{ 0 };
        for(const auto& v : vSegs)
        {
            if(std::abs(v.pos - *foundX) <= 15)
            {
                ++vertCount;
                maxVertLen = std::max(maxVertLen, v.end - v.start);
            }
        }

        if(maxVertLen < 80 && vertCount < 2)
        {
            // Ищем самую правую вертикаль, где есть длинная линия рамки (>= 80 px)
            int bestX{ -1 };
            for(const auto& v : vSegs)
            {
                if((v.end - v.start) >= 80 && v.pos <= searchRight && v.pos > bestX)
                {
                    bestX = v.pos;
                }
            }
            if(bestX > 0)
            {
                foundX = bestX;
            }
        }
    }

    if(!foundX.has_value() || !foundY.has_value())
    {
        return std::nullopt;
    }

    return cv::Point(offsetX + *foundX, offsetY + *foundY);
}



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
        if (upper == 'G' || upper == 'R' || upper == 'F' || upper == '?')
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

    // Поддержка суффиксов вида "/1", "/2" (в имени файла Windows символ '/' заменяется на '-')
    if (rawSuffix.size() == 2 && rawSuffix[0] == '/' &&
        std::isdigit(static_cast<unsigned char>(rawSuffix[1])))
    {
        return std::string{ '_' } + rawSuffix[1];
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
    cv::Rect rect{ cv::boundingRect(box) };

    // Для широких блоков в верхней половине штампа (децимальный номер) даём увеличенный запас справа (36 px),
    // чтобы не обрезать тонкие литеры исполнения ("Г", "А", "Б") или код документа ("СБ") на краю бокса DBNet
    const int padRight{ (rect.width > 220 && rect.y < 280) ? 36 : kCropPadRightPx };

    int x1{ std::max(0, rect.x - kCropPadLeftPx) };
    int y1{ std::max(0, rect.y - kCropPadTopPx) };
    int x2{ std::min(src.cols, rect.x + rect.width + padRight) };
    int y2{ std::min(src.rows, rect.y + rect.height + kCropPadBottomPx) };

    if(x2 <= x1 || y2 <= y1)
    {
        return {};
    }

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

    // Заменяем цифру '3' между кириллическими буквами на 'з' (частая ошибка OCR: "Въе3д" -> "Въезд")
    for(size_t i{ 2 }; i + 2 < s.size(); ++i)
    {
        if(s[i] == '3' &&
            static_cast<unsigned char>(s[i - 2]) >= 0xD0 &&
            static_cast<unsigned char>(s[i + 1]) >= 0xD0)
        {
            s.replace(i, 1, "з");
        }
    }

    trimInPlace(s, ".,:;!?*- |\\/[]\"<>");

    // Отсекаем прилипший к вертикальной линии рамки артефакт ('0', '1') в конце кириллического слова
    while (s.size() > 4 && (s.back() == '0' || s.back() == '1'))
    {
        const unsigned char prevByte{ static_cast<unsigned char>(s[s.size() - 2]) };
        // В UTF-8 второй байт кириллической буквы всегда в диапазоне [0x80..0xBF]
        if (prevByte >= 0x80 && prevByte <= 0xBF)
        {
            s.pop_back();
        }
        else
        {
            break;
        }
    }

    return s;
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
    static const std::regex kBaseNumRegex{ R"((\d{3}\s*\.\s*\d{1,2}\s*\.\s*\d{1,2}\s*\.\s*\d{1,2}\s*\.\s*\d{3}))" };
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
    const size_t spacePos{ tail.find(' ') };
    if(spacePos != std::string::npos)
    {
        tail.erase(spacePos);
    }

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
    cv::resize(crop, resized, cv::Size(targetW, kRecInputHeight), 0, 0, cv::INTER_LINEAR);

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
    // Короткие одиночные заголовки графы «Дата» (проверяем только короткие строки, чтобы не задеть обычные слова)
    if(s.size() <= 10)
    {
        if(s.find("Дата") != std::string::npos  ||
            s.find("Цата") != std::string::npos ||
            s.find("Цота") != std::string::npos ||
            s.find("Лата") != std::string::npos ||
            s.find("Lота") != std::string::npos ||
            s.find("Dота") != std::string::npos ||
            s.find("loma") != std::string::npos ||
            s.find("lomo") != std::string::npos ||
            s.find("Дoma") != std::string::npos ||
            s == "ит" || s == "ата" || s == "Аит" || s == "Лит")
        {
            return true;
        }
    }

    return s.find("чертеж") != std::string::npos ||
           s.find("черпеж") != std::string::npos ||
           s.find("Копиров") != std::string::npos ||
           s.find("Копироб") != std::string::npos ||
           s.find("Копиаов") != std::string::npos ||
           s.find("Конивоб") != std::string::npos ||
           s.find("Формат") != std::string::npos  ||
           s.find("Фопмот") != std::string::npos  ||
           s.find("Сборочн") != std::string::npos ||
           s.find("Соорочн") != std::string::npos ||
           s.find("чертеж") != std::string::npos  ||
           s.find("ГОСТ") != std::string::npos    ||
           s.find("Масса") != std::string::npos   ||
           s.find("Масштаб") != std::string::npos;
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




/**
 * Инициализация выполняется один раз на экземпляр распознавателя.
 * Здесь загружаются две независимые ONNX-модели и словарь символов:
 * det.onnx отвечает за координаты текстовых областей, rec.onnx — за символы.
 * Флаг m_initialized становится true только после успешной подготовки ресурсов.
 */
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




/**
 * Подготовка исходного скана к дальнейшему анализу.
 * Порядок этапов: загрузка серого изображения -> поиск угла штампа ->
 * чтение DPI -> расчёт и вырезание ROI -> перенос координат угла в ROI ->
 * нормализация размера -> улучшение контраста/сглаживание -> перевод в BGR.
 * Возвращаемый bottomRightCorner уже задан относительно bgrMat, а не полного скана.
 */
OnnxStampRecognizer::PreparedStamp OnnxStampRecognizer::prepareStampImage(const std::filesystem::path& imagePath)
{
    const cv::Mat fullGray{ loadGrayImage(imagePath) };
    if(fullGray.empty())
    {
        return {};
    }

    const auto cornerOpt{ findStampBottomRightCorner(fullGray) };
    const auto [scaleX, scaleY]{ readDpiScale(imagePath) };

    constexpr double kCropExpansion{ 1.3 };
    const int cropW{ std::max(1, cvRound(kCropW * scaleX * kCropExpansion)) };
    const int cropH{ std::max(1, cvRound(kCropH * scaleY * kCropExpansion)) };
    const int offsetX{ std::max(0, cvRound(kOffsetX * scaleX)) };
    const int offsetY{ std::max(0, cvRound(kOffsetY * scaleY)) };
    const int padRight{ cvRound(kCornerPadRightPx * scaleX) };
    const int padBottom{ cvRound(kCornerPadBottomPx * scaleY) };

    const int scanW{ fullGray.cols };
    const int scanH{ fullGray.rows };
    int cropX{ std::max(0, scanW - offsetX - cropW) };
    int cropY{ std::max(0, scanH - offsetY - cropH) };

    cv::Point rawCorner{ scanW - offsetX - padRight, scanH - offsetY - padBottom };
    if(cornerOpt.has_value())
    {
        rawCorner = *cornerOpt;
        cropX = std::clamp(cornerOpt->x + padRight - cropW, 0, std::max(0, scanW - cropW));
        cropY = std::clamp(cornerOpt->y + padBottom - cropH, 0, std::max(0, scanH - cropH));
    }
    else
    {
        qWarning().noquote() << "[OpenCV-DNN] Stamp frame corner not found, using fallback crop for:"
                             << QString::fromStdString(imagePath.filename().string());
    }

    const int safeW{ std::min(cropW, scanW - cropX) };
    const int safeH{ std::min(cropH, scanH - cropY) };
    cv::Mat grayCrop{ fullGray(cv::Rect(cropX, cropY, safeW, safeH)).clone() };

    // Переводим координаты правого нижнего угла рамки из полного скана в нормализованный кроп (kCropW x kCropH)
    const float normScaleX{ static_cast<float>(kCropW) / static_cast<float>(std::max(1, safeW)) };
    const float normScaleY{ static_cast<float>(kCropH) / static_cast<float>(std::max(1, safeH)) };
    const cv::Point2f cornerInCrop{
        std::clamp((rawCorner.x - cropX) * normScaleX, 0.0f, static_cast<float>(kCropW - 1)),
        std::clamp((rawCorner.y - cropY) * normScaleY, 0.0f, static_cast<float>(kCropH - 1))
    };

    if(grayCrop.cols != kCropW || grayCrop.rows != kCropH)
    {
        cv::resize(grayCrop, grayCrop, cv::Size(kCropW, kCropH), 0.0, 0.0,
                   (scaleX > 1.0 || scaleY > 1.0) ? cv::INTER_AREA : cv::INTER_CUBIC);
    }

    cv::Scalar meanVal, stdDevVal;
    cv::meanStdDev(grayCrop, meanVal, stdDevVal);

    cv::Mat processed{};
    if(stdDevVal[0] > 75.0)
    {
        cv::medianBlur(grayCrop, processed, 3);
    }
    else
    {
        cv::Ptr<cv::CLAHE> clahe{ cv::createCLAHE(kClaheClipLimit, cv::Size(kClaheGridSize, kClaheGridSize)) };
        clahe->apply(grayCrop, processed);
    }

    cv::Mat bgrMat{};
    cv::cvtColor(processed, bgrMat, cv::COLOR_GRAY2BGR);

    saveStampDebugImage(m_debugDir, imagePath, fullGray,
                        cv::Rect(cropX, cropY, safeW, safeH), cornerOpt);

    return PreparedStamp{ std::move(bgrMat), cornerInCrop };
}




/**
 * Первый нейросетевой проход: DBNet ищет, ГДЕ находится текст.
 * Изображение приводится к размеру входа детектора и нормализуется,
 * затем m_detNet.forward() выдаёт карту вероятностей. findTextBoxes()
 * превращает эту карту в геометрические TextBox. Сами строки здесь не читаются.
 */
std::vector<OnnxStampRecognizer::TextBox> OnnxStampRecognizer::detectTextBoxes(const cv::Mat& bgrMat)
{
    // 1. Приводим штамп к фиксированному размеру 1024x1024 и переводим пиксели из [0..255] в [0.0..1.0]
    cv::Mat rgbMat{};
    cv::cvtColor(bgrMat, rgbMat, cv::COLOR_BGR2RGB);

    cv::Mat detInput{};
    cv::resize(rgbMat, detInput, cv::Size(kDetInputWidth, kDetInputHeight));
    detInput.convertTo(detInput, CV_32FC3, kPixelScale);

    // 2. Применяем стандартизацию ImageNet: (pixel - mean) / std, как при обучении PP-OCRv4
    cv::subtract(detInput, kImageNetMean, detInput);
    cv::divide(detInput, kImageNetStd, detInput);

    // 3. Преобразуем матрицу из формата HWC в 4D-тензор NCHW (1x3x1024x1024) и меняем BGR -> RGB (swapRB = true)
    cv::Mat detBlob{ cv::dnn::blobFromImage(detInput, 1.0, cv::Size(), cv::Scalar(), false, false) };
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




/**
 * Второй нейросетевой этап для уже найденных областей.
 * Для каждого TextBox вырезается изображение строки, после чего recognizeCrop()
 * запускает rec.onnx и декодирует последовательность символов. Результат
 * записывается непосредственно в boxes[i].text; координаты и score сохраняются.
 */
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
                        const cv::Point2f& corner,
                        shared::Document& doc)
{
    cv::Point2f numPos{ 0.0f, kUnsetCoord };

    // Децимальный номер лежит внутри рамки штампа, левее правой ячейки «Лист» (10 мм)
    const float minY{ corner.y - kStampMaxHeightPx };
    const float maxY{ corner.y - kStampBottomEdgeTolPx };
    const float minX{ corner.x - kStampTotalWidthPx };
    const float maxX{ corner.x - kForm2aRightColWidthPx };

    for(const auto& tb : boxes)
    {
        if(tb.text.empty())
        {
            continue;
        }

        const cv::Point2f& topLeft{ tb.box.front() };
        if(topLeft.x < minX || topLeft.x > maxX || topLeft.y < minY || topLeft.y > maxY)
        {
            continue;
        }

        const auto parsedNum{ parseDecimalAndCode(tb.text) };
        if(!parsedNum.number.empty() && topLeft.y > numPos.y)
        {
            doc.number = parsedNum.number;
            doc.code = parsedNum.code;
            numPos = topLeft;
        }
    }

    if(doc.number.empty())
    {
        qWarning().noquote() << "OnnxStampRecognizer::locateDecimalNumber: Decimal number not found.";
        return std::nullopt;
    }

    return numPos;
}




/**
 * Определение формы по вертикальному расстоянию от номера до нижнего угла рамки.
 * Это не отдельное распознавание изображения: метод классифицирует компоновку
 * штампа по уже найденным координатам и выбирает дальнейшие правила извлечения.
 */
OnnxStampRecognizer::StampFormType OnnxStampRecognizer::detectStampForm(float numY, float cornerY)
{
    const float distFromBottom{ cornerY - numY };
    if(distFromBottom <= kForm2aMaxDistFromBottomPx)
    {
        return StampFormType::Form2a;
    }
    if(distFromBottom <= kForm2MaxDistFromBottomPx)
    {
        return StampFormType::Form2;
    }
    return StampFormType::Form1;
}




/**
 * Извлечение графы наименования. Метод рассматривает только боксы в ожидаемой
 * геометрической области под номером, исправляет типовые OCR-ошибки, отбрасывает
 * служебные надписи и склеивает подходящие строки сверху вниз. Для Form2a
 * наименование не извлекается, поскольку в этой форме такой графы нет.
 */
void OnnxStampRecognizer::extractProductName(const std::vector<TextBox>& boxes,
                                             const cv::Point2f& numPos,
                                             const cv::Point2f& corner,
                                             StampFormType formType,
                                             shared::Document& doc)
{
    // На Форме 2а графа наименования по ГОСТ 2.104 отсутствует
    if(formType == StampFormType::Form2a)
    {
        return;
    }

    // Графа наименования ограничена слева рамкой штампа (corner.x - 185 мм),
    // справа — колонкой Лит/Масса/Масштаб (corner.x - 65 мм),
    // сверху — строкой децимального номера, снизу — нижней линией рамки (corner.y)
    const float minX{ numPos.x - kNameLeftFromNumPx };
    const float maxX{ std::min(corner.x - kStampRightColsWidthPx, numPos.x + 300.0f) };
    const float minY{ numPos.y + kNameSearchTopOffsetPx };
    const float maxY{ std::min(numPos.y + kNameMaxHeightFromNumPx, corner.y - kStampBottomEdgeTolPx) };

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
        if(bx < minX || bx > maxX || by <= minY || by >= maxY)
        {
            continue;
        }

        std::string cleanedPart{ cleanRussianName(line) };
        detectDocumentCode(cleanedPart, doc.code);

        const bool hasCyrillic{ cleanedPart.find('\xD0') != std::string::npos ||
                               cleanedPart.find('\xD1') != std::string::npos };


        if(!hasCyrillic                                               ||
            cleanedPart.size() < kMinCleanedNameBytes                 ||
            ((numPos.x - bx) > 220.0f && cleanedPart.size() <= 8)     ||
            isStopWord(cleanedPart))
        {
            continue;
        }

        // Отсекаем короткие 2-буквенные обрывки без гласных из левой колонки (например, "мв", "пт", "кб")
        if(cleanedPart.size() <= 4)
        {
            static constexpr std::array<std::string_view, 20> kCyrillicVowels{
                "а", "е", "ё", "и", "о", "у", "ы", "э", "ю", "я",
                "А", "Е", "Ё", "И", "О", "У", "Ы", "Э", "Ю", "Я"
            };
            const bool hasVowel{ std::any_of(kCyrillicVowels.begin(), kCyrillicVowels.end(),
                                            [&cleanedPart](std::string_view v) {
                                                return cleanedPart.find(v) != std::string::npos;
                                            }) };
            if(!hasVowel)
            {
                continue;
            }
        }

        nameParts.emplace_back(by, std::move(cleanedPart));
    }

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




/**
 * Резервный путь чтения цифры из заранее рассчитанной ячейки.
 * В отличие от обычного прохода, здесь ROI задаётся геометрией штампа/заголовка,
 * а не DBNet-боксом. Из изображения подавляются линии таблицы, после чего
 * фрагмент повторно проходит через ту же rec.onnx и parseSheetNumber().
 */
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

    cv::Mat cellGray{};
    cv::cvtColor(bgrMat(validRoi), cellGray, cv::COLOR_BGR2GRAY);

    // Подавляем длинные горизонтальные и вертикальные линии таблицы внутри ячейки
    cv::Mat inv{};
    cv::bitwise_not(cellGray, inv);
    cv::Mat thresh{};
    cv::threshold(inv, thresh, 128, 255, cv::THRESH_BINARY);

    const int hKernelLen{ std::max(15, validRoi.width / 2) };
    const int vKernelLen{ std::max(15, validRoi.height / 2) };

    cv::Mat hLines{}, vLines{};
    cv::morphologyEx(thresh, hLines, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(hKernelLen, 1)));
    cv::morphologyEx(thresh, vLines, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, vKernelLen)));

    cv::Mat linesMask{};
    cv::bitwise_or(hLines, vLines, linesMask);
    cv::dilate(linesMask, linesMask, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
    cellGray.setTo(255, linesMask);

    cv::Mat cellBgr{};
    cv::cvtColor(cellGray, cellBgr, cv::COLOR_GRAY2BGR);

    cv::Mat paddedCell{};
    cv::copyMakeBorder(cellBgr, paddedCell, 10, 10, 10, 10,
                       cv::BORDER_CONSTANT, cv::Scalar(255, 255, 255));

    const std::string cellText{ recognizeCrop(m_recNet, paddedCell, m_keys) };
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

        std::string currentSheetPart{ tb.text };

        // 1. Сначала ищем «Листов» и отсекаем правую часть строки, если оба заголовка склеились
        for (const auto kw : kTotalSheetsKeywords)
        {
            const size_t pos{ tb.text.find(kw) };
            if (pos != std::string::npos)
            {
                totalHeaderPos = { bx, by };
                const std::string tail{ tb.text.substr(pos + kw.size()) };
                const int inlineTotal{ parseSheetNumber(tail, false) };

                if (inlineTotal > 0 && inlineTotal < kMaxReasonableTotalSheets)
                {
                    doc.totalSheets = inlineTotal;
                    totalFound = true;
                }

                // Оставляем только левую часть до слова «Листов» для поиска «Лист»
                currentSheetPart = tb.text.substr(0, pos);
                break;
            }
        }

        // 2. В оставшейся (или исходной) строке ищем «Лист» и его цифру
        int inlineSheet{ 0 };
        if (!currentSheetPart.empty() &&
            matchKeywordAndExtractTail(currentSheetPart, kCurrentSheetKeywords, inlineSheet))
        {
            sheetHeaderPos = { bx, by };
            if (!sheetFound && inlineSheet > 0 && inlineSheet < kMaxReasonableSheetNumber)
            {
                doc.sheet = inlineSheet;
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




/**
 * Координатор извлечения нумерации листов.
 * Последовательность попыток: найти слова-заголовки «Лист»/«Листов» ->
 * извлечь число из того же или соседнего TextBox -> если значение не найдено,
 * распознать рассчитанную ячейку напрямую. Конкретные ROI зависят от формы штампа.
 */
void OnnxStampRecognizer::extractSheetNumbers(const cv::Mat& bgrMat,
                                              const std::vector<TextBox>& boxes,
                                              const cv::Point2f& corner,
                                              StampFormType formType,
                                              shared::Document& doc)
{
    const bool isForm2a{ formType == StampFormType::Form2a };

    cv::Point2f sheetHeaderPos{ kUnsetCoord, kUnsetCoord };
    cv::Point2f totalHeaderPos{ kUnsetCoord, kUnsetCoord };

    // На Форме 1 и 2 (первый лист) текущий лист по ГОСТ 2.104 всегда равен 1
    bool sheetFound{ !isForm2a };
    bool totalFound{ false };

    // 1. Ищем заголовки «Лист» и «Листов» строго в правой колонке штампа от corner
    const cv::Point2f stampTopLeft{ corner.x - kStampRightColsWidthPx, corner.y - kStampMaxHeightPx };
    findSheetHeaders(boxes, stampTopLeft, sheetHeaderPos, totalHeaderPos, sheetFound, totalFound, doc);

    // 2. Ищем значения в соседних боксах (снизу для Формы 2/2а или справа для Формы 1)
    findSheetValuesInBoxes(boxes, sheetHeaderPos, totalHeaderPos, isForm2a, sheetFound, totalFound, doc);

    recognizeSheetNumbersInCells(bgrMat, corner, formType,
                                 sheetHeaderPos, totalHeaderPos,
                                 sheetFound, totalFound, doc);
}




/**
 * Прямое распознавание ячеек — резерв после анализа текстовых боксов.
 * Здесь вычисляются прямоугольники для цифр с учётом Form1/Form2/Form2a.
 * Каждая область передаётся в recognizeRegionNumber(), который повторно
 * использует rec.onnx. Для Form2a в конце предусмотрено значение по умолчанию.
 */
void OnnxStampRecognizer::recognizeSheetNumbersInCells(const cv::Mat& bgrMat,
                                                       const cv::Point2f& corner,
                                                       StampFormType formType,
                                                       const cv::Point2f& sheetHeaderPos,
                                                       const cv::Point2f& totalHeaderPos,
                                                       bool sheetFound,
                                                       bool totalFound,
                                                       shared::Document& doc)
{
    constexpr int kDefaultSheetNumber{ 1 };
    const bool isForm2a{ formType == StampFormType::Form2a };

    if(!sheetFound)
    {
        if(isForm2a)
        {
            // На Форме 2а ячейка номера листа всегда в самом правом нижнем углу рамки (ширина 10 мм)
            const cv::Rect cornerRoi{
                static_cast<int>(corner.x) - kForm2aSheetCellOffsetX,
                static_cast<int>(corner.y) - kForm2aSheetCellOffsetY,
                kForm2aSheetCellW,
                kForm2aSheetCellH
            };
            const int val{ recognizeRegionNumber(bgrMat, cornerRoi, kMinForm2aCropSizePx, true) };
            if(val > kDefaultSheetNumber && val < kMaxReasonableSheetNumber)
            {
                doc.sheet = val;
                sheetFound = true;
            }
        }
        else if(sheetHeaderPos.x > 0.0f)
        {
            const cv::Rect belowRoi{
                static_cast<int>(sheetHeaderPos.x) - kDirectBelowLeftPadX,
                static_cast<int>(sheetHeaderPos.y) + kDirectBelowShiftY,
                kDirectBelowCropW,
                kDirectBelowCropH
            };
            const int val{ recognizeRegionNumber(bgrMat, belowRoi, kMinValidCropSizePx, false) };
            if(val > 0 && val < kMaxReasonableSheetNumber)
            {
                doc.sheet = val;
                sheetFound = true;
            }
        }
    }

    if(!totalFound && !isForm2a)
    {
        int val{ 0 };
        if(formType == StampFormType::Form1 && totalHeaderPos.x > 0.0f)
        {
            // На Форме 1 цифра общего количества листов стоит справа от слова «Листов»
            const cv::Rect rightRoi{
                static_cast<int>(totalHeaderPos.x) + kDirectTotalRightShiftX,
                static_cast<int>(totalHeaderPos.y) - kDirectRightTopPadY,
                kDirectRightCropW + 25,
                kDirectRightCropH + 10
            };
            val = recognizeRegionNumber(bgrMat, rightRoi, kMinValidCropSizePx, false);
        }

        if(val <= 0 || val >= kMaxReasonableTotalSheets)
        {
            // На Форме 2 (спецификации) или если заголовок не найден — берём ячейку снизу
            const int totalCellX{ (totalHeaderPos.x > 0.0f)
                                     ? (static_cast<int>(totalHeaderPos.x) - kDirectBelowLeftPadX)
                                     : (static_cast<int>(corner.x) - kForm12TotalCellOffsetX) };
            const int totalCellY{ (totalHeaderPos.y > 0.0f)
                                     ? (static_cast<int>(totalHeaderPos.y) + kDirectBelowShiftY)
                                     : (static_cast<int>(corner.y) - kForm2TotalCellOffsetY) };

            const cv::Rect totalRoi{ totalCellX, totalCellY, kForm12CellCropW, kForm12CellCropH };
            val = recognizeRegionNumber(bgrMat, totalRoi, kMinValidCropSizePx, true);
        }

        if(val > 0 && val < kMaxReasonableTotalSheets)
        {
            doc.totalSheets = val;
            totalFound = true;
        }
    }

    if(isForm2a && !sheetFound)
    {
        doc.sheet = kMinFallbackForm2aSheet;
    }
}




/**
 * Главный сценарий одного распознавания. Метод оркестрирует этапы, но делегирует
 * обработку изображения, работу нейросетей и разбор строк специализированным
 * функциям класса. Порядок вызовов ниже является фактическим порядком конвейера.
 */
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

        // Этап 1. Подготовить единый кроп штампа и координаты его правого нижнего угла.
        const auto prepared{ prepareStampImage(imagePath) };
        if(prepared.bgrMat.empty())
        {
            return std::nullopt;
        }

        // Этап 2. DBNet находит только области текста; пустой результат останавливает конвейер.
        auto boxes{ detectTextBoxes(prepared.bgrMat) };
        if(boxes.empty())
        {
            return std::nullopt;
        }

        // Этап 3. rec.onnx читает содержимое каждой области; строки сохраняются в boxes[].text.
        recognizeTextBoxes(prepared.bgrMat, boxes);

        // Этап 4. Создать результат с начальными значениями нумерации листов.
        shared::Document doc{};
        doc.sheet = 1;
        doc.totalSheets = 1;

        // Этап 5. Найти основной децимальный номер и извлечь код документа.
        const auto numPos{ locateDecimalNumber(boxes, prepared.bottomRightCorner, doc) };
        if(!numPos.has_value())
        {
            return std::nullopt;
        }

        // Этап 6. По положению номера определить компоновку основной надписи.
        const StampFormType formType{ detectStampForm(numPos->y, prepared.bottomRightCorner.y) };

        // Этапы 7–8. Извлечь наименование и нумерацию листов с учётом выбранной формы.
        extractProductName(boxes, *numPos, prepared.bottomRightCorner, formType, doc);
        extractSheetNumbers(prepared.bgrMat, boxes, prepared.bottomRightCorner, formType, doc);

        qDebug().noquote()
            << QString("[OpenCV-DNN] PARSED -> Form: %1 | Number: [%2] | Name: [%3] | Sheet: %4/%5")
                   .arg(static_cast<int>(formType))
                   .arg(QString::fromStdString(doc.number))
                   .arg(QString::fromStdString(doc.name))
                   .arg(doc.sheet)
                   .arg(doc.totalSheets);

        // Этап 9. Номер обязателен всегда; наименование обязательно только для Form1/Form2.
        if(doc.number.empty() || (formType != StampFormType::Form2a && doc.name.empty()))
        {
            qWarning().noquote() << "OnnxStampRecognizer::recognize: Failed to parse text.";
            return std::nullopt;
        }

        return doc;
    }
    catch(const std::exception& e)
    {
        std::cerr << "OnnxStampRecognizer::recognize: Error: " << e.what() << "\n" << std::flush;
        return std::nullopt;
    }
}

} // namespace pdm::infrastructure
