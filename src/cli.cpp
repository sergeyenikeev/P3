#include "cli.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "path_utils.h"

namespace {

bool IsFlag(const std::string& value, const std::string& flag) {
    return value == flag;
}

bool ReadValue(const std::vector<std::string>& args,
               size_t* index,
               std::string* out,
               std::string* error) {
    if (*index + 1 >= args.size()) {
        if (error) {
            *error = "Missing value for " + args[*index];
        }
        return false;
    }
    *out = args[*index + 1];
    *index += 1;
    return true;
}

std::string GetEnvValue(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

std::string Trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        start++;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }
    return value.substr(start, end - start);
}

void StripBom(std::string* value) {
    if (!value) {
        return;
    }
    const std::string bom = "\xEF\xBB\xBF";
    if (value->compare(0, bom.size(), bom) == 0) {
        value->erase(0, bom.size());
    }
}

bool LoadCredentialsFile(const std::filesystem::path& path,
                         std::string* email,
                         std::string* app_password,
                         std::string* error) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return false;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        if (error) {
            *error = "Failed to open config file: " + path.string();
        }
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        auto pos = trimmed.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = Trim(trimmed.substr(0, pos));
        std::string value = Trim(trimmed.substr(pos + 1));
        if (key.empty()) {
            continue;
        }
        StripBom(&key);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        std::string key_lower = ToLowerAscii(key);
        if (key_lower == "email") {
            if (email && email->empty()) {
                *email = value;
            }
        } else if (key_lower == "app_password" || key_lower == "app-password") {
            if (app_password && app_password->empty()) {
                *app_password = value;
            }
        }
    }

    return true;
}

}  // namespace

std::string BuildUsage() {
    std::ostringstream oss;
    oss << "Usage:\n";
    oss << "  uploader.exe [options]\n\n";
    oss << "Required for sync (non --dry-run):\n";
    oss << "  --email <email>\n";
    oss << "  --app-password <password>\n\n";
    oss << "Defaults:\n";
    oss << "  --source <exe_dir>\\p\n\n";
    oss << "Config file:\n";
    oss << "  <exe_dir>\\uploader.conf with email/app_password.\n";
    oss << "Environment:\n";
    oss << "  MAILRU_EMAIL and MAILRU_APP_PASSWORD can provide credentials.\n\n";
    oss << "Options:\n";
    oss << "  --source <path>             Source directory.\n";
    oss << "  --app-password <password>   App password (required for sync).\n";
    oss << "  --remote <path>             Remote root (default: /PublicUploadRoot).\n";
    oss << "  --base-url <url>            WebDAV base URL (default: https://webdav.cloud.mail.ru).\n";
    oss << "  --dry-run                   Show actions without uploading or deleting.\n";
    oss << "  --threads <n>               Number of worker threads (default: 1).\n";
    oss << "  --exclude <pattern>         Exclude glob pattern (repeatable).\n";
    oss << "  --compare <mode>            size-mtime (default) or size-only.\n";
    oss << "  --help                      Show this help.\n";
    return oss.str();
}

