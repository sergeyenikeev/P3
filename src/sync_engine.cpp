#include "sync_engine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "decision.h"
#include "exclude.h"
#include "path_utils.h"
#include "webdav_client.h"

namespace {

struct FileEntry {
    std::filesystem::path abs_path;
    std::filesystem::path rel_path;
};

std::chrono::system_clock::time_point FileTimeToSystemClock(
    const std::filesystem::file_time_type& ft) {
    auto now_sys = std::chrono::system_clock::now();
    auto now_file = std::filesystem::file_time_type::clock::now();
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - now_file + now_sys);
}

bool IsJpgFile(const std::filesystem::path& path) {
    std::string ext = ToLowerAscii(path.extension().string());
    return ext == ".jpg";
}

int PathDepth(const std::filesystem::path& path) {
    return static_cast<int>(std::distance(path.begin(), path.end()));
}

std::vector<std::string> SplitRemotePath(const std::string& remote_path) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : remote_path) {
        if (c == '/') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

}  // namespace

SyncStats RunSync(const AppConfig& config, Logger& logger) {
    SyncStats stats;
    auto run_start = std::chrono::system_clock::now();

    ExcludeRules rules = BuildDefaultExcludeRules();
    for (const auto& pattern : config.excludes) {
        rules.patterns.push_back(pattern);
    }

    bool remote_checks = !config.app_password.empty();
    if (config.dry_run && !remote_checks) {
        logger.Warn("Dry-run without app password: remote checks are disabled.");
    }

    std::string url_error;
    auto base_url = WebDavClient::ParseBaseUrl(config.base_url, &url_error);
    if (!base_url) {
        logger.Error("Invalid base URL: " + url_error);
        stats.errors++;
        return stats;
    }

    WebDavCredentials creds{config.email, config.app_password};

    std::vector<std::filesystem::path> directories;
    std::vector<FileEntry> files;

    std::error_code ec;
    std::filesystem::recursive_directory_iterator iter(
        config.source, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::recursive_directory_iterator end;
    for (; iter != end; iter.increment(ec)) {
        if (ec) {
            logger.Error("Directory iteration error: " + ec.message());
            stats.errors++;
            ec.clear();
            continue;
        }

        const auto& entry = *iter;
        std::filesystem::path rel = std::filesystem::relative(entry.path(), config.source, ec);
        if (ec) {
            logger.Error("Failed to build relative path: " + ec.message());
            stats.errors++;
            ec.clear();
            continue;
        }

        if (ShouldExclude(rel, rules)) {
            if (entry.is_directory()) {
                iter.disable_recursion_pending();
            }
            continue;
        }

        if (entry.is_directory()) {
            directories.push_back(rel);
        } else if (entry.is_regular_file()) {
            files.push_back({entry.path(), rel});
        }
    }

    std::sort(directories.begin(), directories.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return PathDepth(a) < PathDepth(b);
              });

    std::unordered_set<std::string> known_dirs;
    auto ensure_dir = [&](WebDavClient* client, const std::string& remote_path) {
        std::string normalized = NormalizeRemoteRoot(remote_path);
        std::vector<std::string> parts = SplitRemotePath(normalized);
        std::string current;
        for (const auto& part : parts) {
            current += "/" + part;
            if (known_dirs.find(current) != known_dirs.end()) {
                continue;
            }

            if (config.dry_run) {
                bool exists = false;
                if (client && remote_checks) {
                    std::string err;
                    RemoteItemInfo info = client->GetInfo(current, &err);
                    if (!err.empty()) {
                        logger.Error("PROPFIND failed for " + current + ": " + err);
                        stats.errors++;
                    }
                    exists = info.exists;
                }
                if (!exists) {
                    logger.Info("Dry-run: would create directory " + current);
                    stats.dirs_created++;
                }
                known_dirs.insert(current);
                continue;
            }

            if (!client) {
                logger.Error("WebDAV client not available for directory " + current);
                stats.errors++;
                continue;
            }

            bool created = false;
            std::string err;
            if (!client->MkCol(current, &created, &err)) {
                logger.Error("MKCOL failed for " + current + ": " + err);
                stats.errors++;
                continue;
            }
            if (created) {
                stats.dirs_created++;
                logger.Info("Created directory " + current);
            }
            known_dirs.insert(current);
        }
    };

    std::unique_ptr<WebDavClient> dir_client;
    if (remote_checks) {
        dir_client = std::make_unique<WebDavClient>(*base_url, creds);
        if (!dir_client->IsReady()) {
            logger.Error("Failed to initialize WebDAV client for directories.");
            stats.errors++;
            return stats;
        }
    }

    ensure_dir(dir_client.get(), config.remote);
    for (const auto& dir : directories) {
        std::string remote_path = JoinRemotePath(config.remote, dir);
        ensure_dir(dir_client.get(), remote_path);
    }

    std::mutex stats_mutex;
    auto add_deleted = [&](const std::string& path, bool is_jpg, bool old_file) {
        std::lock_guard<std::mutex> lock(stats_mutex);
        stats.deleted_files.push_back(path);
        if (is_jpg) {
            stats.files_deleted_jpg++;
        } else if (old_file) {
            stats.files_deleted_old++;
        }
    };

    auto add_uploaded = [&]() {
        std::lock_guard<std::mutex> lock(stats_mutex);
        stats.files_uploaded++;
    };

    auto add_skipped = [&]() {
        std::lock_guard<std::mutex> lock(stats_mutex);
        stats.files_skipped++;
    };

    auto add_error = [&]() {
        std::lock_guard<std::mutex> lock(stats_mutex);
        stats.errors++;
    };

    std::atomic<size_t> next_index{0};
    int thread_count = std::max(1, config.threads);
    if (thread_count > static_cast<int>(files.size())) {
        thread_count = static_cast<int>(files.size());
        if (thread_count == 0) {
            thread_count = 1;
        }
    }

    auto worker = [&](int worker_id) {
        (void)worker_id;
        std::unique_ptr<WebDavClient> client;
        if (remote_checks) {
            client = std::make_unique<WebDavClient>(*base_url, creds);
            if (!client->IsReady()) {
                logger.Error("Failed to initialize WebDAV client for worker.");
                add_error();
                return;
            }
        }

        while (true) {
            size_t index = next_index.fetch_add(1);
            if (index >= files.size()) {
                break;
            }

            const FileEntry& entry = files[index];
            std::error_code ec_size;
            auto file_size = std::filesystem::file_size(entry.abs_path, ec_size);
            if (ec_size) {
                logger.Error("Failed to get file size: " + entry.abs_path.string());
                add_error();
                continue;
            }

            std::error_code ec_time;
            auto last_write = std::filesystem::last_write_time(entry.abs_path, ec_time);
            if (ec_time) {
                logger.Error("Failed to get file time: " + entry.abs_path.string());
                add_error();
                continue;
            }

            LocalFileInfo local;
            local.path = entry.abs_path;
            local.size = file_size;
            local.last_modified = FileTimeToSystemClock(last_write);
            local.is_jpg = IsJpgFile(entry.abs_path);

            std::string remote_path = JoinRemotePath(config.remote, entry.rel_path);
            RemoteItemInfo remote;
            if (remote_checks) {
                std::string err;
                remote = client->GetInfo(remote_path, &err);
                if (!err.empty()) {
                    logger.Error("PROPFIND failed for " + remote_path + ": " + err);
                    add_error();
                    continue;
                }
                if (remote.exists && remote.is_dir) {
                    logger.Error("Remote path is a directory, expected file: " + remote_path);
                    add_error();
                    continue;
                }
            } else {
                remote.exists = false;
            }

            FileDecision decision = DecideFileAction(local, remote, config.compare_mode, run_start);
            bool needs_upload = decision.action == FileActionType::Upload ||
                                decision.action == FileActionType::UploadAndDelete;
            bool should_delete = decision.action == FileActionType::UploadAndDelete;

            if (!needs_upload) {
                logger.Info("Skip " + entry.rel_path.string() + " (" + decision.reason + ")");
                add_skipped();
                continue;
            }

            if (config.dry_run) {
                logger.Info("Dry-run: would upload " + entry.rel_path.string() + " (" +
                            decision.reason + ")");
                add_uploaded();
                if (should_delete) {
                    logger.Info("Dry-run: would delete local " + entry.rel_path.string());
                    add_deleted(entry.abs_path.string(), local.is_jpg,
                                IsOlderThan24Hours(local, run_start));
                }
                continue;
            }

            if (!client) {
                logger.Error("WebDAV client not available for upload: " + remote_path);
                add_error();
                continue;
            }

            std::string err;
            if (!client->PutFile(remote_path, entry.abs_path, &err)) {
                logger.Error("PUT failed for " + remote_path + ": " + err);
                add_error();
                continue;
            }

            logger.Info("Uploaded " + entry.rel_path.string());
            add_uploaded();

            if (should_delete) {
                std::error_code ec_delete;
                if (std::filesystem::remove(entry.abs_path, ec_delete)) {
                    logger.Info("Deleted local file " + entry.abs_path.string());
                    add_deleted(entry.abs_path.string(), local.is_jpg,
                                IsOlderThan24Hours(local, run_start));
                } else {
                    logger.Error("Failed to delete local file: " +
                                 entry.abs_path.string() + " (" + ec_delete.message() + ")");
                    add_error();
                }
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (int i = 0; i < thread_count; ++i) {
        workers.emplace_back(worker, i);
    }
    for (auto& t : workers) {
        t.join();
    }

    return stats;
}
