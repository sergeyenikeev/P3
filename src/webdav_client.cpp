#include "webdav_client.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include "path_utils.h"

namespace {

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                   static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(),
                        static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

std::string WideToUtf8(const std::wstring& wide) {
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
}

std::string FormatWinError(DWORD error_code) {
    LPWSTR buffer = nullptr;
    DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                    FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, error_code,
                                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string message;
    if (size && buffer) {
        std::wstring wide(buffer, size);
        message = WideToUtf8(wide);
        LocalFree(buffer);
    }
    return message;
}

std::string Base64Encode(const std::string& input) {
    static const char* kTable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    int val = 0;
    int valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kTable[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(kTable[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

std::optional<std::string> ExtractXmlTagValue(const std::string& xml,
                                              const std::string& tag) {
    std::string pattern = "<[^>]*" + tag + "[^>]*>([^<]*)</[^>]*" + tag + ">";
    std::regex re(pattern, std::regex_constants::icase);
    std::smatch match;
    if (std::regex_search(xml, match, re) && match.size() > 1) {
        return match[1].str();
    }
    return std::nullopt;
}

bool ContainsNotFoundStatus(const std::string& xml) {
    std::regex re("HTTP/1\\.[01] 404", std::regex_constants::icase);
    return std::regex_search(xml, re);
}

bool ContainsCollection(const std::string& xml) {
    std::regex re("<[^>]*collection[^>]*/>", std::regex_constants::icase);
    return std::regex_search(xml, re);
}

std::optional<std::chrono::system_clock::time_point> ParseHttpDate(const std::string& value) {
    std::tm tm{};
    std::istringstream iss(value);
    iss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");
    if (iss.fail()) {
        return std::nullopt;
    }
    tm.tm_isdst = 0;
#ifdef _WIN32
    std::time_t t = _mkgmtime(&tm);
#else
    std::time_t t = timegm(&tm);
#endif
    if (t == -1) {
        return std::nullopt;
    }
    return std::chrono::system_clock::from_time_t(t);
}

bool IsRetryableStatus(long status) {
    return status == 408 || status == 429 || (status >= 500 && status <= 599);
}

}  // namespace

WebDavClient::WebDavClient(const BaseUrlParts& base_url, const WebDavCredentials& creds)
    : base_url_(base_url), creds_(creds) {
    session_ = WinHttpOpen(L"MailRuUploader/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_) {
        WinHttpSetTimeouts(session_, 10000, 10000, 30000, 30000);
        connection_ = WinHttpConnect(session_, base_url_.host.c_str(), base_url_.port, 0);
    }
}

WebDavClient::~WebDavClient() {
    CloseHandles();
}

bool WebDavClient::IsReady() const {
    return session_ && connection_;
}

void WebDavClient::CloseHandles() {
    if (connection_) {
        WinHttpCloseHandle(connection_);
        connection_ = nullptr;
    }
    if (session_) {
        WinHttpCloseHandle(session_);
        session_ = nullptr;
    }
}

WebDavResponse WebDavClient::PropFind(const std::string& remote_path, std::string* error) {
    const std::string body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<d:propfind xmlns:d=\"DAV:\">"
        "<d:prop><d:getlastmodified/><d:getcontentlength/><d:getetag/><d:resourcetype/></d:prop>"
        "</d:propfind>";

    const std::string headers = "Depth: 0\r\nContent-Type: text/xml\r\n";
    std::wstring path = BuildRequestPath(remote_path);
    return SendRequest(L"PROPFIND", path, body, headers, error);
}

bool WebDavClient::MkCol(const std::string& remote_path, bool* created, std::string* error) {
    std::wstring path = BuildRequestPath(remote_path);
    WebDavResponse resp = SendRequest(L"MKCOL", path, "", "", error);
    if (created) {
        *created = (resp.status == 201);
    }
    if (resp.status == 201 || resp.status == 405) {
        return true;
    }
    if (error && error->empty()) {
        *error = "MKCOL failed with status " + std::to_string(resp.status);
    }
    return false;
}

bool WebDavClient::PutFile(const std::string& remote_path,
                           const std::filesystem::path& local_path,
                           std::string* error) {
    std::wstring path = BuildRequestPath(remote_path);
    return SendFile(L"PUT", path, local_path, "", error);
}

RemoteItemInfo WebDavClient::GetInfo(const std::string& remote_path, std::string* error) {
    RemoteItemInfo info;
    WebDavResponse resp = PropFind(remote_path, error);
    if (resp.status == 404) {
        info.exists = false;
        return info;
    }
    if (resp.status >= 400 && resp.status != 207) {
        if (error && error->empty()) {
            *error = "PROPFIND failed with status " + std::to_string(resp.status);
        }
        return info;
    }

    if (ContainsNotFoundStatus(resp.body)) {
        info.exists = false;
        return info;
    }

    info.exists = true;
    info.is_dir = ContainsCollection(resp.body);

    if (auto size_value = ExtractXmlTagValue(resp.body, "getcontentlength")) {
        try {
            info.size = std::stoull(*size_value);
            info.has_size = true;
        } catch (...) {
            info.has_size = false;
        }
    }

    if (auto last_modified = ExtractXmlTagValue(resp.body, "getlastmodified")) {
        auto parsed = ParseHttpDate(*last_modified);
        if (parsed.has_value()) {
            info.last_modified = *parsed;
            info.has_last_modified = true;
        }
    }

    if (auto etag = ExtractXmlTagValue(resp.body, "getetag")) {
        info.etag = *etag;
    }

    return info;
}

std::optional<BaseUrlParts> WebDavClient::ParseBaseUrl(const std::string& url,
                                                       std::string* error) {
    std::wstring wide = Utf8ToWide(url);
    if (wide.empty()) {
        if (error) {
            *error = "Base URL is empty or invalid";
        }
        return std::nullopt;
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &components)) {
        if (error) {
            *error = "Failed to parse base URL";
        }
        return std::nullopt;
    }

    BaseUrlParts out;
    out.https = (components.nScheme == INTERNET_SCHEME_HTTPS);
    out.port = components.nPort;
    out.host.assign(components.lpszHostName, components.dwHostNameLength);
    if (components.dwUrlPathLength > 0) {
        std::wstring path_w(components.lpszUrlPath, components.dwUrlPathLength);
        std::string path_u8 = WideToUtf8(path_w);
        out.base_path = path_u8.empty() ? "/" : path_u8;
    } else {
        out.base_path = "/";
    }
    if (out.base_path.empty()) {
        out.base_path = "/";
    }

    return out;
}

WebDavResponse WebDavClient::SendRequest(const std::wstring& method,
                                         const std::wstring& request_path,
                                         const std::string& body,
                                         const std::string& extra_headers,
                                         std::string* error) {
    const int kMaxRetries = 3;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300 * attempt));
        }
        WebDavResponse response;
        if (!IsReady()) {
            if (error) {
                *error = "WinHTTP session not ready";
            }
            return response;
        }

