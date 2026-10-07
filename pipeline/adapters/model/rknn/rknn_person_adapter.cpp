#include "rknn_person_adapter.h"

#include <algorithm>

RknnPersonAdapter::RknnPersonAdapter() = default;
RknnPersonAdapter::~RknnPersonAdapter() = default;

bool RknnPersonAdapter::load(const ModelConfig& config, std::string& error) {
    if (config.modelPath.empty()) {
        error = "RKNN model path is empty";
        return false;
    }
    auto model = std::make_unique<ModelContext>();
    if (!model->init(config.modelPath.c_str())) {
        error = "RKNN model initialization failed: " + config.modelPath;
        return false;
    }
    model_ = std::move(model);
    return true;
}

ModelInference RknnPersonAdapter::infer(const VideoFrame& frame) {
    ModelInference result;
    if (!model_ || !frame.pixels || frame.width == 0 || frame.height == 0) {
        result.error = "RKNN adapter is not loaded or frame is invalid";
        return result;
    }
    ImageBuffer image;
    image.width = static_cast<int>(frame.width);
    image.height = static_cast<int>(frame.height);
    image.channels = 3;
    image.data.assign(frame.pixels->begin(), frame.pixels->end());
    DetectResult detections;
    if (!model_->infer(image, detections, confidence_)) {
        result.error = "RKNN inference failed";
        return result;
    }
    result.ok = true;
    result.frame.frameSeq = frame.frameSeq;
    result.frame.timestampMs = frame.timestampMs;
    result.frame.width = frame.width;
    result.frame.height = frame.height;
    for (const auto& box : detections.boxes) {
        // COCO class 0 is person. Ignore other classes at the adapter boundary.
        if (box.class_id != 0) continue;
        const float x = std::clamp(static_cast<float>(box.x1) / frame.width, 0.0F, 1.0F);
        const float y = std::clamp(static_cast<float>(box.y1) / frame.height, 0.0F, 1.0F);
        const float x2 = std::clamp(static_cast<float>(box.x2) / frame.width, 0.0F, 1.0F);
        const float y2 = std::clamp(static_cast<float>(box.y2) / frame.height, 0.0F, 1.0F);
        result.frame.detections.push_back({x, y, std::max(0.0F, x2 - x),
                                           std::max(0.0F, y2 - y),
                                           box.confidence, "person"});
    }
    return result;
}
