#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct ExcludeRules {
    std::vector<std::string> patterns;
};

ExcludeRules BuildDefaultExcludeRules();
bool ShouldExclude(const std::filesystem::path& relative, const ExcludeRules& rules);
