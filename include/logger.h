#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

class Logger {
public:
    explicit Logger(const std::filesystem::path& log_dir);

    void Info(const std::string& message);
    void Warn(const std::string& message);
    void Error(const std::string& message);

    std::filesystem::path LogPath() const;

private:
    void Write(const std::string& level, const std::string& message);
    static std::string Timestamp();
    static std::filesystem::path BuildLogPath(const std::filesystem::path& log_dir);

    std::filesystem::path log_path_;
    std::ofstream file_;
    mutable std::mutex mutex_;
};
