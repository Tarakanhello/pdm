#ifndef MOCK_STAMP_RECOGNIZER_H
#define MOCK_STAMP_RECOGNIZER_H

#include "istamp_recognizer.h"

namespace pdm::infrastructure
{

    class MockStampRecognizer final : public shared::IStampRecognizer
    {
    private:

    public:
        ~MockStampRecognizer() override = default;

        std::optional<shared::Document> recognize(const std::filesystem::path& imagePath) override
        {
            auto filename{ imagePath.filename().string() };

            if(filename == "data1.jpg")
            {
                return shared::Document{ "сб065", shared::DocumentTypeCode::Specification, "Опора с механизмом поворота крана", 2 };
            }

            if(filename == "data2.jpg")
            {
                return shared::Document{ "сб065", shared::DocumentTypeCode::Specification, "Опора с механизмом поворота крана", 2 };
            }

            if(filename == "data3.jpg")
            {
                return shared::Document{ "сб065", shared::DocumentTypeCode::AssemblyDrawing, "Опора с механизмом поворота крана", 1 };
            }

            return std::nullopt; // Не удалось распознать
        }

    };

} // end namespace pdm::infrastructure

#endif // MOCK_STAMP_RECOGNIZER_H
