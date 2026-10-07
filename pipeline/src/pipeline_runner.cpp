#include "pipeline_runner.h"

PipelineRunner::PipelineRunner(std::unique_ptr<ModelAdapter> model,
                               std::vector<ZoneDefinition> zones,
                               regionRouter::config debounce)
    : model_(std::move(model)), engine_(std::move(zones), debounce) {}

bool PipelineRunner::loadModel(const ModelConfig& config, std::string& error) {
    if (!model_) {
        error = "model adapter is not configured";
        return false;
    }
    return model_->load(config, error);
}

bool PipelineRunner::process(const VideoFrame& input, PoseventFrameResult& output,
                             std::string& error) {
    if (!model_) {
        error = "model adapter is not configured";
        return false;
    }
    const auto inference = model_->infer(input);
    if (!inference.ok) {
        error = inference.error.empty() ? "model inference failed" : inference.error;
        return false;
    }
    output = engine_.process(inference.frame);
    return true;
}

bool PipelineRunner::reloadZones(std::vector<ZoneDefinition> zones,
                                 regionRouter::config debounce,
                                 std::string& error) {
    if (zones.empty()) {
        error = "reload rejected: zone set must not be empty";
        return false;
    }
    // Construct fully before replacing the current engine. If construction or
    // validation later fails in a config loader, the old engine remains live.
    PoseventEngine replacement(std::move(zones), debounce);
    engine_ = std::move(replacement);
    return true;
}

std::string PipelineRunner::render(const PoseventFrameResult& output) const {
    return renderSvg(output, output.width, output.height);
}
