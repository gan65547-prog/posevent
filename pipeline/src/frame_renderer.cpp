#include "frame_renderer.h"

#include <algorithm>

namespace {
void pixel(std::vector<uint8_t>& image, uint32_t width, uint32_t height,
           int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= width || static_cast<uint32_t>(y) >= height) return;
    const size_t offset = (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 3;
    image[offset] = r; image[offset + 1] = g; image[offset + 2] = b;
}
void line(std::vector<uint8_t>& image, uint32_t width, uint32_t height,
          int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        pixel(image, width, height, x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}
}

std::vector<uint8_t> renderRgb(const VideoFrame& frame,
                               const PoseventFrameResult& result) {
    const size_t size = static_cast<size_t>(frame.width) * frame.height * 3;
    std::vector<uint8_t> image = frame.pixels && frame.pixels->size() >= size
        ? std::vector<uint8_t>(frame.pixels->begin(), frame.pixels->begin() + size)
        : std::vector<uint8_t>(size, 0);
    auto coordinateX = [&frame](float value) { return static_cast<int>(value * frame.width); };
    auto coordinateY = [&frame](float value) { return static_cast<int>(value * frame.height); };
    for (const auto& detection : result.detections) {
        const int x1 = coordinateX(detection.x), y1 = coordinateY(detection.y);
        const int x2 = coordinateX(detection.x + detection.w), y2 = coordinateY(detection.y + detection.h);
        line(image, frame.width, frame.height, x1, y1, x2, y1, 0, 255, 0);
        line(image, frame.width, frame.height, x2, y1, x2, y2, 0, 255, 0);
        line(image, frame.width, frame.height, x2, y2, x1, y2, 0, 255, 0);
        line(image, frame.width, frame.height, x1, y2, x1, y1, 0, 255, 0);
    }
    for (const auto& overlay : result.overlays) {
        const uint8_t r = overlay.active ? 255 : 0;
        const uint8_t g = overlay.active ? 0 : 200;
        const uint8_t b = overlay.active ? 0 : 200;
        if (overlay.geometry.type == regionType::rect) {
            const int x1 = coordinateX(overlay.geometry.rx), y1 = coordinateY(overlay.geometry.ry);
            const int x2 = coordinateX(overlay.geometry.rx + overlay.geometry.rw);
            const int y2 = coordinateY(overlay.geometry.ry + overlay.geometry.rh);
            line(image, frame.width, frame.height, x1, y1, x2, y1, r, g, b);
            line(image, frame.width, frame.height, x2, y1, x2, y2, r, g, b);
            line(image, frame.width, frame.height, x2, y2, x1, y2, r, g, b);
            line(image, frame.width, frame.height, x1, y2, x1, y1, r, g, b);
        } else if (overlay.geometry.type == regionType::polygon) {
            for (size_t i = 0; i < overlay.geometry.poly.size(); ++i) {
                const auto& a = overlay.geometry.poly[i];
                const auto& z = overlay.geometry.poly[(i + 1) % overlay.geometry.poly.size()];
                line(image, frame.width, frame.height, coordinateX(a.x), coordinateY(a.y),
                     coordinateX(z.x), coordinateY(z.y), r, g, b);
            }
        }
    }
    return image;
}
