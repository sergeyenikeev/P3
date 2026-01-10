#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "decision.h"

struct WebDavResponse {
    long status = 0;
    std::string body;
};

struct BaseUrlParts {
    bool https = true;
    std::wstring host;
    unsigned short port = 443;
    std::string base_path = "/";
};

struct WebDavCredentials {
    std::string username;
    std::string password;
};

class WebDavClient {
public:
    WebDavClient(const BaseUrlParts& base_url, const WebDavCredentials& creds);
    ~WebDavClient();

    bool IsReady() const;

    WebDavResponse PropFind(const std::string& remote_path, std::string* error);
    bool MkCol(const std::string& remote_path, bool* created, std::string* error);
    bool PutFile(const std::string& remote_path,
                 const std::filesystem::path& local_path,
                 std::string* error);

    RemoteItemInfo GetInfo(const std::string& remote_path, std::string* error);

    static std::optional<BaseUrlParts> ParseBaseUrl(const std::string& url, std::string* error);

private:
    WebDavResponse SendRequest(const std::wstring& method,
                               const std::wstring& request_path,
                               const std::string& body,
                               const std::string& extra_headers,
                               std::string* error);

    bool SendFile(const std::wstring& method,
                  const std::wstring& request_path,
                  const std::filesystem::path& local_path,
                  const std::string& extra_headers,
                  std::string* error);

    std::wstring BuildRequestPath(const std::string& remote_path) const;
    std::string BuildAuthHeader() const;

    void CloseHandles();

    BaseUrlParts base_url_;
    WebDavCredentials creds_;
    void* session_ = nullptr;
    void* connection_ = nullptr;
};
