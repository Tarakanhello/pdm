#include "onnx_stamp_recognizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

#include <opencv2/imgproc.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <clipper.hpp>

#include "image.h"

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

std::vector<cv::Point2f> unclipBox(const std::vector<cv::Point2f>& box, float unclipRatio)
{
    double area = std::abs(cv::contourArea(box));
    double length = cv::arcLength(box, true);
    if (length <= 1e-5) return box;

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
    if (solution.empty()) return box;

    std::vector<cv::Point2f> result;
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

    if (x2 <= x1 || y2 <= y1) return cv::Mat();
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
        if (contour.size() < 4) continue;

        cv::RotatedRect rect = cv::minAreaRect(contour);
        if (std::min(rect.size.width, rect.size.height) < 3.0f) continue;

        cv::Rect bounding = cv::boundingRect(contour) & cv::Rect(0, 0, probMap.cols, probMap.rows);
        if (bounding.width <= 0 || bounding.height <= 0) continue;

        cv::Mat mask = cv::Mat::zeros(bounding.size(), CV_8UC1);
        std::vector<cv::Point> shiftedContour;
        for (const auto& pt : contour)
        {
            shiftedContour.emplace_back(pt.x - bounding.x, pt.y - bounding.y);
        }
        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{ shiftedContour }, cv::Scalar(1));
        float meanScore = float(*(cv::mean(probMap(bounding), mask).val));
        if (meanScore < boxScoreThresh) continue;

        std::array<cv::Point2f, 4> vertices{};
        rect.points(vertices.data());
        std::vector<cv::Point2f> boxPts(vertices.begin(), vertices.end());

        auto unclipped = unclipBox(boxPts, unclipRatio);
        if (unclipped.size() < 4) continue;

        cv::RotatedRect unclippedRect = cv::minAreaRect(unclipped);
        if (std::min(unclippedRect.size.width, unclippedRect.size.height) < 5.0f) continue;

        unclippedRect.points(vertices.data());
        std::vector<cv::Point2f> finalBox;
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

