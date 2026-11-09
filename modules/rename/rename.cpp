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

            auto logPath{ scansFolder + "/rename_log.csv" };

            RenameLogger logger{ logPath };

            std::cout << "\n=== Executing Rename ===\n";

            size_t successCount{ 0 };
            size_t errorCount{ 0 };


            for(size_t i{ 0 }; const auto& doc : documents)
            {
                const auto& oldPath{ scanFiles[i] };
                std::string oldName{ oldPath.filename().string() };
                std::string newName{ doc.number + " " + doc.name + scanFiles[i].extension().string() };

                auto newPath{ oldPath.parent_path() / newName };

                try
                {
                    // Проверяем, не существует ли уже файл с таким именем
                    if(std::filesystem::exists(newPath))
                    {
                        std::string msg{ "Target file already exist" };
                        std::cerr << "[RenameModule] Warning: " << msg << " -> " << newName << '\n';
                        logger.log(oldName, newName, "ERROR: " + msg);
                        ++errorCount;
                    }
                    else
                    {
                        std::filesystem::rename(oldPath, newPath);
                        std::cout << "  OK: " << oldName << " -> " << newName << '\n';
                        logger.log(oldName, newName, "OK");
                        ++successCount;
                    }
                }
                catch(const std::filesystem::filesystem_error& e)
                {
                    std::cerr << "[RenameModule] Filesystem error on " << oldName << ": " << e.what() << '\n';
                    logger.log(oldName, newName, std::string{ "ERROR: " } + e.what());
                    ++errorCount;
                }

                ++i;
            }


            std::cout << "\n=== Summary ===\n";
            std::cout << "Renamed: " << successCount << "\n";
            std::cout << "Errors:  " << errorCount << "\n";
            std::cout << "Log saved to: " << logPath << "\n";
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

