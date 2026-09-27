/**
 * @file src/ui/screen_pairing.h
 * @brief Declarations for the embedded UI's pairing PIN entry screen.
 */
#pragma once

#ifdef _WIN32

namespace ui {

  // Lets a user complete a Hunter/Moonlight pairing request without a browser.
  // Calls the same nvhttp:: functions confighttp.cpp's /api/pin handlers do.
  void render_pairing_screen();

}  // namespace ui

#endif
