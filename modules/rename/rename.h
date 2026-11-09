#ifndef RENAME_H
#define RENAME_H

#include <concepts>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "imodule.h"
#include "iexcel_reader.h"
#include "module_id.h"
#include "rename_logger.h"

namespace pdm::modules
{

    /**
     * @brief Модуль переименования отсканированных чертежей.
     *
     * Читает спецификацию, анализирует файлы в папке и переименовывает их
     * в соответствии с обозначениями и наименованиями из спецификации.
     */
    class Rename final : public shared::IModule
    {
    private:
        shared::ModuleId m_id{ shared::ModuleId::Rename };
        std::string m_name{ "Rename scans" };   ///< Имя модуля для отображения.
        std::string m_version{ "0.0.1" };       ///< Текущая версия модуля.

        /**
         * @brief Интерфейс для чтения данных из Excel.
         *
         * Модуль хранит указатель на интерфейс (IExcelReader), а не на
         * конкретную реализацию (например, QxlsxReader).
         * Это обеспечивает слабую связанность: модуль не знает, откуда
         * именно приходят данные (Excel, CSV, база данных), и не зависит
         * от сторонних библиотек (Qt, QXlsx).
         */
        std::unique_ptr<shared::IExcelReader> m_reader;

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
        static std::vector<std::filesystem::path> getFilesFromFolder(const std::filesystem::path& folder, const ARGs&... args)
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



        /**
         * @brief Найти Excel-файл в папке и извлечь из него список документов.
         *
         * @param folder Путь к папке, содержащей файл спецификации.
         * @return Вектор прочитанных документов.
         * @throws std::runtime_error Если файл не найден, их несколько или не удалось открыть.
         */
        std::vector<shared::Document> getDocumentsFromExcel(std::filesystem::path folder);



        /**
         * @brief Найти все отсканированные изображения в папке и отсортировать их.
         *
         * @param folder Путь к папке со сканами.
         * @return Отсортированный вектор путей к найденным изображениям.
         * @throws std::runtime_error Если в папке нет изображений.
         */
        std::vector<std::filesystem::path> getScansPathsFromFolder(std::filesystem::path folder);



        /**
         * @brief Сформировать новое имя файла на основе документа и старого пути.
         *
         * @param doc Документ из спецификации (содержит номер и наименование).
         * @param oldPath Старый путь к файлу (используется для извлечения расширения).
         * @return Новое имя файла (без пути).
         */
        std::string buildNewName(const shared::Document& doc, const std::filesystem::path& oldPath);



        /**
         * @brief Выполнить процесс переименования для всех сопоставленных файлов.
         *
         * @details Создает лог-файл и поочередно вызывает renameOneFile для каждой пары.
         *
         * @param documents Список документов из спецификации.
         * @param scanFiles Список путей к сканам (отсортированный).
         * @param scansFolder Папка со сканами (для сохранения лога).
         */
        void performRename(const std::vector<shared::Document>& documents,
                           const std::vector<std::filesystem::path>& scanFiles,
                           const std::filesystem::path& scansFolder);



        /**
         * @brief Безопасно переименовать один файл.
         *
         * @details Проверяет, не существует ли уже файл с целевым именем.
         * Выполняет переименование через std::filesystem. Логирует результат.
         *
         * @param oldPath Старый путь к файлу.
         * @param newPath Новый путь к файлу.
         * @param logger Ссылка на логгер для записи результата.
         * @return true, если переименование прошло успешно, иначе false.
         */
        bool renameOneFile(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, RenameLogger& logger);


    public:
        /**
         * @brief Конструктор с внедрением зависимости (Dependency Injection).
         *
         * Конкретная реализация ридера передается снаружи (из main.cpp).
         * Это позволяет легко подменять реализации (например, в тестах
         * использовать MockExcelReader) без перекомпиляции самого модуля.
         *
         * @param reader Умный указатель на реализацию интерфейса IExcelReader.
         */
        explicit Rename(std::unique_ptr<shared::IExcelReader> reader)
            : m_reader{ std::move(reader) } {}

        ~Rename() override = default;

        shared::ModuleId getId() const override { return m_id; }
        std::string_view getName() const override { return m_name; }
        std::string_view getVersion() const override { return m_version; }

        /**
         * @brief Запустить процесс переименования.
         * @param params Параметры задачи (должны содержать ключи excelFolder и scansFolder).
         */
        void execute(const shared::JobParams& params) override;
    };

} // end namespace


#endif // RENAME_H
