#include <iostream>
#include <memory>

#include "core.h"
#include "rename.h"

int main()
{
    std::cout << "=== Run PDM System ===\n";

    // 1. Создаем Ядро
    pdm::Core core;

    // 2. Создаем модуль и отдаем его ядру
    core.addModule(std::make_unique<pdm::modules::Rename>());

    // 3. Готовим параметры для задачи
    pdm::shared::JobParams params;
    // Используем константы из модуля, чтобы не опечататься
    params.set(pdm::modules::rename::keys::excelFolder, "C:/Users/TarakanovVV/Downloads/someTestFiles/");
    params.set(pdm::modules::rename::keys::scansFolder, "C:/Users/TarakanovVV/Downloads/someTestFiles/scans");

    // 4. Просим ядро запустить модуль по его ID
    core.runModule("rename", params);

    std::cout << "=== Close PDM System ===\n";

    return 0;
}
