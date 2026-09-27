/**
 * @file src/ui/screen_clients.h
 * @brief Declarations for the embedded UI's paired-clients screen.
 */
#pragma once

#ifdef _WIN32

namespace ui {

  // Lists paired clients and lets a user enable/disable or unpair them, mirroring
  // confighttp.cpp's /api/clients/list|unpair|unpair-all|update handlers.
  void render_clients_screen();

}  // namespace ui

#endif
