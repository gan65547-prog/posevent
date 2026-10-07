#include "config_reload_watcher.h"

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

int main() {
    const auto path = std::string("posevent-reload-test.json");
    { std::ofstream file(path); file << "valid-1"; }
    int applied = 0;
    ConfigReloadWatcher watcher(path, [&applied](const std::string&, std::string&) {
        ++applied;
        return true;
    });
    std::string error;
    assert(watcher.poll(error));
    assert(!watcher.poll(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    { std::ofstream file(path); file << "valid-2"; }
    assert(watcher.poll(error));
    assert(applied == 2);
    std::remove(path.c_str());
    std::cout << "config reload watcher: PASS\n";
}
