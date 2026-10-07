#include "rtsp_source.h"

#include <cassert>
#include <iostream>

int main() {
    RtspSource source({"", 640, 480, 0, 0});
    std::string error;
    assert(!source.open(error));
    assert(error.find("URL") != std::string::npos);
    std::cout << "rtsp source validation: PASS\n";
}
