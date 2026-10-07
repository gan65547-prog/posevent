#include "rtsp_source.h"

#include <cassert>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    RtspSource source({argv[1], 320, 240, 2, 10});
    std::string error;
    assert(source.open(error));
    VideoFrame first;
    VideoFrame second;
    assert(source.read(first, error));
    assert(source.read(second, error));
    assert(first.width == 320 && first.height == 240);
    assert(first.pixels && first.pixels->size() == 320U * 240U * 3U);
    assert(second.frameSeq == first.frameSeq + 1);
    std::cout << "live rtsp source: PASS frames=" << source.frameCount() << "\n";
}
