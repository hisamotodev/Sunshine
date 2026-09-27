/**
 * @file src/ui/screen_virtualhid.h
 * @brief Declarations for the embedded UI's Virtual HID Driver status/license screen.
 */
#pragma once

#ifdef _WIN32

namespace ui {

  // Surfaces libvirtualhid/ViGEmBus driver status and machine license controls,
  // mirroring confighttp.cpp's /api/virtual-input/status and /api/virtual-input/license.
  void render_virtualhid_screen();

}  // namespace ui

#endif
