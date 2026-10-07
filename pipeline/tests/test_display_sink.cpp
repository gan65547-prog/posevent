#include "display_sink.h"

#include <cassert>
#include <fstream>
#include <iostream>

int main() {
    const std::string path = "posevent-display-test.svg";
    AtomicFileDisplaySink sink(path);
    std::string error;
    assert(sink.present("<svg data-frame=\"1\"/>", error));
    std::ifstream input(path);
    std::string content((std::istreambuf_iterator<char>(input)), {});
    assert(content.find("data-frame=\"1\"") != std::string::npos);
    std::remove(path.c_str());
    std::cout << "display sink: PASS\n";
}
