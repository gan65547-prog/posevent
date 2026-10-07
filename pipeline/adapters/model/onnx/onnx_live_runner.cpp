#include "onnx_person_adapter.h"
#include "annotated_rtsp_sink.h"
#include "pipeline_runner.h"
#include "rtsp_source.h"

#include <iostream>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace {
std::atomic_bool running{true};
void stopHandler(int) { running = false; }
int maxFrames() {
    const char* value = std::getenv("POSEVENT_MAX_FRAMES");
    if (!value || !*value) return 30;
    const int parsed = std::atoi(value);
    return parsed < 0 ? 30 : parsed;
}

bool loadRegionsConfig(const std::string& path,
                       std::vector<ZoneDefinition>& zones,
                       std::string& error) {
    std::ifstream input(path);
    if (!input) { error = "cannot open regions config: " + path; return false; }
    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string json = buffer.str();
    const auto array = json.find("\"regions\"");
    if (array == std::string::npos) { error = "regions array is missing"; return false; }
    const auto begin = json.find('[', array);
    if (begin == std::string::npos) { error = "regions array is invalid"; return false; }
    std::vector<std::string> objects;
    int depth = 0;
    size_t objectBegin = std::string::npos;
    for (size_t i = begin + 1; i < json.size(); ++i) {
        if (json[i] == '{') { if (depth++ == 0) objectBegin = i; }
        else if (json[i] == '}' && depth > 0 && --depth == 0)
            objects.push_back(json.substr(objectBegin, i - objectBegin + 1));
        else if (json[i] == ']' && depth == 0) break;
    }
    if (objects.empty()) { error = "regions array is empty"; return false; }
    std::regex number("\\\"([^\"]+)\\\"\\s*:\\s*([-+0-9.eE]+)");
    for (const auto& object : objects) {
        ZoneDefinition zone;
        zone.geometry.type = regionType::rect;
        zone.geometry.name = "region";
        std::smatch match;
        const std::regex namePattern("\\\"name\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
        if (std::regex_search(object, match, namePattern))
            zone.geometry.name = match[1].str();
        auto value = [&](const char* key, float& out) {
            const std::regex p("\\\"" + std::string(key) + "\\\"\\s*:\\s*([-+0-9.eE]+)");
            if (!std::regex_search(object, match, p)) return false;
            out = std::stof(match[1].str()); return true;
        };
        float id = 0;
        value("id", id); zone.geometry.id = static_cast<uint32_t>(id);
        value("x", zone.geometry.rx); value("y", zone.geometry.ry);
        value("w", zone.geometry.rw); value("h", zone.geometry.rh);
        float confidence = zone.minConfidence;
        if (value("min_confidence", confidence)) zone.minConfidence = confidence;
        const std::regex classPattern("\\\"classes\\\"\\s*:\\s*\\[([^]]*)\\]");
        if (std::regex_search(object, match, classPattern)) {
            const std::regex className("\\\"([^\\\"]+)\\\"");
            zone.classes.clear();
            for (auto it = std::sregex_iterator(match[1].first, match[1].second, className);
                 it != std::sregex_iterator(); ++it)
                zone.classes.push_back((*it)[1].str());
        }
        const std::regex polygonType("\\\"type\\\"\\s*:\\s*\\\"polygon\\\"");
        if (std::regex_search(object, polygonType)) {
            zone.geometry.type = regionType::polygon;
            std::regex point("\\[\\s*([-+0-9.eE]+)\\s*,\\s*([-+0-9.eE]+)\\s*\\]");
            for (auto it = std::sregex_iterator(object.begin(), object.end(), point);
                 it != std::sregex_iterator(); ++it)
                zone.geometry.poly.push_back({std::stof((*it)[1]), std::stof((*it)[2])});
            if (zone.geometry.poly.size() < 3) { error = "polygon needs 3 vertices"; return false; }
        } else if (zone.geometry.rx < 0 || zone.geometry.ry < 0 || zone.geometry.rw <= 0 ||
                   zone.geometry.rh <= 0 || zone.geometry.rx + zone.geometry.rw > 1 ||
                   zone.geometry.ry + zone.geometry.rh > 1) {
            error = "regions config contains an invalid normalized rect"; return false;
        }
        zones.push_back(std::move(zone));
    }
    return !zones.empty();
}
}

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4 && argc != 7 && argc != 8) return 2;
    std::signal(SIGINT, stopHandler);
    std::signal(SIGTERM, stopHandler);
    std::string error;
    ZoneDefinition zone;
    zone.geometry.id = 1;
    zone.geometry.name = "entrance";
    zone.geometry.type = regionType::rect;
    const int regionOffset = argc == 8 ? 4 : 3;
    zone.geometry.rx = argc == 7 || argc == 8 ? std::stof(argv[regionOffset]) : .2F;
    zone.geometry.ry = argc == 7 || argc == 8 ? std::stof(argv[regionOffset + 1]) : .2F;
    zone.geometry.rw = argc == 7 || argc == 8 ? std::stof(argv[regionOffset + 2]) : .6F;
    zone.geometry.rh = argc == 7 || argc == 8 ? std::stof(argv[regionOffset + 3]) : .6F;
    zone.classes = {"person"};
    zone.minConfidence = .25F;
    const char* configEnv = std::getenv("POSEVENT_REGIONS_CONFIG");
    const std::string configPath = configEnv ? configEnv : "";
    std::vector<ZoneDefinition> zones{zone};
    if (!configPath.empty() && !loadRegionsConfig(configPath, zones, error)) {
        std::cerr << error << '\n'; return 1;
    }
    PipelineRunner runner(std::make_unique<OnnxPersonAdapter>(), zones);
    if (!runner.loadModel({argv[2], "person"}, error)) { std::cerr << error << '\n'; return 1; }
    RtspSource source({argv[1], 640, 360, 3, 250});
    if (!source.open(error)) { std::cerr << error << '\n'; return 1; }
    std::unique_ptr<AnnotatedRtspSink> sink;
    if (argc == 4 || argc == 8) {
        sink = std::make_unique<AnnotatedRtspSink>(argv[3], 640, 360);
        if (!sink->open(error)) { std::cerr << error << '\n'; return 1; }
    }
    const int limit = maxFrames();
    int processed = 0;
    std::filesystem::file_time_type configMtime{};
    bool configSeen = false;
    while (running && (limit == 0 || processed < limit)) {
        if (!configPath.empty()) {
            std::error_code statusError;
            const auto current = std::filesystem::last_write_time(configPath, statusError);
            if (!statusError && (!configSeen || current != configMtime)) {
                std::vector<ZoneDefinition> replacements;
                std::string reloadError;
                if (loadRegionsConfig(configPath, replacements, reloadError)) {
                    if (runner.reloadZones(replacements, {3, 5, 10}, reloadError)) {
                        zones = std::move(replacements);
                        configMtime = current;
                        configSeen = true;
                        std::cerr << "regions reload applied: " << configPath << '\n';
                    } else {
                        std::cerr << "regions reload rejected: " << reloadError << '\n';
                    }
                } else {
                    std::cerr << "regions reload rejected: " << reloadError << '\n';
                }
            }
        }
        VideoFrame frame; PoseventFrameResult result;
        if (!source.read(frame, error) || !runner.process(frame, result, error)) {
            std::cerr << error << '\n'; return 1;
        }
        if (sink && !sink->present(frame, result, error)) {
            std::cerr << error << '\n'; return 1;
        }
        std::cout << "frame=" << result.frameSeq << " detections="
                  << result.detections.size() << " events=" << result.events.size() << '\n';
        ++processed;
    }
    std::cerr << "stopped frames=" << processed << '\n';
}
