/**
 * @file src/ui/local_client.h
 * @brief In-process equivalent of the config-gui tool's TitanClient, for the embedded
 *        UI managing its own (self) Titan instance.
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

// local includes
#include "client.h"

namespace ui {

  /**
   * @brief Implements Client for the embedded UI's own (self) Titan instance, calling
   *        straight into config::/proc:: instead of round-tripping over HTTPS to itself -
   *        see titan/src/confighttp.cpp's getConfig/saveConfig/getApps/saveApp/deleteApp
   *        for the reference behavior being reproduced here. Compare RemoteClient, which
   *        implements the same interface for a different discovered instance.
   */
  class LocalClient: public Client {
  public:
    std::optional<std::map<std::string, std::string>> get_config(std::string &error) override;
    bool save_config(const std::map<std::string, std::string> &full_config, std::string &error) override;

    std::optional<std::vector<nlohmann::json>> get_apps(std::string &error) override;
    bool save_app(const nlohmann::json &app, std::string &error) override;
    bool delete_app(int index, std::string &error) override;
  };

}  // namespace ui

#endif
