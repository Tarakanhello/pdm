#ifndef QXLSX_READER_H
#define QXLSX_READER_H

#include <filesystem>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>

#include <xlsxdocument.h>

#include "document.h"
#include "iexcel_reader.h"

namespace pdm::modules::rename
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

        std::vector<pdm::shared::Document> readDocuments() override
        {
            std::vector<pdm::shared::Document> documents;
            // Читаем ячейку C2 (строка 2, колонка 3 — в QXlsx колонки нумеруются с 1)

            // Читаем ячейку G2 (строка 2, колонка 7)
            QVariant cellValue{ m_xlsx->read(2, 3) };
            // Читаем ячейку G2 (строка 2, колонка 7)
            QVariant cellValue2{ m_xlsx->read(2, 7) };

            shared::Document doc{cellValue.toString().toStdString(), cellValue2.toString().toStdString() };

            documents.push_back(doc);

            return documents;
        }
    };

}


#endif // QXLSX_READER_H
