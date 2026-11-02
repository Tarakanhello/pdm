#include "rename.h"
#include "rename_keys.h"

#include <iostream>

namespace pdm::modules
{

    std::string Rename::getFileFromFolder(const std::filesystem::path& folder, const std::string& extension)
    {
        if(folder.empty())
        {
            throw std::runtime_error("[pdm::modulesRename::getFileFromFolder] Error: folder path is empty");
        }
        if(extension.empty())
        {
            throw std::runtime_error("[pdm::modulesRename::getFileFromFolder] Error: extension is empty");
        }
        if(!std::filesystem::exists(folder))
        {
            throw std::runtime_error("[pdm::modulesRename::getFileFromFolder] Error: folder does not exist: " + folder.string());
        }

        std::vector<std::filesystem::path> foundFiles;

        for(const auto& entry : std::filesystem::directory_iterator{ folder })
        {
            if(entry.is_regular_file() && entry.path().extension() == extension)
            {
                foundFiles.push_back(entry.path());
            }
        }

        if(foundFiles.empty())
        {
            throw std::runtime_error("[pdm::modulesRename::getFileFromFolder] Error: none "
                                     + extension
                                     + " files in folder: "
                                     + folder.string());
        }

        if(foundFiles.size() > 1)
        {
            std::string files;
            for(const auto& f : foundFiles)
            {
                files += "  - " + f.filename().string() + '\n';
            }
            throw std::runtime_error("[pdm::modulesRename::getFileFromFolder] Error: found few "
                                     + extension
                                     + " files. Left only one:\n"
                                     + files);
        }

        std::string excelFile{ foundFiles[0].string() };

        return excelFile;
    }




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
            std::string filePath{ getFileFromFolder(params.get(rename::keys::excelFolder), ".xlsx") };

            bool opened{ m_reader->open(filePath) };
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

