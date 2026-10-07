#include "onnx_person_adapter.h"

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {
struct Letterbox { float scale; int padX; int padY; };
cv::Mat letterbox(const cv::Mat& source, int width, int height, Letterbox& info) {
    info.scale = std::min(static_cast<float>(width) / source.cols,
                          static_cast<float>(height) / source.rows);
    const int resizedWidth = static_cast<int>(source.cols * info.scale);
    const int resizedHeight = static_cast<int>(source.rows * info.scale);
    info.padX = (width - resizedWidth) / 2;
    info.padY = (height - resizedHeight) / 2;
    cv::Mat resized, result(height, width, CV_8UC3, cv::Scalar(114, 114, 114));
    cv::resize(source, resized, cv::Size(resizedWidth, resizedHeight));
    resized.copyTo(result(cv::Rect(info.padX, info.padY, resizedWidth, resizedHeight)));
    return result;
}
}

OnnxPersonAdapter::OnnxPersonAdapter() = default;
OnnxPersonAdapter::~OnnxPersonAdapter() = default;

bool OnnxPersonAdapter::load(const ModelConfig& config, std::string& error) {
    try {
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "posevent-person");
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(4);
        session_ = std::make_unique<Ort::Session>(*env_, config.modelPath.c_str(), options);
        const auto shape = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 4 || shape[1] != 3 || shape[2] <= 0 || shape[3] <= 0) {
            error = "unsupported ONNX input shape";
            session_.reset();
            return false;
        }
        inputHeight_ = static_cast<int>(shape[2]);
        inputWidth_ = static_cast<int>(shape[3]);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        session_.reset();
        return false;
    }
}

ModelInference OnnxPersonAdapter::infer(const VideoFrame& frame) {
    ModelInference result;
    if (!session_ || !frame.pixels || frame.width == 0 || frame.height == 0) {
        result.error = "ONNX adapter is not loaded or frame is invalid";
        return result;
    }
    try {
        cv::Mat rgb(static_cast<int>(frame.height), static_cast<int>(frame.width),
                    CV_8UC3, const_cast<uint8_t*>(frame.pixels->data()));
        Letterbox box{};
        cv::Mat padded = letterbox(rgb, inputWidth_, inputHeight_, box);
        cv::Mat floatImage;
        padded.convertTo(floatImage, CV_32F, 1.0 / 255.0);
        std::vector<float> input(static_cast<size_t>(3) * inputWidth_ * inputHeight_);
        std::vector<cv::Mat> channels = {
            cv::Mat(inputHeight_, inputWidth_, CV_32F, input.data()),
            cv::Mat(inputHeight_, inputWidth_, CV_32F, input.data() + inputWidth_ * inputHeight_),
            cv::Mat(inputHeight_, inputWidth_, CV_32F, input.data() + 2 * inputWidth_ * inputHeight_)};
        cv::split(floatImage, channels);
        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
        const std::array<int64_t, 4> shape{1, 3, inputHeight_, inputWidth_};
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memory, input.data(), input.size(), shape.data(), shape.size());
        Ort::AllocatorWithDefaultOptions allocator;
        auto inputName = session_->GetInputNameAllocated(0, allocator);
        auto outputName = session_->GetOutputNameAllocated(0, allocator);
        const char* inputNames[] = {inputName.get()};
        const char* outputNames[] = {outputName.get()};
        auto outputs = session_->Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor,
                                     1, outputNames, 1);
        const auto outputInfo = outputs[0].GetTensorTypeAndShapeInfo();
        const auto outputShape = outputInfo.GetShape();
        if (outputShape.size() != 3 || outputShape[1] < 5) {
            result.error = "unsupported ONNX output shape";
            return result;
        }
        const int channelsCount = static_cast<int>(outputShape[1]);
        const int predictions = static_cast<int>(outputShape[2]);
        const float* values = outputs[0].GetTensorData<float>();
        result.ok = true;
        result.frame = {frame.frameSeq, frame.timestampMs, frame.width, frame.height, {}, {}};
        for (int index = 0; index < predictions; ++index) {
            float best = 0.0F;
            int classId = 0;
            for (int cls = 0; cls < channelsCount - 4; ++cls) {
                const float score = values[(4 + cls) * predictions + index];
                if (score > best) { best = score; classId = cls; }
            }
            if (classId != 0 || best < 0.25F) continue;
            const float cx = values[index], cy = values[predictions + index];
            const float width = values[2 * predictions + index];
            const float height = values[3 * predictions + index];
            const float x1 = std::clamp((cx - width / 2 - box.padX) / box.scale / frame.width, 0.0F, 1.0F);
            const float y1 = std::clamp((cy - height / 2 - box.padY) / box.scale / frame.height, 0.0F, 1.0F);
            const float x2 = std::clamp((cx + width / 2 - box.padX) / box.scale / frame.width, 0.0F, 1.0F);
            const float y2 = std::clamp((cy + height / 2 - box.padY) / box.scale / frame.height, 0.0F, 1.0F);
            result.frame.detections.push_back({x1, y1, x2 - x1, y2 - y1, best, "person"});
        }
        return result;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        return result;
    }
}
