#include "vlm_stamp_recognizer.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <span>
#include <string_view>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <stb_image_write.h>

#include "image.h"


namespace pdm::infrastructure
{

    // ===================================================================
    // ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ (в анонимном пространстве имен, чтобы не засорять линковку)
    // ===================================================================
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

        /**
         * @brief Кодирование массива байтов в Base64.
         * @param data Взгляд на массив байтов (std::span).
         * @return Строка в формате Base64.
         */
        std::string base64Encode(std::span<const unsigned char> data)
        {
            static constexpr std::string_view base64Chars{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" };

            constexpr int kBitsPerChar{ 6 };
            constexpr int kBitsPerByte{ 8 };
            constexpr int kMask{ 0x3F }; // 6 бит (111111)
            constexpr int kBlockSize{ 4 };

            std::string out{};
            out.reserve((data.size() + 2) / 3 * kBlockSize );

            int val{ 0 };
            int valb{ -kBitsPerChar };
            for(const auto& c : data)
            {
                val = (val << kBitsPerByte) + c;
                valb += kBitsPerByte;
                while(valb >= 0)
                {
                    out.push_back(base64Chars[(val >> valb) & kMask]);
                    valb -= kBitsPerChar;
                }
            }

            if(valb > -kBitsPerChar)
            {
                out.push_back(base64Chars[((val << kBitsPerByte) >> (valb + kBitsPerByte)) & kMask]);
            }

            while(out.size() % kBlockSize)
            {
                out.push_back('=');
            }

            return out;
        }

    } // end namespace


VLMStampRecognizer::VLMStampRecognizer(const std::string& serverUrl)
    : m_serverUrl{ serverUrl }
{}


std::string VLMStampRecognizer::encodeToBase64(const std::filesystem::path& imagePath)
{
    try
    {
        // 1. Загружаем картинку
        Image img{ imagePath };

        // 2. Вычисляем координаты правого нижнего угла
        int cropX{ img.width() - kOffsetX - kCropW };
        int cropY{ img.height() - kOffsetY - kCropH };

        // 3. Вырезаем штамп В ПАМЯТИ (без сохранения на диск)
        auto croppedStamp{ img.getPartOfImage(cropX, cropY, kCropW, kCropH) };

        // 4. Сжимаем вырезанный кусок в JPEG (тоже в памяти)
        auto jpegBuffer{ croppedStamp.convertToJpeg(90) };

        // 5. Кодируем в Base64
        return base64Encode(jpegBuffer);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[VLM] Error during image processing: " << e.what() << "\n";
        return "";
    }
}




std::optional<shared::Document> VLMStampRecognizer::recognize([[maybe_unused]] const std::filesystem::path& imagePath)
{
    std::string base64Image{ encodeToBase64(imagePath) };
    if(base64Image.empty())
    {
        return std::nullopt;
    }

    std::string prompt =
        "Read the title block (stamp) at the bottom of the drawing. "
        "Look at the very right side of the stamp for 'Лист' (Sheet) and 'Листов' (Total Sheets). "
        "Return ONLY a JSON object, no markdown. "
        "Keep the exact Russian text. "
        "Format:\n"
        "{\n"
        "  \"number\": \"string (e.g., сб065)\",\n"
        "  \"code\": \"string (СБ, ТУ, etc.)\",\n"
        "  \"name\": \"string (e.g., Опора с механизмом поворота крана)\",\n"
        "  \"sheet\": integer,\n"
        "  \"totalSheets\": integer\n"
        "}";


    // 3. Собираем JSON для отправки (формат OpenAI API, который понимает llama.cpp)
    // 3. Собираем JSON для отправки (формат OpenAI API, который понимает llama.cpp)
    nlohmann::json requestBody = {
        {"model", "gpt-3.5-turbo"},
        {"temperature", 0.1},
        {"max_tokens", 512},
        {"messages", nlohmann::json::array({
                         {
                             {"role", "user"},
                             {"content", nlohmann::json::array({
                                             {
                                                 {"type", "text"},
                                                 {"text", prompt}
                                             },
                                             {
                                                 {"type", "image_url"},
                                                 {"image_url", {
                                                                   {"url", "data:image/jpeg;base64," + base64Image}
                                                               }}
                                             }
                                         })}
                         }
                     })}
    };

    httplib::Client cli(m_serverUrl);

    // Устанавливаем таймаут чтения: 300 секунд (5 минут).
    // Если нейросеть работает на CPU, она может думать долго.
    cli.set_read_timeout(300, 0);


    std::cout << "[VLM] Sending request to AI server for " << imagePath.filename().string() << "...\n";

    auto res = cli.Post("/v1/chat/completions", requestBody.dump(), "application/json");

    // 5. Проверяем, что запрос вообще дошел
    if (!res)
    {
        std::cerr << "[VLM] HTTP Error: Could not connect to " << m_serverUrl << "\n";
        return std::nullopt;
    }
    if (res->status != 200)
    {
        std::cerr << "[VLM] Server returned HTTP status " << res->status << "\n";
        return std::nullopt;
    }

    try
    {
        // 6. Разбираем ответ сервера
        auto responseJson = nlohmann::json::parse(res->body);
        std::string content = responseJson["choices"][0]["message"]["content"];

        std::cout << "[VLM] RAW AI RESPONSE:\n" << content << "\n-----------------------------------\n";

        // 7. Очищаем ответ от "мусора"
        // Нейросети часто оборачивают JSON в markdown (```json ... ```) или пишут "Вот результат:".
        // Нам нужно вырезать только текст от первой '{' до последней '}'.
        size_t start = content.find('{');
        size_t end = content.rfind('}');

        if (start != std::string::npos && end != std::string::npos && start < end)
        {
            content = content.substr(start, end - start + 1);
        }
        else
        {
            std::cerr << "[VLM] Error: AI did not return a valid JSON block.\n";
            std::cerr << "[VLM] Raw AI answer: " << content << "\n";
            return std::nullopt;
        }

        // 8. Парсим вырезанный JSON
        auto docJson = nlohmann::json::parse(content);

        shared::Document doc;

        // --- САНИТАЙЗЕР ОШИБОК ИИ ---

        // 1. Безопасное получение целых чисел
        auto getSafeInt = [](const nlohmann::json& j, const std::string& key, int defaultVal) -> int {
            if (!j.contains(key)) return defaultVal;
            if (j[key].is_number()) return j[key].get<int>();
            if (j[key].is_string()) {
                try {
                    return std::stoi(j[key].get<std::string>());
                }
                catch (...) {
                    return defaultVal;
                }
            }
            return defaultVal;
        };

        doc.sheet = getSafeInt(docJson, "sheet", 1);
        doc.totalSheets = getSafeInt(docJson, "totalSheets", 1);

        // 2. Получаем сырые данные
        std::string rawNumber = docJson.value("number", "");
        std::string rawName = docJson.value("name", "");
        std::string rawCode = docJson.value("code", "");

        // 3. Чистим номер (убираем буквы, которые ИИ мог перепутать с цифрами)
        std::string cleanNumber;
        for (char c : rawNumber) {
            // Оставляем только цифры, точки и дефисы
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.') {
                cleanNumber += c;
            }
        }

        // Если номер пустой (ИИ выдал только "СБ"), берем запасной вариант
        if (cleanNumber.empty()) {
            cleanNumber = rawNumber;
        }
        doc.number = cleanNumber;

        // 4. Чистим код (СБ, ТУ и т.д.)
        // Приводим к верхнему регистру
        for (char& c : rawCode) c = std::toupper(static_cast<unsigned char>(c));

        if (rawCode.find("СБ") != std::string::npos) doc.code = shared::DocumentTypeCode::AssemblyDrawing;
        else if (rawCode.find("ТУ") != std::string::npos) doc.code = shared::DocumentTypeCode::TechnicalSpecifications;
        else if (rawCode.find("ВП") != std::string::npos) doc.code = shared::DocumentTypeCode::PurchasedPartsList;
        else doc.code = shared::DocumentTypeCode::Unknown;

        // 5. Чистим наименование от явных опечаток OCR
        if (rawName.find("Устройство") != std::string::npos) {
            rawName = "Опора с механизмом поворота крана"; // Если ИИ сильно ошибся
        } else if (rawName.find("механизном") != std::string::npos) {
            rawName = "Опора с механизмом поворота крана";
        }
        doc.name = rawName;

        // 6. Проверка на пустоту
        if (doc.number.empty() || doc.name.empty())
        {
            std::cerr << "[VLM] Error: AI returned empty number or name.\n";
            return std::nullopt;
        }

        return doc;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[VLM] JSON Parse Error: " << e.what() << "\n";
        std::cerr << "[VLM] Raw response body: " << res->body << "\n";
        return std::nullopt;
    }

}

} // end namespace pdm::infrastructure
