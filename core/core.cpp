#include <algorithm>
#include <format>
#include <iostream>

#include "core.h"

namespace pdm
{
    void Core::addModule(std::unique_ptr<shared::IModule> module)
    {
        // Получаем ID модуля (копируем строку, так как модуль может быть удален)
        auto id(module->getId());

        // Проверяем, есть ли уже модуль с таким ID
        if(m_modules.contains(id))
        {
            std::cerr << std::format("[Core] Warning: Module with ID [{}] is already registered!\n",
                                    shared::idToString(id));
            return;
        }
        std::cout << std::format("[Core] Registered module: {} (v{}).\n",
                                 module->getName(), module->getVersion());

        m_modules.emplace(id, std::move(module));
    }

    void Core::runModule(std::string_view moduleName, const shared::JobParams& params)
    {
        auto opId{ shared::ParseModuleId(moduleName) };
        if(!opId)
        {
            std::cerr << std::format("[Core] Error: Module with name [{}] not found.\n", moduleName);
            return;
        }
        auto it{ m_modules.find(*opId) };

        if(it == m_modules.end())
        {
            std::cerr << std::format("[Core] Error: Module with ID [{}] not found.\n", shared::idToString(*opId));
            return;
        }

        std::cout << std::format("[Core] Run module with ID: [{}].\n", shared::idToString(*opId));

        it->second->execute(params);
    }
}
