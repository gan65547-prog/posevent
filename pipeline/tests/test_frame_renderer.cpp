#include "frame_renderer.h"

#include <cassert>
#include <iostream>

int main() {
    auto pixels = std::make_shared<std::vector<uint8_t>>(100 * 100 * 3, 0);
    VideoFrame frame{1, 1, 100, 100, pixels};
    ZoneOverlay overlay;
    overlay.regionId = 1;
    overlay.active = true;
    overlay.geometry.type = regionType::rect;
    overlay.geometry.rx = .2F; overlay.geometry.ry = .2F;
    overlay.geometry.rw = .6F; overlay.geometry.rh = .6F;
    PoseventFrameResult result;
    result.frameSeq = 1;
    result.width = 100; result.height = 100;
    result.overlays.push_back(overlay);
    const auto output = renderRgb(frame, result);
    const size_t border = (20U * 100U + 20U) * 3U;
    assert(output[border] == 255 && output[border + 1] == 0 && output[border + 2] == 0);
    std::cout << "frame renderer: PASS\n";
}
