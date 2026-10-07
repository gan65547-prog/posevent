#pragma once

#include "pipeline_contract.h"
#include "infer_yolo.h"

#include <memory>

class RknnPersonAdapter final : public ModelAdapter {
public:
    RknnPersonAdapter();
    ~RknnPersonAdapter() override;
    bool load(const ModelConfig& config, std::string& error) override;
    ModelInference infer(const VideoFrame& frame) override;

private:
    std::unique_ptr<ModelContext> model_;
    float confidence_ = 0.25F;
};
