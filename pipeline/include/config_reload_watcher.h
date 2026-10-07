#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

class ConfigReloadWatcher {
public:
    using ReloadCallback = std::function<bool(const std::string& path,
                                              std::string& error)>;

    ConfigReloadWatcher(std::string path, ReloadCallback callback)
        : path_(std::move(path)), callback_(std::move(callback)) {}

    // Poll once. Returns true only when a changed file was successfully applied.
    bool poll(std::string& error) {
        std::error_code statusError;
        const auto current = std::filesystem::last_write_time(path_, statusError);
        if (statusError) {
            error = "cannot stat config: " + statusError.message();
            return false;
        }
        if (seen_ && current == mtime_) return false;
        if (!callback_) {
            error = "reload callback is not configured";
            return false;
        }
        if (!callback_(path_, error)) return false;
        mtime_ = current;
        seen_ = true;
        return true;
    }

private:
    std::string path_;
    ReloadCallback callback_;
    std::filesystem::file_time_type mtime_{};
    bool seen_ = false;
};
