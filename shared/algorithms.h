#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>


namespace pdm::shared::algorithms
{


    inline std::string getFileFromFolder(const std::filesystem::path& folder, const std::string& extension)
    {
        if(folder.empty())
        {
            throw std::runtime_error("[shared::algorithms::getFileFromFolder] Error: folder path is empty");
        }
        if(extension.empty())
        {
            throw std::runtime_error("[shared::algorithms::getFileFromFolder] Error: extension is empty");
        }
        if(!std::filesystem::exists(folder))
        {
            throw std::runtime_error("[shared::algorithms::getFileFromFolder] Error: folder does not exist: " + folder.string());
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
            throw std::runtime_error("[shared::algorithms::getFileFromFolder] Error: none "
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
            throw std::runtime_error("[shared::algorithms::getFileFromFolder] Error: found few "
                                     + extension
                                     + " files. Left only one:\n"
                                     + files);
        }

        std::string excelFile{ foundFiles[0].string() };

        std::cout << "[shared::algorithms::getFileFromFolder] Specification file found! Path: " << excelFile << '\n';

        return excelFile;
    }


} // end namespace



#endif // ALGORITHMS_H
