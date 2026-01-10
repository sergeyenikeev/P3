#include "decision.h"

#include <chrono>

namespace {

std::chrono::system_clock::time_point NowMinusHours(std::chrono::system_clock::time_point now,
                                                    int hours) {
    return now - std::chrono::hours(hours);
}

}  // namespace

bool IsDifferent(const LocalFileInfo& local,
                 const RemoteItemInfo& remote,
                 CompareMode mode) {
    if (!remote.exists) {
        return true;
    }
    if (!remote.has_size) {
        return true;
    }
    if (remote.size != local.size) {
        return true;
    }
    if (mode == CompareMode::SizeOnly) {
        return false;
    }
    if (!remote.has_last_modified) {
        return true;
    }

    auto tolerance = std::chrono::seconds(2);
    if (local.last_modified > remote.last_modified + tolerance) {
        return true;
    }
    return false;
}

FileDecision DecideFileAction(const LocalFileInfo& local,
                              const RemoteItemInfo& remote,
                              CompareMode mode,
                              std::chrono::system_clock::time_point run_start) {
    FileDecision decision;

    if (local.is_jpg) {
        decision.action = FileActionType::UploadAndDelete;
        decision.reason = remote.exists ? "jpg overwrite" : "jpg upload";
        return decision;
    }

    bool older_than_24 = IsOlderThan24Hours(local, run_start);

    if (!remote.exists) {
        decision.action = older_than_24 ? FileActionType::UploadAndDelete : FileActionType::Upload;
        decision.reason = older_than_24 ? "upload + delete (old)" : "upload (missing)";
        return decision;
    }

    if (IsDifferent(local, remote, mode)) {
        decision.action = older_than_24 ? FileActionType::UploadAndDelete : FileActionType::Upload;
        decision.reason = older_than_24 ? "upload + delete (old diff)" : "upload (diff)";
        return decision;
    }

    decision.action = FileActionType::Skip;
    decision.reason = "skip (same)";
    return decision;
}

bool IsOlderThan24Hours(const LocalFileInfo& local,
                        std::chrono::system_clock::time_point run_start) {
    auto threshold = NowMinusHours(run_start, 24);
    return local.last_modified < threshold;
}
