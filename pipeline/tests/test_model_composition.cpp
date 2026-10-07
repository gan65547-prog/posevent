#include "model_composition.h"

#include <cassert>
#include <iostream>

int main() {
    DetectionFrame first;
    first.frameSeq = 7; first.timestampMs = 99; first.width = 1280; first.height = 720;
    first.detections = {{.2F, .2F, .2F, .4F, .8F, "person"}};
    DetectionFrame second = first;
    second.detections = {{.21F, .2F, .2F, .4F, .95F, "person"},
                         {.7F, .2F, .1F, .2F, .9F, "person"}};
    KeypointPose pose;
    pose.keypoints.push_back({.3F, .3F, .9F});
    second.poses.push_back(pose);
    const auto merged = mergeDetectionFrames({first, second});
    assert(merged.frameSeq == 7 && merged.width == 1280);
    assert(merged.detections.size() == 2);
    assert(merged.detections[0].confidence == .95F);
    assert(merged.poses.size() == 1);
    assert(detectionIoU(first.detections[0], second.detections[0]) > .8F);
    std::cout << "model composition: PASS\n";
}
