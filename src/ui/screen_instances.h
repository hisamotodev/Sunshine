/**
 * @file src/ui/screen_instances.h
 * @brief Declarations for the embedded UI's "Other Instances" screen.
 */
#pragma once

#ifdef _WIN32

namespace ui {

  // Lists other Titan instances discovered via ui::discover_other_instances(), and lets
  // the user connect to one (credentials entered once, then cached via CredentialCache)
  // to view/edit its Settings/Apps through the same render_settings_tab/render_apps_tab
  // used for the local instance, backed by RemoteClient instead of LocalClient.
  void render_instances_screen();

}  // namespace ui

#endif
