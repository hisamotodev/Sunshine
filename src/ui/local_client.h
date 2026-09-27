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

namespace ui {

  /**
   * @brief Mirrors config-gui's TitanClient interface (get_config/save_config/get_apps/
   *        save_app/delete_app) so ui_settings.cpp/ui_apps.cpp render the same way
   *        regardless of whether they're backed by this or a networked client (see
   *        remote_client.h, added in a later phase for managing other instances).
   *        Calls straight into config::/proc:: instead of round-tripping over HTTPS to
   *        itself - see titan/src/confighttp.cpp's getConfig/saveConfig/getApps/saveApp/
   *        deleteApp for the reference behavior being reproduced here.
   */
  class LocalClient {
  public:
    std::optional<std::map<std::string, std::string>> get_config(std::string &error);
    bool save_config(const std::map<std::string, std::string> &full_config, std::string &error);

    std::optional<std::vector<nlohmann::json>> get_apps(std::string &error);
    bool save_app(const nlohmann::json &app, std::string &error);
    bool delete_app(int index, std::string &error);
  };

}  // namespace ui

#endif
