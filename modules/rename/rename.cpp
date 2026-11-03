#include "rename.h"
#include "rename_keys.h"

#include <iostream>

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


        try
        {
            // Ищем файл .xlsx в папке
            auto excelFile{ getFilesFromFolder(params.get(rename::keys::excelFolder), ".xlsx") };

            if(excelFile.size() > 1)
            {
                std::string files;
                for(const auto& f : excelFile)
                {
                    files += "  - " + f.filename().string() + '\n';
                }
                throw std::runtime_error("[pdm::modulesRename::execute()] Error: found few files. Left only one:\n" + files);
            }

            bool opened{ m_reader->open(excelFile[0].string()) };
            if(!opened)
            {
                throw std::runtime_error("[RenameModule] Error: can not download file " + excelFile[0].filename().string() + '\n');
            }

            std::cout << "[RenameModule] File succesfully loaded.\n";
        }
        catch(const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            return;
        }

        // Получаем чистые данные, без Qt
        auto documents{ m_reader->readDocuments() };

        std::cout << "Readed documents:\n";
        for(const auto& doc : documents)
        {
            std::cout << "  - " << doc.number << " - " << doc.name << '\n';
        }

        std::cout << "Scan files path: " << params.get(rename::keys::scansFolder) << '\n';
    }

} // end namespcae pdm

