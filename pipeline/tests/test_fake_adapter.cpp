#include "pipeline_contract.h"
#include "posevent_engine.h"

#include <cassert>
#include <iostream>

class FakePersonAdapter final : public ModelAdapter {
public:
    bool load(const ModelConfig&, std::string&) override { return true; }
    ModelInference infer(const VideoFrame& input) override {
        ModelInference result;
        result.ok = true;
        result.frame.frameSeq = input.frameSeq;
        result.frame.timestampMs = input.timestampMs;
        result.frame.width = input.width;
        result.frame.height = input.height;
        result.frame.detections.push_back({0.3F, 0.3F, 0.1F, 0.3F, 0.95F, "person"});
        return result;
    }
};

int main() {
    ZoneDefinition zone;
    zone.geometry.id = 1;
    zone.geometry.name = "entrance";
    zone.geometry.type = regionType::rect;
    zone.geometry.rx = 0.2F; zone.geometry.ry = 0.2F;
    zone.geometry.rw = 0.5F; zone.geometry.rh = 0.5F;
    PoseventEngine engine({zone});
    FakePersonAdapter adapter;
    std::string error;
    assert(adapter.load({"person.mock", "person"}, error));
    VideoFrame video{3, 1234, 1280, 720, {}};
    const auto inference = adapter.infer(video);
    assert(inference.ok && inference.frame.detections.size() == 1);
    const auto output = engine.process(inference.frame);
    assert(output.frameSeq == 3 && output.timestampMs == 1234);
    assert(output.width == 1280 && output.height == 720);
    assert(output.detections.size() == 1);
    std::cout << "fake adapter contract: PASS\n";
}
