#pragma once

#include "posevent/model_contract.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct VideoFrame {
    uint64_t frameSeq = 0;
    uint64_t timestampMs = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    // Owned pixel storage supplied by the RTSP source adapter.
    std::shared_ptr<const std::vector<uint8_t>> pixels;
};

struct ModelConfig {
    std::string modelPath;
    std::string modelName = "person";
};

struct ModelInference {
    bool ok = false;
    std::string error;
    DetectionFrame frame;
};

class ModelAdapter {
public:
    virtual ~ModelAdapter() = default;
    virtual bool load(const ModelConfig& config, std::string& error) = 0;
    virtual ModelInference infer(const VideoFrame& frame) = 0;
};
