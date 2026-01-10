#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "app_config.h"

bool ParseArgs(const std::vector<std::string>& args,
               const std::filesystem::path& default_source_root,
               AppConfig* config,
               std::string* error);
std::string BuildUsage();
