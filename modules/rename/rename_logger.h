#ifndef RENAME_LOGGER_H
#define RENAME_LOGGER_H

#include <fstream>
#include <string>
#include <filesystem>

namespace pdm::modules
{

    /**
     * @brief Простой логгер для записи операций переименования в CSV.
     *
     * @details Открывает файл при создании и закрывает при уничтожении.
     * Записывает строки в формате: old_name;new_name
     */
    class RenameLogger final
    {
    private:
        std::ofstream m_file;

    public:
        explicit RenameLogger(const std::string& path)
        {
            open(path);
        }

        explicit RenameLogger(const std::filesystem::path& path)
            : RenameLogger{ path.string() }
        {}

        void open(const std::filesystem::path& path)
        {
            open(path.string());
        }

        void open(const std::string& path)
        {
            m_file.open(path, std::ios::out | std::ios::trunc);

            if(!m_file.is_open())
            {
                throw std::runtime_error("RanameLogger: Error: cannot create log file: " + path);
            }

            m_file << "old;new;status\n";
        }

    };

} // end namespace pdm::modules

#endif // RENAME_LOGGER_H
