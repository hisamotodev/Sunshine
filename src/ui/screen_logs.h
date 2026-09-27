/**
 * @file src/ui/screen_logs.h
 * @brief Declarations for the embedded UI's log viewer screen.
 */
#pragma once

#ifdef _WIN32

namespace ui {

  // Displays the contents of config::sunshine.log_file, mirroring confighttp.cpp's
  // GET /api/logs (a plain file read, so this reads the file directly - no need to
  // route through a client abstraction).
  void render_logs_screen();

}  // namespace ui

#endif
