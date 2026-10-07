#include "display_sink.h"
#include "pipeline_runner.h"
#include "rtsp_source.h"

#include <cassert>
#include <iostream>

class LiveContractAdapter final : public ModelAdapter {
public:
    bool load(const ModelConfig&, std::string&) override { return true; }
    ModelInference infer(const VideoFrame& input) override {
        ModelInference result;
        result.ok = true;
        result.frame.frameSeq = input.frameSeq;
        result.frame.timestampMs = input.timestampMs;
        result.frame.width = input.width;
        result.frame.height = input.height;
        result.frame.detections = {{.30F, .30F, .10F, .30F, .95F, "person"}};
        return result;
    }
};

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    ZoneDefinition zone;
    zone.geometry.id = 1;
    zone.geometry.name = "entrance";
    zone.geometry.type = regionType::rect;
    zone.geometry.rx = .2F; zone.geometry.ry = .2F;
    zone.geometry.rw = .4F; zone.geometry.rh = .5F;
    PipelineRunner runner(std::make_unique<LiveContractAdapter>(), {zone});
    std::string error;
    assert(runner.loadModel({"contract-test", "person"}, error));
    RtspSource source({argv[1], 320, 240, 2, 10});
    assert(source.open(error));
    AtomicFileDisplaySink sink("live-pipeline.svg");
    PoseventFrameResult result;
    VideoFrame frame;
    for (int i = 0; i < 3; ++i) {
        assert(source.read(frame, error));
        assert(runner.process(frame, result, error));
        assert(sink.present(runner.render(result), error));
    }
    assert(result.frameSeq == 3 && result.detections.size() == 1);
    std::remove("live-pipeline.svg");
    std::cout << "live pipeline contract: PASS frames=" << source.frameCount() << "\n";
}
