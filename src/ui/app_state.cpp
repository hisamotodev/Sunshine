/**
 * @file src/ui/app_state.cpp
 * @brief Definitions for reloading embedded-UI state from the local Titan instance.
 */
#ifdef _WIN32

// local includes
#include "app_state.h"

namespace ui {

  void reload_settings(AppState &state) {
    if (!state.client) {
      return;
    }
    std::string error;
    auto values = state.client->get_config(error);
    if (!values) {
      state.set_status("Failed to load settings: " + error, true);
      return;
    }
    state.config_values = std::move(*values);
    state.settings_loaded = true;
  }

  void reload_apps(AppState &state) {
    if (!state.client) {
      return;
    }
    std::string error;
    auto apps = state.client->get_apps(error);
    if (!apps) {
      state.set_status("Failed to load apps: " + error, true);
      return;
    }
    state.apps = std::move(*apps);
    state.apps_loaded = true;
    state.selected_app_index = -1;
    state.editing = false;
  }

}  // namespace ui

#endif
