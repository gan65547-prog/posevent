#include "framebuffer_sink.h"

#include <iostream>

int main(int argc, char** argv) {
    const std::string device = argc > 1 ? argv[1] : "/dev/fb0";
    auto pixels = std::make_shared<std::vector<uint8_t>>(1920U * 1080U * 3U, 0);
    VideoFrame frame{1, 1, 1920, 1080, pixels};
    PoseventFrameResult result;
    result.frameSeq = 1;
    ZoneOverlay overlay;
    overlay.regionId = 1;
    overlay.active = true;
    overlay.geometry.type = regionType::rect;
    overlay.geometry.rx = .2F; overlay.geometry.ry = .2F;
    overlay.geometry.rw = .6F; overlay.geometry.rh = .6F;
    result.overlays.push_back(overlay);
    FramebufferSink sink(device);
    std::string error;
    if (!sink.present(frame, result, error)) { std::cerr << error << '\n'; return 1; }
    std::cout << "framebuffer smoke: PASS device=" << device << "\n";
}
