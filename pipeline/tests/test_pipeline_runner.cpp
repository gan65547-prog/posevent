#include "pipeline_runner.h"

#include <cassert>
#include <iostream>

class TestAdapter final : public ModelAdapter {
public:
    bool load(const ModelConfig&, std::string&) override { return true; }
    ModelInference infer(const VideoFrame& input) override {
        ModelInference output;
        output.ok = true;
        output.frame.frameSeq = input.frameSeq;
        output.frame.timestampMs = input.timestampMs;
        output.frame.width = input.width;
        output.frame.height = input.height;
        output.frame.detections = {{0.3F, 0.3F, 0.1F, 0.3F, 0.9F, "person"}};
        return output;
    }
};

int main() {
    ZoneDefinition zone;
    zone.geometry.id = 8;
    zone.geometry.name = "entrance";
    zone.geometry.type = regionType::rect;
    zone.geometry.rx = 0.2F; zone.geometry.ry = 0.2F;
    zone.geometry.rw = 0.5F; zone.geometry.rh = 0.5F;
    PipelineRunner runner(std::make_unique<TestAdapter>(), {zone});
    std::string error;
    assert(runner.loadModel({"fake", "person"}, error));
    VideoFrame input{3, 99, 640, 480, {}};
    PoseventFrameResult output;
    assert(runner.process(input, output, error));
    assert(output.frameSeq == 3 && output.detections.size() == 1);
    assert(runner.render(output).find("data-frame=\"3\"") != std::string::npos);
    ZoneDefinition replacement = zone;
    replacement.geometry.id = 9;
    replacement.geometry.name = "updated";
    replacement.geometry.rx = 0.7F;
    assert(runner.reloadZones({replacement}, {3, 5, 10}, error));
    assert(runner.process(input, output, error));
    assert(output.overlays.size() == 1 && output.overlays[0].regionId == 9);
    assert(output.overlays[0].geometry.rx == 0.7F);
    assert(!runner.reloadZones({}, {3, 5, 10}, error));
    assert(output.overlays[0].regionId == 9);
    std::cout << "pipeline runner: PASS\n";
}
