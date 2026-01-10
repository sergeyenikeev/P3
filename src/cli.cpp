#include "cli.h"

#include <filesystem>
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

}  // namespace

std::string BuildUsage() {
    std::ostringstream oss;
    oss << "Usage:\n";
    oss << "  uploader.exe --source <path> --email <user@mail.ru> [options]\n\n";
    oss << "Required:\n";
    oss << "  --source <path>\n";
    oss << "  --email <email>\n\n";
    oss << "Options:\n";
    oss << "  --app-password <password>   App password (required unless --dry-run).\n";
    oss << "  --remote <path>             Remote root (default: /PublicUploadRoot).\n";
    oss << "  --base-url <url>            WebDAV base URL (default: https://webdav.cloud.mail.ru).\n";
    oss << "  --dry-run                   Show actions without uploading or deleting.\n";
    oss << "  --threads <n>               Number of worker threads (default: 1).\n";
    oss << "  --exclude <pattern>         Exclude glob pattern (repeatable).\n";
    oss << "  --compare <mode>            size-mtime (default) or size-only.\n";
    oss << "  --help                      Show this help.\n";
    return oss.str();
}

bool ParseArgs(const std::vector<std::string>& args, AppConfig* config, std::string* error) {
    if (!config) {
        if (error) {
            *error = "Internal error: config is null";
        }
        return false;
    }

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

    if (config->source.empty()) {
        if (error) {
            *error = "Missing --source";
        }
        return false;
    }
    if (config->email.empty()) {
        if (error) {
            *error = "Missing --email";
        }
        return false;
    }
    if (!config->dry_run && config->app_password.empty()) {
        if (error) {
            *error = "Missing --app-password (required for non dry-run)";
        }
        return false;
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
