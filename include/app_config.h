#pragma once

#include <filesystem>
#include <string>
#include <vector>

enum class CompareMode {
    SizeMtime,
    SizeOnly
};

struct AppConfig {
    std::filesystem::path source;
    std::string remote = "/PublicUploadRoot";
    std::string email;
    std::string app_password;
    std::string base_url = "https://webdav.cloud.mail.ru";
    bool dry_run = false;
    int threads = 1;
    CompareMode compare_mode = CompareMode::SizeMtime;
    std::vector<std::string> excludes;
};
