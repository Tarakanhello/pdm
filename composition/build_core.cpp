#include "build_core.h"

#include "module_factory.h"
#include "module_id.h"

namespace pdm::composition
{

std::unique_ptr<pdm::Core> BuildCore()
{
    auto core{ std::make_unique<pdm::Core>() };

    // Проходим по списку всех известных модулей и создаем их через фабрику
    for(const auto& id : shared::kAllModuleIds)
    {
        core->addModule(CreateModule(id));
    }

    return core;
}

}
