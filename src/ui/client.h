/**
 * @file src/ui/client.h
 * @brief Common interface implemented by LocalClient (self instance) and
 *        RemoteClient (another discovered instance), so ui_settings.cpp/ui_apps.cpp
 *        render identically regardless of which is behind AppState::client.
 */
#pragma once

#ifdef _WIN32

// standard includes
#include <map>
#include <optional>
#include <string>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

namespace ui {

  class Client {
  public:
    virtual ~Client() = default;

    virtual std::optional<std::map<std::string, std::string>> get_config(std::string &error) = 0;
    virtual bool save_config(const std::map<std::string, std::string> &full_config, std::string &error) = 0;

    virtual std::optional<std::vector<nlohmann::json>> get_apps(std::string &error) = 0;
    virtual bool save_app(const nlohmann::json &app, std::string &error) = 0;
    virtual bool delete_app(int index, std::string &error) = 0;
  };

}  // namespace ui

#endif
