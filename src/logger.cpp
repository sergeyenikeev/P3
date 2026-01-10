#include "logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

Logger::Logger(const std::filesystem::path& log_dir) {
    std::filesystem::create_directories(log_dir);
    log_path_ = BuildLogPath(log_dir);
    file_.open(log_path_, std::ios::out | std::ios::app);
}

void Logger::Info(const std::string& message) {
    Write("INFO", message);
}

void Logger::Warn(const std::string& message) {
    Write("WARN", message);
}

void Logger::Error(const std::string& message) {
    Write("ERROR", message);
}

std::filesystem::path Logger::LogPath() const {
    return log_path_;
}

void Logger::Write(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string line = Timestamp() + " [" + level + "] " + message + "\n";
    if (file_.is_open()) {
        file_ << line;
        file_.flush();
    }
    if (level == "ERROR") {
        std::cerr << line;
    } else {
        std::cout << line;
    }
}

std::string Logger::Timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &now_c);
#else
    localtime_r(&now_c, &local_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::filesystem::path Logger::BuildLogPath(const std::filesystem::path& log_dir) {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &now_c);
#else
    localtime_r(&now_c, &local_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d") << ".log";
    return log_dir / oss.str();
}
