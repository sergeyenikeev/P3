#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app_config.h"
#include "logger.h"

struct SyncStats {
    std::uint64_t dirs_created = 0;
    std::uint64_t files_uploaded = 0;
    std::uint64_t files_deleted_jpg = 0;
    std::uint64_t files_deleted_old = 0;
    std::uint64_t files_skipped = 0;
    std::uint64_t errors = 0;
    std::vector<std::string> deleted_files;
};

SyncStats RunSync(const AppConfig& config, Logger& logger);
