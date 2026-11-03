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
