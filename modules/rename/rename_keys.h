#ifndef RENAME_KEYS_H
#define RENAME_KEYS_H

#include <string_view>

/**
 * @brief Ключи параметров, которые ожидает модуль переименования.
 *
 * Эти константы используются как модулем, так и вызывающим кодом (main.cpp),
 * чтобы избежать "магических строк" и опечаток.
 */
namespace pdm::modules::rename::keys
{
    inline constexpr std::string_view excelFolder = "excel_folder";     ///< Путь к файлу спецификации Excel.
    inline constexpr std::string_view scansFolder = "scans_folder";     ///< Путь к папке с отсканированными файлами.
} // end namespace rename::keys

#endif // RENAME_KEYS_H
