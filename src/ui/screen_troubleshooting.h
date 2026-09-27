/**
 * @file src/ui/screen_troubleshooting.h
 * @brief Declarations for the embedded UI's troubleshooting/diagnostics screen.
 */
#pragma once

#ifdef _WIN32

namespace ui {

  // Restart/reset buttons and password change, mirroring confighttp.cpp's
  // /api/restart, /api/reset-display-device-persistence, /api/reset-portal-token,
  // and /api/password (the "change an existing password" path - first-run setup
  // is screen_first_run.h).
  void render_troubleshooting_screen();

}  // namespace ui

#endif
