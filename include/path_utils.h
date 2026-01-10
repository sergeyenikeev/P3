#pragma once

#include <filesystem>
#include <string>

std::string NormalizeRemoteRoot(const std::string& remote);
std::string JoinRemotePath(const std::string& remote_root, const std::filesystem::path& relative);
std::string UrlEncodePath(const std::string& path);
std::string ToLowerAscii(const std::string& value);
std::string PathToGenericUtf8(const std::filesystem::path& path);
