/**
 * @file src/ui/screen_first_run.cpp
 * @brief Definitions for the embedded UI's first-run username/password setup screen.
 */
#ifdef _WIN32

// lib includes
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

// local includes
#include "screen_first_run.h"
#include "src/config.h"
#include "src/httpcommon.h"

using namespace std::literals;

namespace ui {

  namespace {
    std::string username = "sunshine";
    std::string password;
    std::string confirm_password;
    std::string status_message;
    bool status_is_error = false;
  }  // namespace

  void render_first_run_screen() {
    ImGui::TextWrapped(
      "Set a username and password to finish setting up Sunshine. This is required "
      "before pairing a client or streaming."
    );
    ImGui::Spacing();

    ImGui::InputText("Username", &username);
    ImGui::InputText("Password", &password, ImGuiInputTextFlags_Password);
    ImGui::InputText("Confirm password", &confirm_password, ImGuiInputTextFlags_Password);

    if (ImGui::Button("Save")) {
      if (username.empty()) {
        status_message = "Username cannot be empty.";
        status_is_error = true;
      } else if (password.empty() || password != confirm_password) {
        status_message = "Passwords must match and cannot be empty.";
        status_is_error = true;
      } else {
        {
          const std::scoped_lock lock(config::config_write_mutex);
          http::save_user_creds(config::sunshine.credentials_file, username, password);
          http::reload_user_creds(config::sunshine.credentials_file);
        }
        status_message = "Saved.";
        status_is_error = false;
        password.clear();
        confirm_password.clear();
      }
    }

    if (!status_message.empty()) {
      const auto color = status_is_error ? ImVec4(1.0F, 0.4F, 0.4F, 1.0F) : ImVec4(0.5F, 0.9F, 0.5F, 1.0F);
      ImGui::TextColored(color, "%s", status_message.c_str());
    }
  }

}  // namespace ui

#endif
