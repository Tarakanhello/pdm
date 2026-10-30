#ifndef IEXCEL_READER_H
#define IEXCEL_READER_H

#include <string>
#include <vector>

#include "document.h"

namespace  pdm::shared
{
    /**
     * @brief Интерфейс для чтения документов из файла Excel.
     *
     * Позволяет изолировать бизнес-логику от конкретной библиотеки (QXlsx).
     * Если завтра мы захотим перейти на OpenXLSX или читать CSV,
     * мы просто создадим новый класс, реализующий этот интерфейс.
     */
    class IExcelReader
    {
    public:
        virtual ~IExcelReader() = default;

        /**
         * @brief Открыть файл Excel.
         * @param filePath Путь к файлу.
         * @return true, если файл успешно открыт и готов к чтению, иначе false.
         */
        virtual bool open(const std::string& filePath) = 0;

        /**
         * @brief Прочитать список документов из открытого файла.
         * @return Вектор структур Document, содержащих извлеченные данные.
         */
        virtual std::vector<Document> readDocuments() = 0;
    };
} // end name space

#endif // IEXCEL_READER_H
