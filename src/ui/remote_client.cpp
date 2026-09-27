/**
 * @file src/ui/remote_client.cpp
 * @brief Definitions for the WinHTTP client used to manage another discovered Titan
 *        instance. Ported from config-gui/titan_client.cpp.
 */
#ifdef _WIN32

// standard includes
#include <format>

// clang-format off
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
// clang-format on

// local includes
#include "remote_client.h"
#include "win_util.h"

namespace ui {

  namespace {

    constexpr auto kUserAgent = L"TitanEmbeddedUI/1.0";

    std::string basic_auth_header(const std::string &username, const std::string &password) {
      const std::string combined = username + ":" + password;
      const auto encoded = win_util::base64_encode(reinterpret_cast<const std::uint8_t *>(combined.data()), combined.size());
      return "Basic " + encoded;
    }

  }  // namespace

  RemoteClient::RemoteClient(RemoteCredentials credentials):
      credentials_ {std::move(credentials)} {
    session_ = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  }

  RemoteClient::~RemoteClient() {
    if (session_) {
      WinHttpCloseHandle(static_cast<HINTERNET>(session_));
    }
  }

  bool RemoteClient::request(const std::wstring &method, const std::wstring &path, const std::string *json_body, RawResponse &out, std::string &error) {
    if (!session_) {
      error = "WinHttpOpen failed";
      return false;
    }

    const auto host_wide = win_util::utf8_to_wide(credentials_.host);
    HINTERNET connect = WinHttpConnect(static_cast<HINTERNET>(session_), host_wide.c_str(), credentials_.port, 0);
    if (!connect) {
      error = std::format("WinHttpConnect failed ({})", GetLastError());
      return false;
    }

    HINTERNET req = WinHttpOpenRequest(
      connect,
      method.c_str(),
      path.c_str(),
      nullptr,
      WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES,
      WINHTTP_FLAG_SECURE
    );
    if (!req) {
      error = std::format("WinHttpOpenRequest failed ({})", GetLastError());
      WinHttpCloseHandle(connect);
      return false;
    }

    // Titan's management server presents a self-signed cert by default; this client
    // relies on the same trust-on-first-use model config-gui already used rather than
    // pinning a CA (the discovery manifest, not a cert, is what identifies the instance).
    DWORD security_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA
      | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
      | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
      | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &security_flags, sizeof(security_flags));

    std::wstring headers = L"Authorization: " + win_util::utf8_to_wide(basic_auth_header(credentials_.username, credentials_.password)) + L"\r\n";
    if (json_body) {
      headers += L"Content-Type: application/json\r\n";
    }

    const void *body_ptr = json_body ? json_body->data() : nullptr;
    const DWORD body_len = json_body ? static_cast<DWORD>(json_body->size()) : 0;

    const BOOL sent = WinHttpSendRequest(
      req,
      headers.c_str(),
      static_cast<DWORD>(-1L),
      const_cast<void *>(body_ptr),
      body_len,
      body_len,
      0
    );
    if (!sent || !WinHttpReceiveResponse(req, nullptr)) {
      error = std::format("HTTP request failed ({})", GetLastError());
      WinHttpCloseHandle(req);
      WinHttpCloseHandle(connect);
      return false;
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(
      req,
      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX,
      &status_code,
      &status_size,
      WINHTTP_NO_HEADER_INDEX
    );
    out.status = static_cast<int>(status_code);

    std::string body;
    for (;;) {
      DWORD available = 0;
      if (!WinHttpQueryDataAvailable(req, &available) || available == 0) {
        break;
      }
      std::string chunk(available, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(req, chunk.data(), available, &read)) {
        break;
      }
      chunk.resize(read);
      body += chunk;
    }
    out.body = std::move(body);

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    return true;
  }

  std::optional<std::map<std::string, std::string>> RemoteClient::get_config(std::string &error) {
    RawResponse response;
    if (!request(L"GET", L"/api/config", nullptr, response, error)) {
      return std::nullopt;
    }
    if (response.status != 200) {
      error = std::format("GET /api/config returned HTTP {}", response.status);
      return std::nullopt;
    }

    try {
      const auto parsed = nlohmann::json::parse(response.body);
      std::map<std::string, std::string> values;
      for (const auto &[key, value] : parsed.items()) {
        if (key == "status" || key == "platform" || key == "version") {
          continue;
        }
        values[key] = value.is_string() ? value.get<std::string>() : value.dump();
      }
      return values;
    } catch (const std::exception &e) {
      error = std::format("Failed to parse /api/config response: {}", e.what());
      return std::nullopt;
    }
  }

  bool RemoteClient::save_config(const std::map<std::string, std::string> &full_config, std::string &error) {
    nlohmann::json body = nlohmann::json::object();
    for (const auto &[key, value] : full_config) {
      body[key] = value;
    }
    const std::string body_str = body.dump();

    RawResponse response;
    if (!request(L"POST", L"/api/config", &body_str, response, error)) {
      return false;
    }
    if (response.status != 200) {
      error = std::format("POST /api/config returned HTTP {}", response.status);
      return false;
    }
    return true;
  }

  std::optional<std::vector<nlohmann::json>> RemoteClient::get_apps(std::string &error) {
    RawResponse response;
    if (!request(L"GET", L"/api/apps", nullptr, response, error)) {
      return std::nullopt;
    }
    if (response.status != 200) {
      error = std::format("GET /api/apps returned HTTP {}", response.status);
      return std::nullopt;
    }

    try {
      const auto parsed = nlohmann::json::parse(response.body);
      std::vector<nlohmann::json> apps;
      if (parsed.contains("apps") && parsed["apps"].is_array()) {
        for (const auto &app : parsed["apps"]) {
          apps.push_back(app);
        }
      }
      return apps;
    } catch (const std::exception &e) {
      error = std::format("Failed to parse /api/apps response: {}", e.what());
      return std::nullopt;
    }
  }

  bool RemoteClient::save_app(const nlohmann::json &app, std::string &error) {
    const std::string body_str = app.dump();

    RawResponse response;
    if (!request(L"POST", L"/api/apps", &body_str, response, error)) {
      return false;
    }
    if (response.status != 200) {
      error = std::format("POST /api/apps returned HTTP {}: {}", response.status, response.body);
      return false;
    }
    return true;
  }

  bool RemoteClient::delete_app(int index, std::string &error) {
    const std::wstring path = std::format(L"/api/apps/{}", index);

    RawResponse response;
    if (!request(L"DELETE", path, nullptr, response, error)) {
      return false;
    }
    if (response.status != 200) {
      error = std::format("DELETE {} returned HTTP {}", win_util::wide_to_utf8(path), response.status);
      return false;
    }
    return true;
  }

}  // namespace ui

#endif
