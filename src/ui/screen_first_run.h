/**
 * @file src/ui/screen_first_run.h
 * @brief Declarations for the embedded UI's first-run username/password setup screen.
 */
#pragma once

#ifdef _WIN32

namespace ui {

  // Shown in place of the normal Settings/Apps tabs whenever config::sunshine.username
  // is empty. Calls http::save_user_creds()/reload_user_creds() directly (in-process) -
  // the same functions the `sunshine creds <user> <pass>` CLI subcommand already uses
  // (entry_handler.cpp's args::creds) - so this is a second caller of an already-proven
  // direct-call path, not new credential-handling logic.
  void render_first_run_screen();

}  // namespace ui

#endif
