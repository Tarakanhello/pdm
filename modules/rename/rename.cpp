#include "rename.h"
#include "rename_keys.h"

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
            auto scanFiles{ getScansPathsFromFolder(scansFolder) };

            if(documents.size() != scanFiles.size())
            {
                throw std::runtime_error("pdm::modules::Rename::execute:: quantity of scaned files and documents from excel is not the same");
            }

            performRename(documents, scanFiles, scansFolder);
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

        return m_reader->readDocuments();
    }



     std::vector<std::filesystem::path> Rename::getScansPathsFromFolder(std::filesystem::path folder)
    {
        auto scanFiles{ getFilesFromFolder(folder, ".jpg", ".JPG", ".jpeg", ".JPEG") };

        std::ranges::sort(scanFiles);

        return scanFiles;
    }



    std::string Rename::buildNewName(const shared::Document& doc, const std::filesystem::path& oldPath)
    {
        return doc.number + " " + doc.name + oldPath.extension().string();
    }


    void Rename::performRename(const std::vector<shared::Document>& documents,
                               const std::vector<std::filesystem::path>& scanFiles,
                               const std::filesystem::path& scansFolder)
    {
        auto logPath{ scansFolder / "rename_log.csv" };

        RenameLogger logger{ logPath };

        size_t successCount{ 0 };
        size_t errorCount{ 0 };

        for(size_t i{ 0 }; const auto& doc : documents)
        {
            const auto& oldPath{ scanFiles[i] };
            auto newPath{ oldPath.parent_path() / buildNewName(doc, oldPath) };

            if(renameOneFile(oldPath, newPath, logger))
            {
                ++successCount;
            }
            else
            {
                ++errorCount;
            }

            ++i;
        }

        std::cout << "\n=== Summary ===\n";
        std::cout << "Renamed: " << successCount << "\n";
        std::cout << "Errors:  " << errorCount << "\n";
        std::cout << "Log saved to: " << logPath << "\n";
    }

    bool Rename::renameOneFile(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, RenameLogger& logger)
    {
        bool success{ false };

        try
        {
            // Проверяем, не существует ли уже файл с таким именем
            if(std::filesystem::exists(newPath))
            {
                std::string msg{ "Target file already exist" };
                logger.log(oldPath.filename().string(), newPath.filename().string(), "ERROR: " + msg);
            }
            else
            {
                std::filesystem::rename(oldPath, newPath);
                logger.log(oldPath.filename().string(), newPath.filename().string(), "OK");
                success = true;
            }
        }
        catch(const std::filesystem::filesystem_error& e)
        {
            logger.log(oldPath.filename().string(), newPath.filename().string(), std::string{ "ERROR: " } + e.what());
        }

        return success;
    }

} // end namespcae pdm

