#ifndef RENAME_LOGGER_H
#define RENAME_LOGGER_H

#include <format>
#include <fstream>
#include <string>
#include <filesystem>

namespace
{
    /**
     * @brief Маркер BOM (Byte Order Mark) для кодировки UTF-8.
     *
     * @details Добавляется в начало файла, чтобы Microsoft Excel
     * и другие программы корректно распознавали кириллицу (русские буквы)
     * при открытии CSV-файла.
     */
    inline constexpr const char* kUtf8Bom{ "\xEF\xBB\xBF" };
}

namespace pdm::modules
{

    /**
     * @brief Простой логгер для записи операций переименования в CSV.
     *
     * @details Открывает файл при создании и закрывает при уничтожении.
     * Записывает строки в формате: `old_name;new_name;status`.
     */
    class RenameLogger final
    {
    private:
        std::ofstream m_file;   ///< Поток для записи в файл.

    public:
        /**
         * @brief Конструктор с открытием файла лога.
         * @param path Путь к файлу лога в виде строки.
         * @throws std::runtime_error Если файл не удалось создать или открыть.
         */
        explicit RenameLogger(const std::string& path)
        {
            open(path);
        }

        /**
         * @brief Конструктор с открытием файла лога (перегрузка для std::filesystem::path).
         * @param path Путь к файлу лога.
         * @throws std::runtime_error Если файл не удалось создать или открыть.
         */
        explicit RenameLogger(const std::filesystem::path& path)
            : RenameLogger{ path.string() }
        {}




        /**
         * @brief Открыть файл лога (перегрузка для std::filesystem::path).
         * @param path Путь к файлу лога.
         */
        void open(const std::filesystem::path& path)
        {
            open(path.string());
        }

        /**
         * @brief Открыть файл лога.
         *
         * @details Открывает файл в режиме перезаписи (trunc), записывает BOM
         * для поддержки UTF-8 и заголовок таблицы.
         *
         * @param path Путь к файлу лога.
         * @throws std::runtime_error Если не удалось создать файл.
         */
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
         *
         * @param oldName Старое имя файла.
         * @param newName Новое имя файла.
         * @param status Статус операции (например, "OK" или "ERROR: ...").
         * @throws std::runtime_error Если файл лога не был открыт.
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



        /**
         * @brief Проверить, открыт ли файл лога.
         * @return true, если файл открыт, иначе false.
         */
        bool isOpen() const
        {
            return m_file.is_open();
        }

    };

} // end namespace pdm::modules

#endif // RENAME_LOGGER_H
