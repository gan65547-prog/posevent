#include "rknn_person_adapter.h"
#include "pipeline_runner.h"
#include "rtsp_source.h"
#include "framebuffer_sink.h"
#include "wayland_display_sink.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <iostream>
#include <thread>

namespace {
bool number(const std::string& text, const char* key, float& value) {
    const std::regex expression(std::string("\\\"") + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(text, match, expression)) return false;
    value = std::stof(match[1].str());
    return true;
}

bool loadRect(const std::string& path, ZoneDefinition& zone, std::string& error) {
    std::ifstream input(path);
    if (!input) { error = "cannot open regions config: " + path; return false; }
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    float x = 0, y = 0, w = 0, h = 0;
    if (!number(text, "x", x) || !number(text, "y", y) ||
        !number(text, "w", w) || !number(text, "h", h)) {
        error = "regions config has no complete rectangle";
        return false;
    }
    zone.geometry.rx = x; zone.geometry.ry = y;
    zone.geometry.rw = w; zone.geometry.rh = h;
    return true;
}
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) {
        std::cerr << "usage: rknn_live_runner <rtsp-url> <model.rknn> [framebuffer] [regions.json]\n";
        return 2;
    }
    ZoneDefinition zone;
    zone.geometry.id = 1;
    zone.geometry.name = "entrance";
    zone.geometry.type = regionType::rect;
    zone.geometry.rx = .2F; zone.geometry.ry = .2F;
    zone.geometry.rw = .4F; zone.geometry.rh = .5F;
    zone.classes = {"person"};
    zone.minConfidence = .25F;

    const std::string regionsPath = argc >= 5 ? argv[4] : "";
    std::string configError;
    if (!regionsPath.empty() && !loadRect(regionsPath, zone, configError)) {
        std::cerr << configError << '\n';
        return 1;
    }
    PipelineRunner runner(std::make_unique<RknnPersonAdapter>(), {zone});
    std::string error;
    if (!runner.loadModel({argv[2], "person"}, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    RtspSource source({argv[1], 1920, 1080, 3, 250});
    if (!source.open(error)) {
        std::cerr << error << '\n';
        return 1;
    }
    FramebufferSink framebuffer(argc >= 4 ? argv[3] : "/dev/fb0");
    WaylandDisplaySink wayland(1920, 1080);
    const bool useWayland = std::getenv("POSEVENT_DISPLAY_MODE") != nullptr &&
                            std::string(std::getenv("POSEVENT_DISPLAY_MODE")) == "wayland";
    const char* maxFramesText = std::getenv("POSEVENT_MAX_FRAMES");
    const int maxFrames = maxFramesText ? std::atoi(maxFramesText) : 30;
    int processed = 0;
    std::string lastConfigError;
    while (maxFrames <= 0 || processed < maxFrames) {
        if (!regionsPath.empty()) {
            ZoneDefinition updated = zone;
            if (loadRect(regionsPath, updated, configError) &&
                (updated.geometry.rx != zone.geometry.rx ||
                 updated.geometry.ry != zone.geometry.ry ||
                 updated.geometry.rw != zone.geometry.rw ||
                 updated.geometry.rh != zone.geometry.rh)) {
                if (runner.reloadZones({updated}, {3, 5, 10}, configError)) {
                    zone = updated;
                    std::cout << "regions reload applied: " << regionsPath << '\n';
                }
            } else if (!configError.empty() && configError != lastConfigError) {
                std::cerr << "regions reload rejected: " << configError << '\n';
                lastConfigError = configError;
            }
        }
        VideoFrame frame;
        PoseventFrameResult result;
        if (!source.read(frame, error) || !runner.process(frame, result, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        if (argc >= 4) {
            const bool presented = useWayland
                ? wayland.present(frame, result, error)
                : framebuffer.present(frame, result, error);
            if (!presented) {
                std::cerr << error << '\n';
                return 1;
            }
        }
        std::cout << "frame=" << result.frameSeq
                  << " detections=" << result.detections.size()
                  << " events=" << result.events.size() << '\n';
        ++processed;
    }
    return 0;
}
