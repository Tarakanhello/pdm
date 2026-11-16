#ifndef TESSERACT_STAMP_RECOGNIZER_H
#define TESSERACT_STAMP_RECOGNIZER_H

#include <filesystem>
#include <optional>
#include <string>

#include "istamp_recognizer.h"

namespace pdm::infrastructure
{

    /**
     * @brief Распознаватель штампа с использованием классического OCR (Tesseract).
     */
    class TesseractStampRecognizer final : public shared::IStampRecognizer
    {
    private:
        std::string m_tessdataPath;
    public:
        explicit TesseractStampRecognizer(const std::string& tessdataPath);
        ~TesseractStampRecognizer() override = default;

        std::optional<shared::Document> recognize(const std::filesystem::path& imagePath) override;
    };

} // end namespace pdm::infrastructure

#endif // TESSERACT_STAMP_RECOGNIZER_H
