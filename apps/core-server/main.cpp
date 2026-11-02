#include <iostream>
#include <string_view>

#include "build_core.h"
#include "job_params.h"

#include "rename_keys.h" // Включаем только ключи, не включая сам модуль!

/**
 * @brief Константы путей для тестирования.
 * @note В будущем они должны считываться из конфигурационного файла (config.json).
 */
namespace constants
{

inline constexpr std::string_view kExcelFolderPath{ "C:/Users/TarakanovVV/Downloads/someTestFiles/" };
inline constexpr std::string_view kScansFolderPath{ "C:/Users/TarakanovVV/Downloads/someTestFiles/scans" };

}

int main()
{
    std::cout << "=== Run PDM System ===\n";

    // 1. Создаем Ядро
    auto core{ pdm::composition::BuildCore() };

    // 2. Готовим параметры для задачи
    pdm::shared::JobParams params;
    // Используем константы из модуля, чтобы не опечататься
    params.set(pdm::modules::rename::keys::excelFolder, constants::kExcelFolderPath);
    params.set(pdm::modules::rename::keys::scansFolder, constants::kScansFolderPath);

    // 3. Просим ядро запустить модуль по его ID
    core->runModule("rename", params);

    std::cout << "=== Close PDM System ===\n";

    return 0;
}
