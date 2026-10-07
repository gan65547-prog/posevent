#pragma once

#include "pipeline_contract.h"

#include <memory>

namespace Ort { class Env; class Session; }

class OnnxPersonAdapter final : public ModelAdapter {
public:
    OnnxPersonAdapter();
    ~OnnxPersonAdapter() override;
    bool load(const ModelConfig& config, std::string& error) override;
    ModelInference infer(const VideoFrame& frame) override;

private:
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    int inputWidth_ = 640;
    int inputHeight_ = 640;
};
