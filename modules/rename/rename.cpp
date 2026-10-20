#include "rename.h"

#include <iostream>

namespace pdm::modules
{

    void Rename::execute(const shared::JobParams& params)
    {
        std::cout << "[RenameModule] Module runs and ready to work!\n";

        // Проверяем, передало ли ядро нужные пути

        if(!params.has(rename::keys::excelPath) || !params.has(rename::keys::scansFolder))
        {
            std::cerr << "[RenameModule] Error: paths ways has not found!\n";
            return;
        }

        std::string excelPath{ params.get(rename::keys::excelPath) };
        std::string scansFolder{ params.get(rename::keys::scansFolder) };


        std::cout << "Specification file path: " << excelPath << '\n';
        std::cout << "Scan files path: " << scansFolder << '\n';
    }

} // end namespcae pdm

