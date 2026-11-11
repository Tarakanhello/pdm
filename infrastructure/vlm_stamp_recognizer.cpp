#include "vlm_stamp_recognizer.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
#include <memory>
#include <span>
#include <string_view>

#include <httplib.h>
#include <nlohmann/json.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>


namespace pdm::infrastructure
{

    // ===================================================================
    // ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ (в анонимном пространстве имен, чтобы не засорять линковку)
    // ===================================================================
    namespace
    {

        constexpr float kMaxImageDimension{ 1024.0f };
        constexpr int kJpegQuality{ 90 };

        struct StbiImageDeleter
        {
            void operator()(unsigned char* img) const
            {
                if(img)
                {
                    stbi_image_free(img);
                }
            }
        };

        using UniqueStbiImage = std::unique_ptr<unsigned char, StbiImageDeleter>;

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



        /**
         * @brief Callback для stb_image_write, чтобы сохранить JPEG в память (в vector), а не в файл.
         */
        void writeToVector(void* context, void* data, int size)
        {
            auto* vec{ static_cast<std::vector<unsigned char>*>(context) };
            auto* bytes{ static_cast<unsigned char*>(data) };
            vec->insert(vec->end(), bytes, bytes + size);
        }

    } // end namespace


VLMStampRecognizer::VLMStampRecognizer(const std::string& serverUrl)
    : m_serverUrl{ serverUrl }
{}


std::string VLMStampRecognizer::encodeToBase64(const std::filesystem::path& imagePath)
{
    int width{};
    int height{};
    int channels{};

    UniqueStbiImage img{ stbi_load(imagePath.string().c_str(), &width, &height, &channels, 0) };

    if(!img) return "";

    // --- ТОЧНЫЙ КРОП (Только штамп) ---
    // Берем 35% ширины справа и 15% высоты снизу.
    int cropW = static_cast<int>(width * 0.35f);
    int cropH = static_cast<int>(height * 0.15f);
    int startX = width - cropW;
    int startY = height - cropH;

    std::vector<unsigned char> croppedImg(cropW * cropH * channels);

    for (int y = 0; y < cropH; ++y)
    {
        for (int x = 0; x < cropW; ++x)
        {
            for (int c = 0; c < channels; ++c)
            {
                croppedImg[(y * cropW + x) * channels + c] =
                    img.get()[((startY + y) * width + (startX + x)) * channels + c];
            }
        }
    }

    // Увеличили лимит до 2048, чтобы текст в штампе был крупным и четким
    int newWidth = cropW;
    int newHeight = cropH;
    const float maxSize = 2048.0f;

    if (cropW > maxSize || cropH > maxSize)
    {
        float scale = std::min(maxSize / cropW, maxSize / cropH);
        newWidth = static_cast<int>(cropW * scale);
        newHeight = static_cast<int>(cropH * scale);
    }

    std::vector<unsigned char> resizedImg(newWidth * newHeight * channels);
    auto layout = (channels == 4) ? STBIR_RGBA : STBIR_RGB;

    stbir_resize_uint8_linear(croppedImg.data(), cropW, cropH, 0,
                              resizedImg.data(), newWidth, newHeight, 0, layout);

    std::vector<unsigned char> jpegBuffer{};
    stbi_write_jpg_to_func(writeToVector, &jpegBuffer, newWidth, newHeight, channels, resizedImg.data(), kJpegQuality);

    return base64Encode(jpegBuffer);
}




std::optional<shared::Document> VLMStampRecognizer::recognize([[maybe_unused]] const std::filesystem::path& imagePath)
{
    std::string base64Image{ encodeToBase64(imagePath) };
    if(base64Image.empty())
    {
        return std::nullopt;
    }

    std::string prompt =
        "Ты — OCR-система. Твоя задача — прочитать текст в штампе на картинке. "
        "Верни ТОЛЬКО JSON без markdown-разметки.\n"
        "{\n"
        "  \"number\": \"строка (только номер, без кода)\",\n"
        "  \"code\": \"строка (СБ, ТУ и т.д. Если нет, то пусто)\",\n"
        "  \"name\": \"строка (наименование)\",\n"
        "  \"sheet\": число,\n"
        "  \"totalSheets\": число\n"
        "}\n"
        "ВАЖНО: Не придумывай данные! Читай только то, что видишь. Если не видишь — оставь пустую строку.";

    // 3. Собираем JSON для отправки (формат OpenAI API, который понимает llama.cpp)
    nlohmann::json requestBody{
        {"model", "gpt-3.5-turbo"}, // Сервер игнорирует это поле, но требует его наличия
        {"temperature", 0.1},       // 0.1 делает модель максимально точной и убирает "фантазии"
        {"max_tokens", 512},
        {"messages", nlohmann::json::array({
            {
                {"role", "user"},
                {"content", nlohmann::json::array(
                    {
                    {{"type", "text"}, {"text", prompt}},
                    {{"type", "image_url"}, {"image_url", {{"url", "data:image/jpeg;base64," + base64Image}}}}
                    }
                    )
                }
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
        doc.number = docJson.value("number", "");
        doc.name = docJson.value("name", "");

        // Безопасное получение целых чисел (ИИ может вернуть их как строку)
        auto getSafeInt = [](const nlohmann::json& j, const std::string& key, int defaultVal) {
            if (!j.contains(key)) return defaultVal;
            if (j[key].is_number()) return j[key].get<int>();
            if (j[key].is_string()) {
                try { return std::stoi(j[key].get<std::string>()); }
                catch (...) { return defaultVal; }
            }
            return defaultVal;
        };

        doc.sheet = getSafeInt(docJson, "sheet", 1);
        doc.totalSheets = getSafeInt(docJson, "totalSheets", 1);

        if (doc.number.empty() || doc.name.empty())
        {
            std::cerr << "[VLM] Error: AI returned empty number or name.\n";
            return std::nullopt;
        }

        // 9. Маппинг строкового кода (СБ, ТУ) в наш enum DocumentTypeCode
        std::string codeStr = docJson.value("code", "");

        // Приводим к верхнему регистру на всякий случай (Сб, сб, СБ -> СБ)
        for (char& c : codeStr) c = std::toupper(static_cast<unsigned char>(c));

        if (codeStr == "СБ") doc.code = shared::DocumentTypeCode::AssemblyDrawing;
        else if (codeStr == "ТУ") doc.code = shared::DocumentTypeCode::TechnicalSpecifications;
        else if (codeStr == "ВП") doc.code = shared::DocumentTypeCode::PurchasedPartsList;
        else doc.code = shared::DocumentTypeCode::Unknown;

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
