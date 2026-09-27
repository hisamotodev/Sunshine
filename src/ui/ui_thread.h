/**
 * @file src/ui/ui_thread.h
 * @brief Declarations for the embedded SDL3 + Dear ImGui management UI.
 *
 * This replaces the browser-based WebUI (see docs/research and the Titan
 * embedded ImGui UI plan). Windows-only for now.
 */
#pragma once

#ifdef _WIN32

namespace ui {
  /**
   * @brief Starts the embedded UI's background thread. The window is created
   *        hidden; call show() to reveal it. Safe to call once during startup.
   */
  void start();

  /**
   * @brief Signals the embedded UI thread to shut down and joins it.
   */
  void stop();

  /**
   * @brief Requests the embedded UI window be shown and raised to the foreground.
   *        Safe to call from any thread.
   */
  void show();

  /**
   * @brief Requests the embedded UI window be hidden. Safe to call from any thread.
   */
  void hide();

  /**
   * @brief Returns whether the embedded UI window is currently visible.
   */
  bool is_visible();
}  // namespace ui

#endif