std::string cleanRussianName(std::string s)
{
    // Заменяем латинские буквы, похожие по начертанию на кириллицу
    const std::vector<std::pair<std::string, std::string>> replacements = {
        {"A", "А"}, {"B", "В"}, {"C", "С"}, {"E", "Е"}, {"H", "Н"},
        {"K", "К"}, {"M", "М"}, {"O", "О"}, {"P", "Р"}, {"T", "Т"}, {"X", "Х"},
        {"a", "а"}, {"b", "в"}, {"c", "с"}, {"e", "е"}, {"k", "к"},
        {"m", "т"}, {"o", "о"}, {"p", "р"}, {"u", "и"}, {"x", "х"}, {"y", "у"}
    };

    for (const auto& [lat, cyr] : replacements)
    {
        size_t pos = 0;
        while ((pos = s.find(lat, pos)) != std::string::npos)
        {
            s.replace(pos, lat.length(), cyr);
            pos += cyr.length();
        }
    }

    // Убираем мусорные точки, тире и пробелы по краям названия
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

// Исправляем типичные OCR-ошибки в децимальном номере (например, "5:9.30.54. 01.120" -> "519.30.54.01.120")
std::string cleanDecimalCandidate(std::string s)
{
    s = std::regex_replace(s, std::regex(R"(\s*\.\s*)"), ".");
    s = std::regex_replace(s, std::regex(R"(\s+)"), "");
    s = std::regex_replace(s, std::regex(R"(\.:|:\.)"), ".");

    if (std::count(s.begin(), s.end(), '.') >= 3)
    {
        for (char& ch : s)
        {
            if (ch == ':' || ch == 'I' || ch == 'l' || ch == 'T' || ch == '!') ch = '1';
            else if (ch == 'O' || ch == 'U' || ch == 'D' || ch == 'Q') ch = '0';
            else if (ch == '$' || ch == 'S') ch = '5';
            else if (ch == 'B') ch = '8';
        }
    }
    return s;
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
        std::cout << "[OpenCV-DNN] Initializing models from: " << modelsDir << "\n" << std::flush;

        m_detNet = cv::dnn::readNetFromONNX((modelsDir / "det.onnx").string());
        m_clsNet = cv::dnn::readNetFromONNX((modelsDir / "cls.onnx").string());
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

        std::cout << "[OpenCV-DNN] Loaded dictionary keys: " << m_keys.size() << "\n" << std::flush;
        m_initialized = (!m_detNet.empty() && !m_clsNet.empty() && !m_recNet.empty());

        if (m_initialized)
        {
            std::cout << "[OpenCV-DNN] All 3 ONNX models loaded successfully!\n" << std::flush;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[OpenCV-DNN] Init exception: " << e.what() << "\n" << std::flush;
        m_initialized = false;
    }
}

std::optional<shared::Document> OnnxStampRecognizer::
    recognize(const std::filesystem::path& imagePath)
{
    if (!m_initialized)
    {
        std::cerr << "[OpenCV-DNN] ERROR: Recognizer is not initialized.\n" << std::flush;
        return std::nullopt;
    }

    try
    {
        std::cout << "[OpenCV-DNN] 1. Start recognizing: " << imagePath << "\n" << std::flush;

        Image img{ imagePath };
        int cropX{ std::max(0, img.width() - kOffsetX - kCropW) };
        int cropY{ std::max(0, img.height() - kOffsetY - kCropH) };
        Image stamp{ img.getPartOfImage(cropX, cropY, kCropW, kCropH) };

        const int w = stamp.width();
        const int h = stamp.height();
        const int channels = stamp.channels();
        const unsigned char* rawData = stamp.get();

        if (!rawData || w <= 0 || h <= 0) return std::nullopt;

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

        // Повышаем контраст серых сканов
        cv::Mat gray, enhanced;
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

        std::ostringstream fullTextBuilder;
        std::cout << "[OpenCV-DNN] DETECTED BLOCKS (x, y):\n";

        for (auto& tb : boxes)
        {
            cv::Mat crop = getRotateCropImage(bgrMat, tb.box);
            if (crop.empty() || crop.rows <= 0 || crop.cols <= 0) continue;

            constexpr int recH = 48;
            constexpr int maxRecW = 640;
            constexpr int leftPad = 16;
            int targetW = std::min(maxRecW - leftPad, std::max(8, int(std::round(float(crop.cols) * recH / crop.rows))));

            cv::Mat resized;
            cv::resize(crop, resized, cv::Size(targetW, recH), 0, 0, cv::INTER_CUBIC);

            // Белый холст с отступом слева, чтобы не обрезались первые буквы ("Б", "О")
            cv::Mat padded(recH, maxRecW, CV_8UC3, cv::Scalar(255, 255, 255));
            resized.copyTo(padded(cv::Rect(leftPad, 0, targetW, recH)));

            cv::Mat recInput;
            padded.convertTo(recInput, CV_32FC3, 1.0 / 255.0);
            cv::subtract(recInput, cv::Scalar(0.5, 0.5, 0.5), recInput);
            cv::divide(recInput, cv::Scalar(0.5, 0.5, 0.5), recInput);

            cv::Mat recBlob = cv::dnn::blobFromImage(recInput, 1.0, cv::Size(), cv::Scalar(), false, false);
            m_recNet.setInput(recBlob);
            cv::Mat recOut = m_recNet.forward();

            const int seqLen = recOut.size[1];
            const int numClasses = recOut.size[2];
            const float* data = reinterpret_cast<const float*>(recOut.data);

            std::string lineText;
            int prevIdx = 0;
            for (int t = 0; t < seqLen; ++t)
            {
                const float* stepScores = data + t * numClasses;
                int maxIdx = int(std::max_element(stepScores, stepScores + numClasses) - stepScores);

                if (maxIdx > 0 && maxIdx != prevIdx && maxIdx < int(m_keys.size()))
                {
                    lineText += m_keys.at(size_t(maxIdx));
                }
                prevIdx = maxIdx;
            }

            tb.text = lineText;
            if (!lineText.empty())
            {
                std::cout << "  (" << int(tb.box.front().x) << ", " << int(tb.box.front().y)
                << "): " << lineText << "\n";
                fullTextBuilder << lineText << "\n";
            }
        }
        std::cout << "-------------------\n" << std::flush;

        std::string fullText = fullTextBuilder.str();
        if (fullText.empty()) return std::nullopt;

        shared::Document doc;
        std::regex numberRegex(R"(\d{3}\.\d{2}\.\d{2}\.\d{1,2}\.\d{3})");

        // 1. Ищем децимальный номер и запоминаем его координаты
        float numX = 200.0f;
        float numY = 50.0f;
        for (const auto& tb : boxes)
        {
            if (tb.text.empty()) continue;
            std::string cleaned = cleanDecimalCandidate(tb.text);
            std::smatch match;
            if (std::regex_search(cleaned, match, numberRegex))
            {
                doc.number = match.str();
                numX = tb.box.front().x;
                numY = tb.box.front().y;
                break;
            }
        }

        // 2. Ищем наименование изделия строго под номером в том же столбце
        for (const auto& tb : boxes)
        {
            const float bx = tb.box.front().x;
            const float by = tb.box.front().y;
            const std::string& line = tb.text;

            if (line.size() < 4) continue;
            if (std::abs(bx - numX) > 100.0f) continue;
            if (by <= numY + 25.0f) continue;
            if (by > numY + 230.0f) continue;

            if (line.find('\xD0') != std::string::npos || line.find('\xD1') != std::string::npos)
            {
                doc.name = cleanRussianName(line);
                break;
            }
        }

        // Определяем код документа (СБ, ТУ)
        if (fullText.find("СБ") != std::string::npos ||
            fullText.find("C6") != std::string::npos ||
            fullText.find("C5") != std::string::npos ||
            fullText.find("CB") != std::string::npos)
        {
            doc.code = shared::DocumentTypeCode::AssemblyDrawing;
        }
        else if (fullText.find("ТУ") != std::string::npos || fullText.find("TY") != std::string::npos)
        {
            doc.code = shared::DocumentTypeCode::TechnicalSpecifications;
        }

        std::cout << "[OpenCV-DNN] PARSED -> Number: [" << doc.number
                  << "] | Name: [" << doc.name << "]\n" << std::flush;

        if (doc.number.empty() || doc.name.empty())
        {
            std::cerr << "OnnxStampRecognizer::recognize: Failed to parse text.\n" << std::flush;
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
