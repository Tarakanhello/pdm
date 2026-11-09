#ifndef RENAME_LOGGER_H
#define RENAME_LOGGER_H

#include <format>
#include <fstream>
#include <string>
#include <filesystem>

namespace
{
    inline constexpr const char* kUtf8Bom{ "\xEF\xBB\xBF" };
}

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



        /**
         * @brief Открыть файл лога.
         * @param path Путь к файлу (например, "rename_log.csv").
         */
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

            m_file << kUtf8Bom;

            m_file << "old;new;status\n";
        }


        /**
         * @brief Записать операцию в лог.
         * @param oldName Старое имя.
         * @param newName Новое имя.
         * @param status Статус (OK или ERROR).
         */
        void log(const std::string& oldName, const std::string& newName, const std::string& status)
        {
            if(!m_file.is_open())
            {
                throw std::runtime_error("RanameLogger::Log: Error: Lof file is not open");
            }

            m_file << std::format("{};{};{}\n", oldName, newName, status);
            m_file.flush(); // Принудительно сбрасываем буфер на диск, чтобы не потерять данные при сбое
        }

        bool isOpen() const
        {
            return m_file.is_open();
        }

    };

} // end namespace pdm::modules

#endif // RENAME_LOGGER_H
