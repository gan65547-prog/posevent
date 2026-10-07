#include "osd_renderer.h"

#include <iomanip>
#include <sstream>

namespace {
int px(float value, uint32_t extent) {
    return static_cast<int>(value * static_cast<float>(extent) + 0.5F);
}
}

std::string renderSvg(const PoseventFrameResult& result, uint32_t width,
                      uint32_t height)
{
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\" data-frame=\"" << result.frameSeq << "\">\n";
    for (const auto& detection : result.detections) {
        svg << "<rect x=\"" << px(detection.x, width) << "\" y=\""
            << px(detection.y, height) << "\" width=\"" << px(detection.w, width)
            << "\" height=\"" << px(detection.h, height)
            << "\" fill=\"none\" stroke=\"#00ff00\" stroke-width=\"2\"/>\n";
    }
    for (const auto& overlay : result.overlays) {
        const char* color = overlay.active ? "#ff0000" : "#00c8c8";
        svg << "<g data-region-id=\"" << overlay.regionId << "\" data-active=\""
            << (overlay.active ? "true" : "false") << "\" stroke=\"" << color
            << "\" fill=\"none\" stroke-width=\"3\">\n";
        if (overlay.geometry.type == regionType::rect) {
            svg << "<rect x=\"" << px(overlay.geometry.rx, width) << "\" y=\""
                << px(overlay.geometry.ry, height) << "\" width=\""
                << px(overlay.geometry.rw, width) << "\" height=\""
                << px(overlay.geometry.rh, height) << "\"/>\n";
        } else if (overlay.geometry.type == regionType::polygon) {
            svg << "<polygon points=\"";
            for (const auto& point : overlay.geometry.poly) {
                svg << px(point.x, width) << "," << px(point.y, height) << " ";
            }
            svg << "\"/>\n";
        }
        svg << "</g>\n";
    }
    svg << "</svg>\n";
    return svg.str();
}
