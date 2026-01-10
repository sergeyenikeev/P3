#include "exclude.h"

#include <sstream>
#include <vector>

#include "path_utils.h"

namespace {

bool GlobMatch(const std::string& pattern, const std::string& text) {
    size_t p = 0;
    size_t t = 0;
    size_t star = std::string::npos;
    size_t match = 0;

    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
            continue;
        }
        if (p < pattern.size() && pattern[p] == '*') {
            star = p;
            match = t;
            ++p;
            continue;
        }
        if (star != std::string::npos) {
            p = star + 1;
            ++match;
            t = match;
            continue;
        }
        return false;
    }

    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

std::vector<std::string> SplitPath(const std::string& value) {
    std::vector<std::string> parts;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, '/')) {
        if (!item.empty()) {
            parts.push_back(item);
        }
    }
    return parts;
}

}  // namespace

ExcludeRules BuildDefaultExcludeRules() {
    ExcludeRules rules;
    rules.patterns = {
        ".git",
        ".svn",
        ".hg",
        "Thumbs.db",
        "desktop.ini",
        ".DS_Store",
        "*.tmp",
        "*.temp",
        "*.swp",
        "*~"
    };
    return rules;
}

bool ShouldExclude(const std::filesystem::path& relative, const ExcludeRules& rules) {
    std::string rel = ToLowerAscii(PathToGenericUtf8(relative));
    std::vector<std::string> segments = SplitPath(rel);

    for (const auto& raw_pattern : rules.patterns) {
        std::string pattern = ToLowerAscii(raw_pattern);
        if (pattern.find('/') != std::string::npos) {
            if (GlobMatch(pattern, rel)) {
                return true;
            }
        } else {
            for (const auto& segment : segments) {
                if (GlobMatch(pattern, segment)) {
                    return true;
                }
            }
        }
    }

    return false;
}
