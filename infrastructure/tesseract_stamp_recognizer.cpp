#include "tesseract_stamp_recognizer.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <regex>

#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

#include "image.h"

namespace
{

/// @brief Отступ от правого края изображения (в пикселях).
constexpr int kOffsetX{ 30 };

/// @brief Отступ от нижнего края изображения (в пикселях)
constexpr int kOffsetY{ 90 }; // Отступаем 90 пикселей от нижнего края

/// @brief Ширина вырезаемой области штампа (в пикселях).
constexpr int kCropW{ 1050 };

/// @brief Высота вырезаемой области штампа (в пикселях).
constexpr int kCropH{ 500 };

}




namespace pdm::infrastructure
{

    TesseractStampRecognizer::TesseractStampRecognizer(const std::string& tessdataPath)
        : m_tessdataPath{ tessdataPath } {}

    std::optional<shared::Document> TesseractStampRecognizer::
        recognize(const std::filesystem::path& imagePath)
    {
        try
        {
            std::cout << "[Tesseract] 1. Start recognizing: " << imagePath << "\n";

            // 1. Вырезаем правый нижний угол
            Image img{ imagePath };
            std::cout << "[Tesseract] 2. Image loaded. Size: " << img.width() << "x" << img.height() << "\n";

            int cropX{ img.width() - kOffsetX - kCropW };
            int cropY{ img.height() - kOffsetY - kCropH };
            Image stamp{ img.getPartOfImage(cropX, cropY, kCropW, kCropH) };
            std::cout << "[Tesseract] 3. Stamp cropped. Size: " << stamp.width() << "x" << stamp.height() << "\n";

            // 2. Инициализируем Tesseract
            std::cout << "[Tesseract] 4. Initializing Tesseract with path: " << m_tessdataPath << "\n";
            std::unique_ptr<tesseract::TessBaseAPI> TessAPI{ std::make_unique<tesseract::TessBaseAPI>() };

            if(TessAPI->Init(m_tessdataPath.c_str(), "rus"))
            {
                std::cerr << "[Tesseract] ERROR: Could not initialize tesseract. Check tessdata path!\n";
                return std::nullopt;
            }
            std::cout << "[Tesseract] 5. Tesseract initialized successfully.\n";

            TessAPI->SetVariable("dotproduct", "generic");

            // 3. Передаем картинку
            std::cout << "[Tesseract] 6. Setting image to Tesseract...\n";
            // Принудительно создаем 32-битный буфер (RGBA), чтобы избежать багов Tesseract с 24-бит
            const int w = stamp.width();
            const int h = stamp.height();
            const int srcChannels = stamp.channels();
            const int dstChannels = 4; // RGBA

            std::vector<unsigned char> rgbaBuffer(static_cast<size_t>(w) * h * dstChannels, 255); // Заполняем альфу 255

            const unsigned char* src = stamp.get();
            for (int i = 0; i < w * h; ++i) {
                // Копируем RGB
                rgbaBuffer[i * 4 + 0] = src[i * srcChannels + 0]; // R
                rgbaBuffer[i * 4 + 1] = src[i * srcChannels + 1]; // G
                rgbaBuffer[i * 4 + 2] = src[i * srcChannels + 2]; // B
                // Альфа уже 255
            }

            // Передаем RGBA буфер
            TessAPI->SetImage(rgbaBuffer.data(), w, h, dstChannels, w * dstChannels);

            std::cout << "[Tesseract] 7. Image set.\n";

            // 4. Получаем текст
            std::cout << "[Tesseract] 8. Getting UTF8 text...\n";
            std::unique_ptr<char[]> outText{ TessAPI->GetUTF8Text() };
            std::string text{ outText.get() ? outText.get() : "" };
            TessAPI->End();
            std::cout << "[Tesseract] 9. Text received. Length: " << text.length() << "\n";

            if(text.empty())
            {
                std::cerr << "[Tesseract] WARNING: Text is empty.\n";
                return std::nullopt;
            }

            std::cout << "[Tesseract] RAW TEXT:\n" << text << "\n-------------------\n";

            // 5. Парсим текст с помощью регулярных выражений
            shared::Document doc;

            // Ищем номер (например, 519.30.54.01.120 или сб065)
            std::regex numberRegex(R"((\d{3}\.\d{2}\.\d{2}\.\d{2}\.\d{3})|([а-яА-Я]{2,4}\d{3}))");
            std::smatch match;
            if(std::regex_search(text, match, numberRegex))
            {
                doc.number = match.str();
            }

            // Ищем наименование (строки, где есть русские буквы, но нет "Лист", "Формат" и т.д.)
            std::istringstream stream(text);
            std::string line;
            while(std::getline(stream, line))
            {
                // Убираем пробелы по краям
                line.erase(0, line.find_first_not_of(" \t\r\n"));
                line.erase(line.find_last_not_of(" \t\r\n") + 1);

                // Если строка не пустая и содержит буквы, и это не служебные слова
                if (!line.empty() &&
                    line.find_first_of("абвгдеёжзийклмнопрстуфхцчшщъыьэюяАБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ")
                                         != std::string::npos)
                {
                    if (line.find("Лист") == std::string::npos &&
                        line.find("Формат") == std::string::npos &&
                        line.find("Масса") == std::string::npos)
                    {
                        if (doc.name.empty())
                        {
                            doc.name = line;
                        }
                    }
                }
            }

            // Определяем код (СБ, ТУ и т.д.)
            if(text.find("СБ") != std::string::npos)
            {
                doc.code = shared::DocumentTypeCode::AssemblyDrawing;
            }
            else if(text.find("ТУ") != std::string::npos)
            {
                doc.code = shared::DocumentTypeCode::TechnicalSpecifications;
            }

            // 6. Проверки
            if (doc.number.empty() || doc.name.empty())
            {
                std::cerr << "TesseractStampRecognizer::recognize: Failed to parse text.\n";
                return std::nullopt;
            }

            return doc;
        }
        catch (const std::exception& e)
        {
            std::cerr << "TesseractStampRecognizer::recognize: Error: " << e.what() << "\n";
            return std::nullopt;
        }
    }

}
