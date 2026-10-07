#pragma once

#include "osd_renderer.h"
#include "pipeline_contract.h"

#include <memory>
#include <string>

class PipelineRunner {
public:
    PipelineRunner(std::unique_ptr<ModelAdapter> model,
                   std::vector<ZoneDefinition> zones,
                   regionRouter::config debounce = {3, 5, 10});

    bool loadModel(const ModelConfig& config, std::string& error);
    bool process(const VideoFrame& input, PoseventFrameResult& output,
                 std::string& error);
    bool reloadZones(std::vector<ZoneDefinition> zones,
                     regionRouter::config debounce, std::string& error);
    std::string render(const PoseventFrameResult& output) const;

private:
    std::unique_ptr<ModelAdapter> model_;
    PoseventEngine engine_;
};
