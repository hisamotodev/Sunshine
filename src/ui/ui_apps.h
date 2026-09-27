/**
 * @file src/ui/ui_apps.h
 * @brief Declarations for the embedded UI's "Apps" tab. Ported from config-gui/ui_apps.h.
 */
#pragma once

#ifdef _WIN32

// local includes
#include "app_state.h"

namespace ui {

  // Renders the "Apps" tab: a list of apps.json entries (via LocalClient) plus an
  // edit form, including Destiny's own "remote-path" field.
  void render_apps_tab(AppState &state);

}  // namespace ui

#endif
