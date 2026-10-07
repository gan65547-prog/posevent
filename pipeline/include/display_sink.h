#pragma once

#include <string>

class DisplaySink {
public:
    virtual ~DisplaySink() = default;
    virtual bool present(const std::string& renderedFrame, std::string& error) = 0;
};

// Development/evidence sink. A platform DRM/KMS sink can implement the same
// interface without changing PipelineRunner or posevent_core.
class AtomicFileDisplaySink final : public DisplaySink {
public:
    explicit AtomicFileDisplaySink(std::string path) : path_(std::move(path)) {}
    bool present(const std::string& renderedFrame, std::string& error) override;

private:
    std::string path_;
};
