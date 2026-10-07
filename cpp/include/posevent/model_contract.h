#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Detection {
    float x = 0.0F, y = 0.0F, w = 0.0F, h = 0.0F;
    float confidence = 0.0F;
    std::string label = "person";
};

struct Keypoint { float x = 0.0F, y = 0.0F, confidence = 0.0F; };
struct KeypointPose { std::vector<Keypoint> keypoints; };

struct DetectionFrame {
    uint64_t frameSeq = 0;
    uint64_t timestampMs = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<Detection> detections;
    std::vector<KeypointPose> poses;
};
