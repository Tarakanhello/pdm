#ifndef RENAME_H
#define RENAME_H

#include <string>
#include <string_view>

#include "imodule.h"

namespace pdm::modules
{
/**
     * @brief Ключи параметров, которые ожидает модуль переименования.
     *
     * Эти константы используются как модулем, так и вызывающим кодом (ядром или UI),
     * чтобы избежать "магических строк" и опечаток.
     */
    namespace rename::keys
    {
        inline constexpr std::string_view excelFolder = "excel_folder";     ///< Путь к файлу спецификации Excel.
        inline constexpr std::string_view scansFolder = "scans_folder"; ///< Путь к папке с отсканированными файлами.
    } // end namespace rename::keys




    /**
     * @brief Модуль переименования отсканированных чертежей.
     *
     * Читает спецификацию, анализирует файлы в папке и переименовывает их
     * в соответствии с обозначениями и наименованиями из спецификации.
     */
    class Rename final : public shared::IModule
    {
    private:
        std::string m_id{ "rename" };           ///< Уникальный ID модуля.
        std::string m_name{ "Rename scans" };   ///< Имя модуля для отображения.
        std::string m_version{ "0.0.1" };       ///< Текущая версия модуля.

    public:
        Rename() = default;
        ~Rename() override = default;

        std::string_view getId() const override { return m_id; }
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
