#ifndef IEXCEL_READER_H
#define IEXCEL_READER_H

#include <string>
#include <vector>

#include "document.h"

namespace  pdm::shared
{
    class IExcelReader
    {
    public:
        virtual ~IExcelReader() = default;

        virtual bool open(const std::string& filePath) = 0;

        virtual std::vector<Document> readDocuments() = 0;
    };
} // end name space

#endif // IEXCEL_READER_H
