#ifndef FILES_FROM_FOLDER_H
#define FILES_FROM_FOLDER_H

#include <concepts>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>



namespace pdm::utilities
{

    /**
     * @brief Найти файлы с заданными расширениями в папке.
     *
     * Использует вариативные шаблоны и свёртки (fold expressions) для
     * эффективного поиска файлов сразу по нескольким расширениям.
     *
     * @tparam ARGs Типы аргументов (должны конвертироваться в std::string_view).
     * @param folder Путь к папке для поиска.
     * @param args Одно или несколько расширений (например, ".xlsx" или ".jpg", ".jpeg").
     * @return Вектор путей к найденным файлам.
     * @throws std::runtime_error Если папки не существует или не найдено ни одного файла.
     */
    template<std::convertible_to<std::string_view>... ARGs>
    inline std::vector<std::filesystem::path> getFilesFromFolder(const std::filesystem::path& folder, const ARGs&... args)
    {
        if(folder.empty())
        {
            throw std::runtime_error("[pdm::modules::Rename::getFilesFromFolder] Error: folder path is empty");
        }
        if(sizeof...(args) == 0)
        {
            throw std::runtime_error("[pdm::modules::Rename::getFilesFromFolder] Error: extensions list is empty");
        }
        if(!std::filesystem::exists(folder))
        {
            throw std::runtime_error("[pdm::modules::Rename::getFilesFromFolder] Error: folder does not exist: "
                                     + folder.string());
        }

        std::vector<std::filesystem::path> foundFiles;

        for(const auto& entry : std::filesystem::directory_iterator{ folder })
        {
            if(entry.is_regular_file() && ( (args == entry.path().extension()) || ... ) )
            {
                foundFiles.push_back(entry.path());
            }
        }

        if(foundFiles.empty())
        {
            std::string extList;
            ((extList += std::string(args) + " "), ...);
            throw std::runtime_error("[pdm::modules::Rename::getFilesFromFolder] Error: no files with extensions ["
                                     + extList
                                     + "] in folder: "
                                     + folder.string());
        }

        return foundFiles;
    }

} // end namespace pdm::utilities

#endif // FILES_FROM_FOLDER_H
