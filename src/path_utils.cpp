#include "path_utils.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::string WideToUtf8(const std::wstring& wide) {
#ifdef _WIN32
    if (wide.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                   static_cast<int>(wide.size()),
                                   nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                        static_cast<int>(wide.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
#else
    return std::string(wide.begin(), wide.end());
#endif
}

bool IsUnreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

}  // namespace

std::string NormalizeRemoteRoot(const std::string& remote) {
    std::string value = remote.empty() ? "/" : remote;
    std::replace(value.begin(), value.end(), '\\', '/');
    if (value.front() != '/') {
        value.insert(value.begin(), '/');
    }
    while (value.size() > 1 && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string JoinRemotePath(const std::string& remote_root,
                           const std::filesystem::path& relative) {
    std::string normalized = NormalizeRemoteRoot(remote_root);
    if (relative.empty()) {
        return normalized;
    }
    std::string rel = relative.generic_string();
    if (rel == ".") {
        return normalized;
    }
    if (!rel.empty() && rel.front() == '/') {
        rel.erase(rel.begin());
    }
    if (normalized.back() != '/') {
        normalized.push_back('/');
    }
    normalized += rel;
    return normalized;
}

std::string UrlEncodePath(const std::string& path) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase;
    for (unsigned char c : path) {
        if (c == '/') {
            oss << '/';
        } else if (IsUnreserved(c)) {
            oss << c;
        } else {
            oss << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return oss.str();
}

std::string ToLowerAscii(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        if (c >= 'A' && c <= 'Z') {
            return static_cast<unsigned char>(c - 'A' + 'a');
        }
        return c;
    });
    return out;
}

std::string PathToGenericUtf8(const std::filesystem::path& path) {
    std::wstring wide = path.wstring();
    std::string utf8 = WideToUtf8(wide);
    std::replace(utf8.begin(), utf8.end(), '\\', '/');
    return utf8;
}
