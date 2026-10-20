#include <algorithm>
#include <format>
#include <iostream>

#include "core.h"

namespace pdm
{
    void Core::addModule(std::unique_ptr<shared::IModule> module)
    {
        // Получаем ID модуля (копируем строку, так как модуль может быть удален)
        std::string id(module->getId());

        // Проверяем, есть ли уже модуль с таким ID
        if(m_modules.contains(id))
        {
            std::cerr << std::format("[Core] Warning: Module with ID [{}] is already registered!\n",
                                    id);
            return;
        }
        std::cout << std::format("[Core] Registered module: {} (v{}).\n",
                                 module->getName(), module->getVersion());

        m_modules.emplace(id, std::move(module));
    }

    void Core::runModule(const std::string& moduleId, const shared::JobParams& params)
    {
        auto it{ m_modules.find(moduleId) };
        if(it == m_modules.end())
        {
            std::cerr << std::format("[Core] Error: Module with ID [{}] not found.\n", moduleId);
            return;
        }

        std::cout << std::format("[Core] Run module with ID: [{}].\n", moduleId);

        it->second->execute(params);
    }
}
