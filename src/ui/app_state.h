/**
 * @file src/ui/app_state.h
 * @brief Shared embedded-UI state for the Settings/Apps tabs.
 *
 * Ported from config-gui/app_state.h, simplified to a single (self) instance -
 * cross-instance management (config-gui's InstanceStore/connect flow) is a later phase.
 */
#pragma once

#ifdef _WIN32

// standard includes
#include <map>
#include <memory>
#include <string>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "local_client.h"

namespace ui {

  // Shared UI state passed by reference into the tab renderers (ui_settings.cpp,
  // ui_apps.cpp). Reloaded wholesale after every save, since Titan itself
  // resorts/renumbers apps on every write.
  struct AppState {
    std::unique_ptr<LocalClient> client = std::make_unique<LocalClient>();

    std::string status_message;
    bool status_is_error = false;

    // Settings tab
    std::map<std::string, std::string> config_values;
    std::string settings_filter;
    bool settings_loaded = false;

    // Apps tab
    std::vector<nlohmann::json> apps;
    bool apps_loaded = false;
    int selected_app_index = -1;  // index into `apps`, or -1 while editing a brand-new app
    nlohmann::json edit_app;
    bool editing = false;

    void set_status(std::string message, bool is_error) {
      status_message = std::move(message);
      status_is_error = is_error;
    }
  };

  void reload_settings(AppState &state);
  void reload_apps(AppState &state);

}  // namespace ui

#endif
