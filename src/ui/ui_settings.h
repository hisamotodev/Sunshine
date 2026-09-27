/**
 * @file src/ui/ui_settings.h
 * @brief Declarations for the embedded UI's "Settings" tab. Ported from config-gui/ui_settings.h.
 */
#pragma once

#ifdef _WIN32

// local includes
#include "app_state.h"

namespace ui {

  // Renders the "Settings" tab: a filterable, flat key/value editor over
  // sunshine.conf (via LocalClient, mirroring GET/POST /api/config).
  void render_settings_tab(AppState &state);

}  // namespace ui

#endif
