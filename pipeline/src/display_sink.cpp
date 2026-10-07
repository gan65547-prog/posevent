#include "display_sink.h"

#include <cstdio>
#include <fstream>

bool AtomicFileDisplaySink::present(const std::string& renderedFrame,
                                    std::string& error) {
    const std::string temporary = path_ + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { error = "cannot open display output"; return false; }
        output << renderedFrame;
        output.flush();
        if (!output) { error = "cannot write display output"; return false; }
    }
    if (std::rename(temporary.c_str(), path_.c_str()) != 0) {
        error = "cannot atomically publish display output";
        return false;
    }
    return true;
}
