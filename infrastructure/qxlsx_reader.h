#ifndef QXLSX_READER_H
#define QXLSX_READER_H

#include <memory>
#include <string>
#include <vector>

#include <xlsxdocument.h>

#include "document.h"
#include "iexcel_reader.h"

namespace pdm::infrastructure
{

    class QxlsxReader final : public pdm::shared::IExcelReader
    {
    private:
        std::unique_ptr<QXlsx::Document> m_xlsx{};

    public:
        QxlsxReader() = default;

        explicit QxlsxReader(const std::string& filePath)
        {
            open(filePath);
        }

        ~QxlsxReader() override = default;

        bool open(const std::string& filePath) override
        {
            m_xlsx = std::make_unique<QXlsx::Document>( QString::fromStdString(filePath) );

            return m_xlsx->load();
        }

        /**
         * @brief Прочитать список документов из открытого файла Excel.
         *
         * @details
         * Метод проходит по строкам листа сверху вниз, начиная с первой строки.
         * Для каждой строки проверяются условия:
         * 1. Если в столбце G (Наименование) встречается "Стандартные изделия",
         *    чтение немедленно прекращается (всё, что ниже — болты и гайки, их не переименовываем).
         * 2. Если строка пустая (нет ни обозначения, ни наименования), она пропускается.
         * 3. Если в столбце V (Примечание) стоит "БЧ", строка пропускается (нет скана).
         * 4. Если в строке есть и Обозначение, и Наименование — она добавляется в список.
         *
         * @note
         * Номера столбцов (C=3, G=7, V=22) жестко зафиксированы под шаблон
         * спецификации A4, утвержденный на предприятии.
         *
         * @return Вектор структур Document с извлеченными данными.
         */
        std::vector<pdm::shared::Document> readDocuments() override
        {
            std::vector<pdm::shared::Document> documents;

            int row{ 1 };

            while(true)
            {
                // Читаем значения из нужных ячеек.
                // Столбцы: C=3 (Обозначение), G=7 (Наименование), V=22 (Примечание)
                QString cellC{ m_xlsx->read(row, 3).toString().trimmed() };
                QString cellG{ m_xlsx->read(row, 7).toString().trimmed() };
                QString cellV{ m_xlsx->read(row, 22).toString().trimmed() };

                // Если достигли раздела "Стандартные изделия" — останавливаемся
                if(cellG.contains("Стандариные изделия", Qt::CaseInsensitive) ||
                    cellG.contains("Прочие изделия", Qt::CaseInsensitive))
                {
                    break;
                }

                // Пропускаем пустые строки и строки-заголовки
                if(cellC.isEmpty() && cellG.isEmpty())
                {
                    ++row;

                    // Защита от бесконечного цикла, если файл закончился
                    if(row > 10'000)
                    {
                        break;
                    }

                    continue;
                }

                // Пропускаем документы без чертежей (БЧ)
                if(cellV.contains("БЧ", Qt::CaseInsensitive))
                {
                    ++row;
                    continue;
                }

                // Если есть и номер, и наименование — это валидный документ
                if(!cellC.isEmpty() && !cellG.isEmpty())
                {
                    pdm::shared::Document doc;
                    doc.number = cellC.toStdString();
                    doc.name = cellG.toStdString();

                    documents.emplace_back(std::move(doc));
                }

                ++row;
            }
            // Читаем ячейку C2 (строка 2, колонка 3 — в QXlsx колонки нумеруются с 1)

            return documents;
        }
    };

}


#endif // QXLSX_READER_H
