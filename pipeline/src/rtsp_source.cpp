#include "rtsp_source.h"

#include <chrono>
#include <memory>
#include <sstream>
#include <thread>

namespace {
std::string shellQuote(const std::string& value) {
    std::string result = "'";
    for (const char character : value) {
        if (character == '\'') result += "'\\''";
        else result += character;
    }
    return result + "'";
}
}

RtspSource::RtspSource(Config config) : config_(std::move(config)) {}

RtspSource::~RtspSource() { close(); }

std::string RtspSource::command() const {
    std::ostringstream command;
    command << "ffmpeg -hide_banner -loglevel error -rtsp_transport tcp -i "
            << shellQuote(config_.url)
            << " -an -f rawvideo -pix_fmt rgb24 -vf scale=" << config_.width
            << ":" << config_.height << " -";
    return command.str();
}

bool RtspSource::open(std::string& error) {
    if (config_.url.empty() || config_.width == 0 || config_.height == 0) {
        error = "RTSP URL and non-zero dimensions are required";
        return false;
    }
    return reconnect(error);
}

bool RtspSource::reconnect(std::string& error) {
    close();
    pipe_ = popen(command().c_str(), "r");
    if (pipe_ == nullptr) {
        error = "failed to start ffmpeg RTSP decoder";
        return false;
    }
    return true;
}

bool RtspSource::read(VideoFrame& frame, std::string& error) {
    const size_t bytes = static_cast<size_t>(config_.width) * config_.height * 3U;
    auto pixels = std::make_shared<std::vector<uint8_t>>(bytes);
    size_t received = 0;
    while (received < bytes && pipe_ != nullptr) {
        const size_t count = fread(pixels->data() + received, 1, bytes - received, pipe_);
        if (count == 0) break;
        received += count;
    }
    if (received != bytes) {
        if (reconnects_++ >= config_.reconnectAttempts) {
            error = "RTSP decoder ended after reconnect attempts";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnectDelayMs));
        if (!reconnect(error)) return false;
        return read(frame, error);
    }
    reconnects_ = 0;
    frame.frameSeq = ++frameSeq_;
    frame.timestampMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    frame.width = config_.width;
    frame.height = config_.height;
    frame.pixels = std::move(pixels);
    return true;
}

void RtspSource::close() {
    if (pipe_ != nullptr) {
        pclose(pipe_);
        pipe_ = nullptr;
    }
}
