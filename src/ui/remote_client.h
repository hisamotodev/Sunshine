/**
 * @file src/ui/remote_client.h
 * @brief WinHTTP client for another discovered Titan instance's HTTPS management API.
 *        Ported from config-gui/titan_client.h. Compare LocalClient, which implements
 *        the same Client interface for the embedded UI's own (self) instance in-process.
 */
#pragma once

#ifdef _WIN32

// standard includes
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "client.h"

namespace ui {

  struct RemoteCredentials {
    std::string host;
    std::uint16_t port = 0;
    std::string username;
    std::string password;
  };

  // Mirrors what confighttp.cpp itself implements - see that file for the
  // authoritative endpoint behavior being reproduced here over HTTPS + Basic Auth.
  class RemoteClient: public Client {
  public:
    explicit RemoteClient(RemoteCredentials credentials);
    ~RemoteClient() override;

    RemoteClient(const RemoteClient &) = delete;
    RemoteClient &operator=(const RemoteClient &) = delete;

    std::optional<std::map<std::string, std::string>> get_config(std::string &error) override;
    bool save_config(const std::map<std::string, std::string> &full_config, std::string &error) override;

    std::optional<std::vector<nlohmann::json>> get_apps(std::string &error) override;
    bool save_app(const nlohmann::json &app, std::string &error) override;
    bool delete_app(int index, std::string &error) override;

  private:
    struct RawResponse {
      int status = 0;
      std::string body;
    };

    bool request(const std::wstring &method, const std::wstring &path, const std::string *json_body, RawResponse &out, std::string &error);

    RemoteCredentials credentials_;
    void *session_ = nullptr;  // HINTERNET, opaque here to avoid pulling <winhttp.h> into this header
  };

}  // namespace ui

#endif
