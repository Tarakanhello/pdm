#include "module_factory.h"

#include <filesystem>
#include <stdexcept>

#include "onnx_stamp_recognizer.h"

#include "rename.h"

namespace pdm::composition
{
    std::unique_ptr<pdm::shared::IModule> CreateModule(shared::ModuleId id)
    {
        switch(id)
        {
        case shared::ModuleId::Rename:
        {// Внедряем зависимость (QxlsxReader) в модуль Rename
            auto recognizer{ std::make_unique<infrastructure::OnnxStampRecognizer>(std::filesystem::path{"../../../../models"}) };
            recognizer->setDebugOutputDir( {"C:/Users/TarakanovVV/Downloads/someTestFiles/scans/debug_stamps" });

            return std::make_unique<modules::Rename>(std::move(recognizer));
        }
        case shared::ModuleId::NumberOfElements:
            throw std::logic_error("pdm::apps::CreateModule: Invalid ModuleId (NumberOfElements)");
        }

        throw std::logic_error("pdm::composition::CreateModule: unhandled ModuleId");
        return nullptr;
    }
}
