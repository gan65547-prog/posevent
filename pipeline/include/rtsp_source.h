#pragma once

#include "pipeline_contract.h"

#include <cstdio>
#include <string>

class RtspSource {
public:
    struct Config {
        std::string url;
        uint32_t width = 1280;
        uint32_t height = 720;
        uint32_t reconnectAttempts = 3;
        uint32_t reconnectDelayMs = 250;
    };

    explicit RtspSource(Config config);
    ~RtspSource();
    RtspSource(const RtspSource&) = delete;
    RtspSource& operator=(const RtspSource&) = delete;

    bool open(std::string& error);
    bool read(VideoFrame& frame, std::string& error);
    void close();
    uint64_t frameCount() const { return frameSeq_; }

private:
    bool reconnect(std::string& error);
    std::string command() const;

    Config config_;
    FILE* pipe_ = nullptr;
    uint64_t frameSeq_ = 0;
    uint32_t reconnects_ = 0;
};
