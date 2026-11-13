#ifndef VLM_STAMP_RECOGNIZER_H
#define VLM_STAMP_RECOGNIZER_H

#include <filesystem>
#include <optional>
#include <string>

#include "document.h"
#include "istamp_recognizer.h"


namespace pdm::infrastructure
{

    /**
     * @brief Распознаватель штампа, использующий локальную VLM (Vision-Language Model).
     *
     * @details
     * Этот класс реализует интерфейс shared::IStampRecognizer. Он не содержит
     * тяжелых вычислений сам по себе, а выступает в роли "клиента".
     * Он кодирует изображение в Base64, отправляет HTTP POST-запрос на локальный
     * сервер (например, llama.cpp) и парсит полученный JSON-ответ.
     *
     * Благодаря такому подходу (клиент-сервер), наше приложение остается легким,
     * не зависит от тяжелых библиотек ИИ и может быть легко перенесено на
     * headless-сервер (без графического интерфейса).
     */
    class VLMStampRecognizer final : public shared::IStampRecognizer
    {
    private:
        /**
         * @brief URL-адрес локального сервера (например, "http://127.0.0.1:8080").
         */
        std::string m_serverUrl;

        /**
         * @brief Вспомогательный метод: читает изображение, уменьшает его и кодирует в Base64.
         *
         * @details Использует библиотеку stb для ресайза и сжатия в JPEG, чтобы
         * не перегружать контекст нейросети гигантскими сканами.
         *
         * @param imagePath Путь к файлу скана.
         * @return Строка в формате Base64 или пустая строка в случае ошибки.
         */
        static std::string encodeToBase64(const std::filesystem::path& imagePath);

    public:
        /**
         * @brief Конструктор.
         *
         * @param serverUrl Адрес, по которому запущен llama-server.
         */
        explicit VLMStampRecognizer(const std::string& serverUrl);

        ~VLMStampRecognizer() override = default;

        /**
         * @brief Распознать штамп на изображении с помощью нейросети.
         *
         * @param imagePath Путь к файлу скана.
         * @return Заполненная структура Document, если распознавание прошло успешно,
         * иначе std::nullopt.
         */
        std::optional<shared::Document> recognize(const std::filesystem::path& imagePath) override;
    };

} // end namespace pdm::infrastructure

#endif // VLM_STAMP_RECOGNIZER_H
