#ifndef RENAME_H
#define RENAME_H

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

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
         * @brief Найти единственный файл с заданным расширением в папке.
         *
         * Проверяет, что папка существует. Ищет файлы с указанным расширением.
         * Если файлов ноль или больше одного — выбрасывает исключение (защита от неоднозначности).
         *
         * @param folder Путь к папке для поиска.
         * @param extension Искомое расширение (например, ".xlsx").
         * @return Полный путь к найденному файлу.
         * @throws std::runtime_error Если папки нет, или файлов не найдено, или их больше одного.
         */
        static std::string getFileFromFolder(const std::filesystem::path& folder, const std::string& extension);

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
