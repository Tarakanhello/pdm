#include "rename.h"

#include <filesystem>
#include <iostream>
#include <vector>

#include "qxlsx_reader.h"
#include "algorithms.h"


namespace pdm::modules
{

    void Rename::execute(const shared::JobParams& params)
    {
        std::cout << "[RenameModule] Module runs and ready to work!\n";

        // Проверяем, передало ли ядро нужные пути

        if(!params.has(rename::keys::excelFolder) || !params.has(rename::keys::scansFolder))
        {
            std::cerr << "[RenameModule] Error: paths ways has not found!\n";
            return;
        }


        rename::QxlsxReader xlsxReader{};

        try
        {
            std::string filePath{ shared::algorithms::getFileFromFolder(params.get(rename::keys::excelFolder), ".xlsx") };
            bool opened{ xlsxReader.open(filePath) };
            if(!opened)
            {
                std::cerr << "[RenameModule] Error: can not download file " << filePath << '\n';
                return;
            }
            std::cout << "[RenameModule] File succesfully loaded.\n";
        }
        catch(const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            return;
        }

        auto documents{ xlsxReader.readDocuments() };

        std::cout << "Readed documents:\n";
        for(const auto& doc : documents)
        {
            std::cout << "  - " << doc.number << " - " << doc.name << '\n';
        }

        std::cout << "Scan files path: " << params.get(rename::keys::scansFolder) << '\n';
    }

} // end namespcae pdm

