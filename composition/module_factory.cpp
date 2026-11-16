#include "module_factory.h"

#include <stdexcept>

//#include "mock_stamp_recognizer.h"
#include "tesseract_stamp_recognizer.h"

#include "rename.h"

namespace pdm::composition
{
    std::unique_ptr<pdm::shared::IModule> CreateModule(shared::ModuleId id)
    {
        switch(id)
        {
        case shared::ModuleId::Rename:
            //return std::make_unique<modules::Rename>(std::make_unique<infrastructure::MockStampRecognizer>());
            return std::make_unique<modules::Rename>(std::make_unique<infrastructure::TesseractStampRecognizer>("C:/Users/TarakanovVV/source/Qt/pdm-core/tessdata"));
        case shared::ModuleId::NumberOfElements:
            throw std::logic_error("pdm::apps::CreateModule: Invalid ModuleId (NumberOfElements)");
        }

        throw std::logic_error("pdm::composition::CreateModule: unhandled ModuleId");
    }
}
