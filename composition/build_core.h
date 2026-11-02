#ifndef BUILD_CORE_H
#define BUILD_CORE_H

#include <memory>
#include "core.h"

namespace pdm::composition
{
    /**
     * @brief Собирает и инициализирует ядро системы (Composition Root).
     *
     * Создает экземпляр Core, а затем регистрирует в нем все доступные модули,
     * перечисленные в shared::kAllModuleIds. Конкретные реализации модулей
     * скрыты внутри фабрики CreateModule.
     *
     * @return Умный указатель на готовое к работе ядро.
     */
    std::unique_ptr<pdm::Core> BuildCore();
}
#endif // BUILD_CORE_H