bool ParseArgs(const std::vector<std::string>& args,
               const std::filesystem::path& default_source_root,
               AppConfig* config,
               std::string* error) {
    if (!config) {
        if (error) {
            *error = "Internal error: config is null";
        }
        return false;
    }

    bool source_set = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (IsFlag(arg, "--help") || IsFlag(arg, "-h")) {
            if (error) {
                error->clear();
            }
            return false;
        }
        if (IsFlag(arg, "--source")) {
            std::string value;
            if (!ReadValue(args, &i, &value, error)) {
                return false;
            }
            config->source = std::filesystem::path(value);
            source_set = true;
            continue;
        }
        if (IsFlag(arg, "--remote")) {
            std::string value;
            if (!ReadValue(args, &i, &value, error)) {
                return false;
            }
            config->remote = value;
            continue;
        }
        if (IsFlag(arg, "--email")) {
            std::string value;
            if (!ReadValue(args, &i, &value, error)) {
                return false;
            }
            config->email = value;
            continue;
        }
        if (IsFlag(arg, "--app-password")) {
            std::string value;
            if (!ReadValue(args, &i, &value, error)) {
                return false;
            }
            config->app_password = value;
            continue;
        }
        if (IsFlag(arg, "--base-url")) {
            std::string value;
            if (!ReadValue(args, &i, &value, error)) {
                return false;
            }
            config->base_url = value;
            continue;
        }
        if (IsFlag(arg, "--dry-run")) {
            config->dry_run = true;
            continue;
        }
        if (IsFlag(arg, "--threads")) {
            std::string value;
            if (!ReadValue(args, &i, &value, error)) {
                return false;
            }
            try {
                config->threads = std::stoi(value);
            } catch (...) {
                if (error) {
                    *error = "Invalid threads value: " + value;
                }
                return false;
            }
            continue;
        }
        if (IsFlag(arg, "--exclude")) {
            std::string value;
            if (!ReadValue(args, &i, &value, error)) {
                return false;
            }
            config->excludes.push_back(value);
            continue;
        }
        if (IsFlag(arg, "--compare")) {
            std::string value;
            if (!ReadValue(args, &i, &value, error)) {
                return false;
            }
            std::string mode = ToLowerAscii(value);
            if (mode == "size-mtime") {
                config->compare_mode = CompareMode::SizeMtime;
            } else if (mode == "size-only") {
                config->compare_mode = CompareMode::SizeOnly;
            } else {
                if (error) {
                    *error = "Unknown compare mode: " + value;
                }
                return false;
            }
            continue;
        }

        if (error) {
            *error = "Unknown argument: " + arg;
        }
        return false;
    }

    if (!source_set) {
        std::filesystem::path base = default_source_root;
        if (base.empty()) {
            base = std::filesystem::current_path();
        }
        config->source = base / "p";
        std::error_code create_ec;
        std::filesystem::create_directories(config->source, create_ec);
        if (create_ec) {
            if (error) {
                *error = "Failed to create default source dir: " + config->source.string();
            }
            return false;
        }
    }

    std::filesystem::path config_root = default_source_root;
    if (config_root.empty()) {
        config_root = std::filesystem::current_path();
    }
    std::filesystem::path config_path = config_root / "uploader.conf";
    if (config->email.empty() || config->app_password.empty()) {
        std::string cfg_error;
        LoadCredentialsFile(config_path, &config->email, &config->app_password, &cfg_error);
        if (!cfg_error.empty()) {
            if (error) {
                *error = cfg_error;
            }
            return false;
        }
    }

    if (config->email.empty()) {
        config->email = GetEnvValue("MAILRU_EMAIL");
    }
    if (config->app_password.empty()) {
        config->app_password = GetEnvValue("MAILRU_APP_PASSWORD");
    }

    if (!config->app_password.empty() && config->email.empty()) {
        if (error) {
            *error = "--app-password requires --email";
        }
        return false;
    }
    if (!config->dry_run) {
        if (config->email.empty()) {
            if (error) {
                *error = "Missing --email (or MAILRU_EMAIL)";
            }
            return false;
        }
        if (config->app_password.empty()) {
            if (error) {
                *error = "Missing --app-password (or MAILRU_APP_PASSWORD)";
            }
            return false;
        }
    }
    if (config->threads < 1) {
        if (error) {
            *error = "--threads must be >= 1";
        }
        return false;
    }
    if (!std::filesystem::exists(config->source)) {
        if (error) {
            *error = "Source path does not exist: " + config->source.string();
        }
        return false;
    }
    if (!std::filesystem::is_directory(config->source)) {
        if (error) {
            *error = "Source path is not a directory: " + config->source.string();
        }
        return false;
    }

    config->remote = NormalizeRemoteRoot(config->remote);
    config->source = std::filesystem::absolute(config->source);

    return true;
}
