#pragma once

#include "pipeline_contract.h"

#include <algorithm>
#include <vector>

// Model-specific adapters can map their SDK output to this common result.
// YOLOPose keeps keypoints in DetectionFrame::poses; region logic only uses
// the normalized person detections.
class YoloPoseAdapter : public ModelAdapter {
public:
    ~YoloPoseAdapter() override = default;
};

inline float detectionIoU(const Detection& left, const Detection& right) {
    const float x1 = std::max(left.x, right.x);
    const float y1 = std::max(left.y, right.y);
    const float x2 = std::min(left.x + left.w, right.x + right.w);
    const float y2 = std::min(left.y + left.h, right.y + right.h);
    const float intersection = std::max(0.0F, x2 - x1) * std::max(0.0F, y2 - y1);
    const float unionArea = left.w * left.h + right.w * right.h - intersection;
    return unionArea > 0.0F ? intersection / unionArea : 0.0F;
}

inline DetectionFrame mergeDetectionFrames(const std::vector<DetectionFrame>& frames,
                                           float iouThreshold = 0.5F) {
    DetectionFrame merged;
    if (!frames.empty()) {
        merged.frameSeq = frames.front().frameSeq;
        merged.timestampMs = frames.front().timestampMs;
        merged.width = frames.front().width;
        merged.height = frames.front().height;
    }
    for (const auto& frame : frames) {
        merged.poses.insert(merged.poses.end(), frame.poses.begin(), frame.poses.end());
        for (const auto& candidate : frame.detections) {
            auto duplicate = std::find_if(merged.detections.begin(), merged.detections.end(),
                [&](const Detection& existing) {
                    return existing.label == candidate.label &&
                           detectionIoU(existing, candidate) >= iouThreshold;
                });
            if (duplicate == merged.detections.end()) {
                merged.detections.push_back(candidate);
            } else if (candidate.confidence > duplicate->confidence) {
                *duplicate = candidate;
            }
        }
    }
    return merged;
}
