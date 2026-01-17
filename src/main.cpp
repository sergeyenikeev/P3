#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

#include <windows.h>

#include "cli.h"
#include "logger.h"
#include "sync_engine.h"

namespace {

std::filesystem::path GetExecutableDir() {
    std::wstring buffer;
    buffer.resize(32768);
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(),
                                   static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size()) {
        return std::filesystem::current_path();
    }
    buffer.resize(size);
    std::filesystem::path exe_path(buffer);
    return exe_path.parent_path();
}

std::string JoinList(const std::vector<std::string>& items, const std::string& sep) {
    std::ostringstream oss;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            oss << sep;
        }
        oss << items[i];
    }
    return oss.str();
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    AppConfig config;
    std::string error;
    std::filesystem::path exe_dir = GetExecutableDir();
    if (!ParseArgs(args, exe_dir, &config, &error)) {
        if (!error.empty()) {
            std::cerr << "Error: " << error << "\n\n";
        }
        std::cerr << BuildUsage() << "\n";
        return 1;
    }

    Logger logger("logs");
    logger.Info("Start");
    logger.Info("Log file: " + logger.LogPath().string());
    logger.Info("Mode: " + std::string(config.dry_run ? "dry-run" : "sync"));
    logger.Info("Dry-run: " + std::string(config.dry_run ? "true" : "false"));
    logger.Info("Source: " + config.source.string());
    logger.Info("Remote root: " + config.remote);
    logger.Info("Target URL: " + config.base_url + config.remote);
    logger.Info("Email: " + config.email);
    logger.Info("Base URL: " + config.base_url);
    logger.Info("Threads: " + std::to_string(config.threads));
    logger.Info("Compare: " + std::string(config.compare_mode == CompareMode::SizeOnly
                                              ? "size-only"
                                              : "size-mtime"));
    logger.Info("Excludes: " + (config.excludes.empty() ? "(none)" : JoinList(config.excludes, ";")));

    std::filesystem::path config_path = exe_dir / "uploader.conf";
    std::error_code ec;
    bool config_exists = std::filesystem::exists(config_path, ec) && !ec;
    logger.Info("Config file: " + config_path.string() + " (" +
                (config_exists ? "found" : "absent") + ")");

    SyncStats stats = RunSync(config, logger);

    logger.Info("Summary:");
    logger.Info("  Dirs created: " + std::to_string(stats.dirs_created));
    logger.Info("  Files uploaded: " + std::to_string(stats.files_uploaded));
    logger.Info("  Files deleted (jpg): " + std::to_string(stats.files_deleted_jpg));
    logger.Info("  Files deleted (>24h): " + std::to_string(stats.files_deleted_old));
    logger.Info("  Files skipped: " + std::to_string(stats.files_skipped));
    logger.Info("  Errors: " + std::to_string(stats.errors));

    if (!stats.deleted_files.empty()) {
        logger.Info("Deleted local files:");
        for (const auto& file : stats.deleted_files) {
            logger.Info("  " + file);
        }
    }

    logger.Info("Finish");

    return stats.errors == 0 ? 0 : 1;
}