        DWORD flags = base_url_.https ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = WinHttpOpenRequest(connection_, method.c_str(),
                                               request_path.c_str(), nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!request) {
            if (error) {
                *error = "WinHttpOpenRequest failed: " + FormatWinError(GetLastError());
            }
            return response;
        }

        std::wstring auth_header = Utf8ToWide(BuildAuthHeader());
        if (!auth_header.empty()) {
            WinHttpAddRequestHeaders(request, auth_header.c_str(), -1,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }
        if (!extra_headers.empty()) {
            std::wstring headers_w = Utf8ToWide(extra_headers);
            WinHttpAddRequestHeaders(request, headers_w.c_str(), -1,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        BOOL ok = WinHttpSendRequest(request,
                                     WINHTTP_NO_ADDITIONAL_HEADERS,
                                     0,
                                     body.empty() ? WINHTTP_NO_REQUEST_DATA
                                                  : const_cast<char*>(body.data()),
                                     static_cast<DWORD>(body.size()),
                                     static_cast<DWORD>(body.size()),
                                     0);
        if (!ok) {
            if (error) {
                *error = "WinHttpSendRequest failed: " + FormatWinError(GetLastError());
            }
            WinHttpCloseHandle(request);
            response.status = 0;
        } else {
            ok = WinHttpReceiveResponse(request, nullptr);
            if (!ok) {
                if (error) {
                    *error = "WinHttpReceiveResponse failed: " + FormatWinError(GetLastError());
                }
                WinHttpCloseHandle(request);
                response.status = 0;
            } else {
                DWORD status_code = 0;
                DWORD status_size = sizeof(status_code);
                WinHttpQueryHeaders(request,
                                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                                    WINHTTP_NO_HEADER_INDEX);
                response.status = static_cast<long>(status_code);

                std::string body_out;
                DWORD data_size = 0;
                do {
                    data_size = 0;
                    if (!WinHttpQueryDataAvailable(request, &data_size)) {
                        break;
                    }
                    if (data_size == 0) {
                        break;
                    }
                    std::vector<char> buffer(data_size);
                    DWORD read = 0;
                    if (!WinHttpReadData(request, buffer.data(), data_size, &read)) {
                        break;
                    }
                    body_out.append(buffer.data(), buffer.data() + read);
                } while (data_size > 0);

                response.body = std::move(body_out);
                WinHttpCloseHandle(request);
            }
        }

        if (!IsRetryableStatus(response.status) && response.status != 0) {
            if (error) {
                error->clear();
            }
            return response;
        }
        if (attempt == kMaxRetries - 1) {
            return response;
        }
    }

    return {};
}

bool WebDavClient::SendFile(const std::wstring& method,
                            const std::wstring& request_path,
                            const std::filesystem::path& local_path,
                            const std::string& extra_headers,
                            std::string* error) {
    const int kMaxRetries = 3;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300 * attempt));
        }

        if (!IsReady()) {
            if (error) {
                *error = "WinHTTP session not ready";
            }
            return false;
        }

        std::error_code ec;
        auto file_size = std::filesystem::file_size(local_path, ec);
        if (ec) {
            if (error) {
                *error = "Failed to get file size: " + ec.message();
            }
            return false;
        }

        DWORD flags = base_url_.https ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = WinHttpOpenRequest(connection_, method.c_str(),
                                               request_path.c_str(), nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!request) {
            if (error) {
                *error = "WinHttpOpenRequest failed: " + FormatWinError(GetLastError());
            }
            return false;
        }

        std::wstring auth_header = Utf8ToWide(BuildAuthHeader());
        if (!auth_header.empty()) {
            WinHttpAddRequestHeaders(request, auth_header.c_str(), -1,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }
        if (!extra_headers.empty()) {
            std::wstring headers_w = Utf8ToWide(extra_headers);
            WinHttpAddRequestHeaders(request, headers_w.c_str(), -1,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        BOOL ok = WinHttpSendRequest(request,
                                     WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     WINHTTP_NO_REQUEST_DATA, 0,
                                     static_cast<DWORD>(file_size), 0);
        if (!ok) {
            if (error) {
                *error = "WinHttpSendRequest failed: " + FormatWinError(GetLastError());
            }
            WinHttpCloseHandle(request);
            continue;
        }

        HANDLE file = CreateFileW(local_path.wstring().c_str(), GENERIC_READ,
                                  FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (error) {
                *error = "Failed to open file for upload";
            }
            WinHttpCloseHandle(request);
            return false;
        }

        const DWORD kBufferSize = 64 * 1024;
        std::vector<char> buffer(kBufferSize);
        DWORD read = 0;
        bool success = true;
        while (ReadFile(file, buffer.data(), kBufferSize, &read, nullptr) && read > 0) {
            DWORD written = 0;
            if (!WinHttpWriteData(request, buffer.data(), read, &written)) {
                success = false;
                if (error) {
                    *error = "WinHttpWriteData failed: " + FormatWinError(GetLastError());
                }
                break;
            }
        }

        CloseHandle(file);

        if (!success) {
            WinHttpCloseHandle(request);
            continue;
        }

        ok = WinHttpReceiveResponse(request, nullptr);
        if (!ok) {
            if (error) {
                *error = "WinHttpReceiveResponse failed: " + FormatWinError(GetLastError());
            }
            WinHttpCloseHandle(request);
            continue;
        }

        DWORD status_code = 0;
        DWORD status_size = sizeof(status_code);
        WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                            WINHTTP_NO_HEADER_INDEX);
        WinHttpCloseHandle(request);

        if (status_code >= 200 && status_code < 300) {
            if (error) {
                error->clear();
            }
            return true;
        }
        if (!IsRetryableStatus(status_code) || attempt == kMaxRetries - 1) {
            if (error) {
                *error = "PUT failed with status " + std::to_string(status_code);
            }
            return false;
        }
    }

    return false;
}

std::wstring WebDavClient::BuildRequestPath(const std::string& remote_path) const {
    std::string encoded = UrlEncodePath(remote_path);
    std::string base = base_url_.base_path.empty() ? "/" : base_url_.base_path;
    if (base.back() == '/' && !encoded.empty() && encoded.front() == '/') {
        encoded.erase(encoded.begin());
    } else if (base.back() != '/' && (encoded.empty() || encoded.front() != '/')) {
        base.push_back('/');
    }
    std::string full = base + encoded;
    return Utf8ToWide(full);
}

std::string WebDavClient::BuildAuthHeader() const {
    if (creds_.username.empty() && creds_.password.empty()) {
        return {};
    }
    std::string token = creds_.username + ":" + creds_.password;
    std::string encoded = Base64Encode(token);
    return "Authorization: Basic " + encoded + "\r\n";
}
