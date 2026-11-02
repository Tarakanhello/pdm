#include "module_factory.h"

#include <stdexcept>

#include "qxlsx_reader.h"
#include "rename.h"

namespace pdm::composition
{
    std::unique_ptr<pdm::shared::IModule> CreateModule(shared::ModuleId id)
    {
        switch(id)
        {
        case shared::ModuleId::Rename:
            // Внедряем зависимость (QxlsxReader) в модуль Rename
            return std::make_unique<modules::Rename>(std::make_unique<infrastructure::QxlsxReader>());

        case shared::ModuleId::NumberOfElements:
            throw std::logic_error("pdm::apps::CreateModule: Invalid ModuleId (NumberOfElements)");
        }

        throw std::logic_error("pdm::composition::CreateModule: unhandled ModuleId");
        return nullptr;
    }
}
