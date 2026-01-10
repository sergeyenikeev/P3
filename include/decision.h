#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include "app_config.h"

struct RemoteItemInfo {
    bool exists = false;
    bool is_dir = false;
    bool has_size = false;
    std::uint64_t size = 0;
    bool has_last_modified = false;
    std::chrono::system_clock::time_point last_modified{};
    std::string etag;
};

struct LocalFileInfo {
    std::filesystem::path path;
    std::uint64_t size = 0;
    std::chrono::system_clock::time_point last_modified{};
    bool is_jpg = false;
};

enum class FileActionType {
    Skip,
    Upload,
    UploadAndDelete
};

struct FileDecision {
    FileActionType action = FileActionType::Skip;
    std::string reason;
};

bool IsDifferent(const LocalFileInfo& local,
                 const RemoteItemInfo& remote,
                 CompareMode mode);

FileDecision DecideFileAction(const LocalFileInfo& local,
                              const RemoteItemInfo& remote,
                              CompareMode mode,
                              std::chrono::system_clock::time_point run_start);

bool IsOlderThan24Hours(const LocalFileInfo& local,
                        std::chrono::system_clock::time_point run_start);
