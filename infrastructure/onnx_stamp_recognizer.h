#ifndef ONNX_STAMP_RECOGNIZER_H
#define ONNX_STAMP_RECOGNIZER_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/dnn.hpp>

#include "istamp_recognizer.h"

namespace pdm::infrastructure
{

/**
     * @brief Распознаватель штампа на базе моделей PaddleOCR (ONNX) через нативный OpenCV DNN.
     */
class OnnxStampRecognizer final : public shared::IStampRecognizer
{
private:
    cv::dnn::Net m_detNet;
    cv::dnn::Net m_clsNet;
    cv::dnn::Net m_recNet;
    std::vector<std::string> m_keys;
    bool m_initialized{ false };

public:
    explicit OnnxStampRecognizer(const std::filesystem::path& modelsDir);
    ~OnnxStampRecognizer() override = default;

    std::optional<shared::Document> recognize(const std::filesystem::path& imagePath) override;
};

} // namespace pdm::infrastructure

#endif // ONNX_STAMP_RECOGNIZER_H
