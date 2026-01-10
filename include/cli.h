#pragma once

#include <string>
#include <vector>

#include "app_config.h"

bool ParseArgs(const std::vector<std::string>& args, AppConfig* config, std::string* error);
std::string BuildUsage();
