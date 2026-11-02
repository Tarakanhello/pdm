#ifndef MODULE_FACTORY_H
#define MODULE_FACTORY_H

#include <memory>

#include "imodule.h"
#include "module_id.h"


namespace pdm::composition
{

    /**
     * @brief Фабрика для создания конкретных модулей по их ID.
     *
     * Это единственное место (кроме этого файла), где система знает
     * о существовании конкретных классов модулей (Rename, QxlsxReader).
     *
     * @param id Типизированный идентификатор модуля.
     * @return Умный указатель на созданный модуль.
     */
    std::unique_ptr<pdm::shared::IModule> CreateModule(shared::ModuleId id);
}


#endif // MODULE_FACTORY_H
