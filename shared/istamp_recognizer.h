#ifndef ISTAMP_RECOGNIZER_H
#define ISTAMP_RECOGNIZER_H

#include <filesystem>
#include <optional>

#include "document.h"


namespace pdm::shared
{

    /**
     * @brief Интерфейс для распознавания основной надписи (штампа) на скане.
     */
    class  IStampRecognizer
    {
    public:
        virtual ~IStampRecognizer() = default;

        /**
         * @brief Распознать штамп на изображении.
         * @param imagePath Путь к файлу скана.
         * @return Структура StampData, если удалось распознать, иначе std::nullopt.
         */
        virtual std::optional<Document> recognize(const std::filesystem::path& imagePath) = 0;
    };

} // end namespace pdm::shared

#endif // ISTAMP_RECOGNIZER_H
