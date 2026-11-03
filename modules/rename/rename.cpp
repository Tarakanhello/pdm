#include "rename.h"
#include "rename_keys.h"
#include "rename_logger.h"

#include <algorithm>
#include <format>
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

        const auto& excelFolder{ params.get(rename::keys::excelFolder) };
        const auto& scansFolder{ params.get(rename::keys::scansFolder) };

        try
        {
            auto documents{ getDocumentsFromExcel(excelFolder) };

            std::cout << "Readed documents:\n";

            for(const auto& doc : documents)
            {
                std::cout << "  - " << doc.number << " - " << doc.name << '\n';
            }

            auto scanFiles{ getScansPathsFromFolder(scansFolder) };

            if(scanFiles.size() != documents.size())
            {
                throw std::runtime_error("pdm::modules::Rename::execute:: quantity of scaned files and documents from excel is not the same");
            }

            RenameLogger logger{ scansFolder + "/rename_log.csv" };

            std::cout << "Files after rename:\n";

            std::cout << "\n=== Rename Plan ===\n";
            for(size_t i{ 0 }; const auto& doc : documents)
            {
                std::string newName{ doc.number + " " + doc.name + scanFiles[i].extension().string() };

                std::cout << std::format("  - {} -> {}\n", scanFiles[i++].filename().string(), newName);
            }
        }
        catch(const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            return;
        }


        std::cout << "[RenameModule] Module end work!\n";
    }



    std::vector<shared::Document> Rename::getDocumentsFromExcel(std::filesystem::path folder)
    {
        // Ищем файл .xlsx в папке
        auto excelFile{ getFilesFromFolder(folder, ".xlsx") };

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

        return m_reader->readDocuments();
    }



     std::vector<std::filesystem::path> Rename::getScansPathsFromFolder(std::filesystem::path folder)
    {
        auto scanFiles{ getFilesFromFolder(folder, ".jpg", ".JPG", ".jpeg", ".JPEG") };

        std::ranges::sort(scanFiles);

        return scanFiles;
    }

} // end namespcae pdm

