/**
 * @file src/ui/screen_troubleshooting.cpp
 * @brief Definitions for the embedded UI's troubleshooting/diagnostics screen.
 */
#ifdef _WIN32

// lib includes
#include <boost/algorithm/string.hpp>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

// local includes
#include "screen_troubleshooting.h"
#include "src/config.h"
#include "src/crypto.h"
#include "src/display_device.h"
#include "src/httpcommon.h"
#include "src/platform/common.h"
#include "src/utility.h"

namespace ui {

  namespace {
    std::string status_message;
    bool status_is_error = false;

    std::string current_password;
    std::string new_username;
    std::string new_password;
    std::string confirm_password;

    void change_password() {
      const auto hash = util::hex(crypto::hash(current_password + config::sunshine.salt)).to_string();
      if (hash != config::sunshine.password) {
        status_message = "Current password is incorrect.";
        status_is_error = true;
        return;
      }

      const std::string username = new_username.empty() ? config::sunshine.username : new_username;
      if (new_password.empty() || new_password != confirm_password) {
        status_message = "New password must be non-empty and match its confirmation.";
        status_is_error = true;
        return;
      }

      {
        const std::scoped_lock lock(config::config_write_mutex);
        http::save_user_creds(config::sunshine.credentials_file, username, new_password);
        http::reload_user_creds(config::sunshine.credentials_file);
      }
      status_message = "Password changed.";
      status_is_error = false;
      current_password.clear();
      new_password.clear();
      confirm_password.clear();
    }
  }  // namespace

  void render_troubleshooting_screen() {
    ImGui::SeparatorText("Actions");

    if (ImGui::Button("Restart Sunshine")) {
      platf::restart();  // may not return
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Display Device Persistence")) {
      const bool ok = display_device::reset_persistence();
      status_message = ok ? "Display device persistence reset." : "Failed to reset display device persistence.";
      status_is_error = !ok;
    }

    ImGui::SeparatorText("Change Password");
    ImGui::InputText("Current password", &current_password, ImGuiInputTextFlags_Password);
    ImGui::InputTextWithHint("New username", config::sunshine.username.c_str(), &new_username);
    ImGui::InputText("New password", &new_password, ImGuiInputTextFlags_Password);
    ImGui::InputText("Confirm new password", &confirm_password, ImGuiInputTextFlags_Password);
    if (ImGui::Button("Change Password")) {
      change_password();
    }

    if (!status_message.empty()) {
      const auto color = status_is_error ? ImVec4(1.0F, 0.4F, 0.4F, 1.0F) : ImVec4(0.5F, 0.9F, 0.5F, 1.0F);
      ImGui::TextColored(color, "%s", status_message.c_str());
    }
  }

}  // namespace ui

#endif
