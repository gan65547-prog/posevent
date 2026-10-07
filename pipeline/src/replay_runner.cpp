#include "posevent_engine.h"

#include <fstream>
#include <iostream>
#include <regex>
#include <string>

// Offline evidence runner. It deliberately uses the same C++ core as a live
// model adapter; it is not a second production business implementation.
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: posevent_replay <detections.jsonl> <results.jsonl>\n";
        return 2;
    }
    std::ifstream input(argv[1]);
    std::ofstream output(argv[2]);
    if (!input || !output) {
        std::cerr << "cannot open replay files\n";
        return 2;
    }

    ZoneDefinition zone;
    zone.geometry.id = 1;
    zone.geometry.name = "entrance";
    zone.geometry.type = regionType::rect;
    zone.geometry.rx = 0.2F; zone.geometry.ry = 0.2F;
    zone.geometry.rw = 0.4F; zone.geometry.rh = 0.5F;
    zone.classes = {"person"};
    zone.minConfidence = 0.25F;
    PoseventEngine engine({zone}, {3, 5, 10});

    const std::regex seqPattern(R"REGEX("(?:seq|frame_seq)"\s*:\s*(\d+))REGEX");
    const std::regex detectionPattern(
        R"REGEX("class"\s*:\s*"([^"]+)"\s*,\s*"score"\s*:\s*([0-9.]+)\s*,\s*"box"\s*:\s*\[\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*([0-9.]+))REGEX");
    std::string line;
    while (std::getline(input, line)) {
        std::smatch seqMatch;
        if (!std::regex_search(line, seqMatch, seqPattern)) {
            std::cerr << "invalid replay frame: missing seq\n";
            return 1;
        }
        const uint64_t seq = std::stoull(seqMatch[1].str());
        std::vector<Detection> detections;
        for (auto begin = std::sregex_iterator(line.begin(), line.end(), detectionPattern),
                  end = std::sregex_iterator(); begin != end; ++begin) {
            const auto& match = *begin;
            detections.push_back({std::stof(match[3]), std::stof(match[4]),
                                  std::stof(match[5]), std::stof(match[6]),
                                  std::stof(match[2]), match[1]});
        }
        const auto result = engine.process(detections, seq);
        output << "{\"frame_seq\":" << result.frameSeq << ",\"detections\":"
               << result.detections.size() << ",\"events\":[";
        for (size_t i = 0; i < result.events.size(); ++i) {
            if (i) output << ',';
            output << "{\"event\":\"" << result.events[i].type
                   << "\",\"region_id\":" << result.events[i].regionId
                   << ",\"frame\":" << result.events[i].frame << "}";
        }
        output << "],\"overlays\":[";
        for (size_t i = 0; i < result.overlays.size(); ++i) {
            if (i) output << ',';
            output << "{\"region_id\":" << result.overlays[i].regionId
                   << ",\"active\":"
                   << (result.overlays[i].active ? "true" : "false") << "}";
        }
        output << "]}\n";
    }
}
