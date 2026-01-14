#include "cli.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "config_defaults.h"
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

bool IsPlaceholderValue(const std::string& value) {
    return ToLowerAscii(value) == "test";
}

std::string NormalizeDefaultValue(const char* value) {
    std::string out = value ? value : "";
    if (out.empty()) {
        return out;
    }
    if (IsPlaceholderValue(out)) {
        return {};
    }
    return out;
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

std::vector<std::string> SplitList(const std::string& value) {
    std::vector<std::string> result;
    std::string current;
    for (char c : value) {
        if (c == ';' || c == ',') {
            std::string trimmed = Trim(current);
            if (!trimmed.empty()) {
                result.push_back(trimmed);
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    std::string trimmed = Trim(current);
    if (!trimmed.empty()) {
        result.push_back(trimmed);
    }
    return result;
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

struct ConfigFileData {
    std::filesystem::path source;
    bool has_source = false;
    std::string remote;
    bool has_remote = false;
    std::string base_url;
    bool has_base_url = false;
    int threads = 1;
    bool has_threads = false;
    CompareMode compare_mode = CompareMode::SizeMtime;
    bool has_compare = false;
    bool dry_run = false;
    bool has_dry_run = false;
    std::vector<std::string> excludes;
    std::string email;
    std::string app_password;
};

bool ParseBoolValue(const std::string& value, bool* out) {
    std::string lower = ToLowerAscii(Trim(value));
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        if (out) {
            *out = true;
        }
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        if (out) {
            *out = false;
        }
        return true;
    }
    return false;
}

bool LoadConfigFile(const std::filesystem::path& path,
                    ConfigFileData* out,
                    std::string* error) {
    if (!out) {
        if (error) {
            *error = "Internal error: config output is null";
        }
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
        std::string value_lower = ToLowerAscii(value);
        if (value_lower == "test") {
            continue;
        }
        std::string key_lower = ToLowerAscii(key);
        if (key_lower == "email") {
            out->email = value;
        } else if (key_lower == "app_password" || key_lower == "app-password") {
            out->app_password = value;
        } else if (key_lower == "source") {
            out->source = std::filesystem::path(value);
            out->has_source = true;
        } else if (key_lower == "remote") {
            out->remote = value;
            out->has_remote = true;
        } else if (key_lower == "base_url" || key_lower == "base-url") {
            out->base_url = value;
            out->has_base_url = true;
        } else if (key_lower == "threads") {
            try {
                out->threads = std::stoi(value);
                out->has_threads = true;
            } catch (...) {
                if (error) {
                    *error = "Invalid threads value in config: " + value;
                }
                return false;
            }
        } else if (key_lower == "compare") {
            std::string mode = ToLowerAscii(value);
            if (mode == "size-mtime") {
                out->compare_mode = CompareMode::SizeMtime;
                out->has_compare = true;
            } else if (mode == "size-only") {
                out->compare_mode = CompareMode::SizeOnly;
                out->has_compare = true;
            } else {
                if (error) {
                    *error = "Invalid compare value in config: " + value;
                }
                return false;
            }
        } else if (key_lower == "dry_run" || key_lower == "dry-run") {
            bool parsed = false;
            if (!ParseBoolValue(value, &parsed)) {
                if (error) {
                    *error = "Invalid dry_run value in config: " + value;
                }
                return false;
            }
            out->dry_run = parsed;
            out->has_dry_run = true;
        } else if (key_lower == "exclude") {
            if (!value.empty()) {
                out->excludes.push_back(value);
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
    oss << "  <exe_dir>\\uploader.conf with email/app_password/source/remote/base_url/threads/compare/dry_run/exclude.\n";
    oss << "Compiled defaults:\n";
    oss << "  set via CMake cache DEFAULT_* variables.\n";
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
    bool remote_set = false;
    bool base_url_set = false;
    bool threads_set = false;
    bool compare_set = false;
    bool dry_run_set = false;
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
            remote_set = true;
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
            base_url_set = true;
            continue;
        }
        if (IsFlag(arg, "--dry-run")) {
            config->dry_run = true;
            dry_run_set = true;
            continue;
        }
        if (IsFlag(arg, "--threads")) {
            std::string value;
            if (!ReadValue(args, &i, &value, error)) {
                return false;
            }
            try {
                config->threads = std::stoi(value);
                threads_set = true;
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
                compare_set = true;
            } else if (mode == "size-only") {
                config->compare_mode = CompareMode::SizeOnly;
                compare_set = true;
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

    std::filesystem::path config_root = default_source_root;
    if (config_root.empty()) {
        config_root = std::filesystem::current_path();
    }
    std::filesystem::path config_path = config_root / "uploader.conf";
    std::error_code config_ec;
    if (std::filesystem::exists(config_path, config_ec)) {
        if (config_ec) {
            if (error) {
                *error = "Failed to access config file: " + config_path.string();
            }
            return false;
        }
        ConfigFileData file_data;
        std::string cfg_error;
        if (!LoadConfigFile(config_path, &file_data, &cfg_error)) {
            if (error) {
                *error = cfg_error;
            }
            return false;
        }
        if (!source_set && file_data.has_source) {
            std::filesystem::path cfg_source = file_data.source;
            if (cfg_source.is_relative()) {
                cfg_source = config_root / cfg_source;
            }
            config->source = cfg_source;
            source_set = true;
        }
        if (!remote_set && file_data.has_remote) {
            config->remote = file_data.remote;
            remote_set = true;
        }
        if (!base_url_set && file_data.has_base_url) {
            config->base_url = file_data.base_url;
            base_url_set = true;
        }
        if (!threads_set && file_data.has_threads) {
            config->threads = file_data.threads;
            threads_set = true;
        }
        if (!compare_set && file_data.has_compare) {
            config->compare_mode = file_data.compare_mode;
            compare_set = true;
        }
        if (!dry_run_set && file_data.has_dry_run) {
            config->dry_run = file_data.dry_run;
            dry_run_set = true;
        }
        if (config->email.empty() && !file_data.email.empty()) {
            config->email = file_data.email;
        }
        if (config->app_password.empty() && !file_data.app_password.empty()) {
            config->app_password = file_data.app_password;
        }
        for (const auto& pattern : file_data.excludes) {
            config->excludes.push_back(pattern);
        }
    } else if (config_ec) {
        if (error) {
            *error = "Failed to access config file: " + config_path.string();
        }
        return false;
    }

    if (config->email.empty()) {
        config->email = GetEnvValue("MAILRU_EMAIL");
    }
    if (config->app_password.empty()) {
        config->app_password = GetEnvValue("MAILRU_APP_PASSWORD");
    }

    std::string default_email = NormalizeDefaultValue(DEFAULT_EMAIL);
    if (config->email.empty() && !default_email.empty()) {
        config->email = default_email;
    }
    std::string default_password = NormalizeDefaultValue(DEFAULT_APP_PASSWORD);
    if (config->app_password.empty() && !default_password.empty()) {
        config->app_password = default_password;
    }

    if (!source_set) {
        std::string default_source = NormalizeDefaultValue(DEFAULT_SOURCE);
        if (!default_source.empty()) {
            std::filesystem::path src = std::filesystem::path(default_source);
            if (src.is_relative()) {
                src = config_root / src;
            }
            config->source = src;
            source_set = true;
        }
    }
    if (!remote_set) {
        std::string default_remote = NormalizeDefaultValue(DEFAULT_REMOTE);
        if (!default_remote.empty()) {
            config->remote = default_remote;
            remote_set = true;
        }
    }
    if (!base_url_set) {
        std::string default_base_url = NormalizeDefaultValue(DEFAULT_BASE_URL);
        if (!default_base_url.empty()) {
            config->base_url = default_base_url;
            base_url_set = true;
        }
    }
    if (!threads_set && DEFAULT_THREADS != 0) {
        if (DEFAULT_THREADS > 0) {
            config->threads = DEFAULT_THREADS;
            threads_set = true;
        } else {
            if (error) {
                *error = "Invalid default threads value";
            }
            return false;
        }
    }
    if (!compare_set) {
        std::string default_compare = NormalizeDefaultValue(DEFAULT_COMPARE);
        if (!default_compare.empty()) {
            std::string mode = ToLowerAscii(default_compare);
            if (mode == "size-mtime") {
                config->compare_mode = CompareMode::SizeMtime;
                compare_set = true;
            } else if (mode == "size-only") {
                config->compare_mode = CompareMode::SizeOnly;
                compare_set = true;
            } else {
                if (error) {
                    *error = "Invalid compare value in defaults: " + default_compare;
                }
                return false;
            }
        }
    }
    if (!dry_run_set && DEFAULT_DRY_RUN != -1) {
        if (DEFAULT_DRY_RUN == 0 || DEFAULT_DRY_RUN == 1) {
            config->dry_run = (DEFAULT_DRY_RUN == 1);
            dry_run_set = true;
        } else {
            if (error) {
                *error = "Invalid dry_run value in defaults";
            }
            return false;
        }
    }
    std::string default_excludes = NormalizeDefaultValue(DEFAULT_EXCLUDES);
    if (!default_excludes.empty()) {
        for (const auto& pattern : SplitList(default_excludes)) {
            config->excludes.push_back(pattern);
        }
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

    if (!config->app_password.empty() && config->email.empty()) {
        if (error) {
            *error = "--app-password requires --email";
        }
        return false;
    }
    if (!config->dry_run) {
        if (config->email.empty()) {
            if (error) {
                *error = "Missing --email (or MAILRU_EMAIL/uploader.conf)";
            }
            return false;
        }
        if (config->app_password.empty()) {
            if (error) {
                *error = "Missing --app-password (or MAILRU_APP_PASSWORD/uploader.conf)";
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
