/**
 * @file src/ui/ui_apps.cpp
 * @brief Definitions for the embedded UI's "Apps" tab. Ported from config-gui/ui_apps.cpp.
 */
#ifdef _WIN32

// local includes
#include "ui_apps.h"

// standard includes
#include <cfloat>

// lib includes
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace ui {

  namespace {

    void new_app_template(nlohmann::json &app) {
      app = nlohmann::json::object();
      app["name"] = "";
      app["cmd"] = "";
      app["working-dir"] = "";
      app["image-path"] = "";
      app["remote-path"] = "";
      app["output"] = "";
      app["exclude-global-prep-cmd"] = false;
      app["elevated"] = false;
      app["auto-detach"] = true;
      app["wait-all"] = true;
      app["exit-timeout"] = 5;
    }

    void text_field(nlohmann::json &app, const char *key, const char *label) {
      std::string value = app.value(key, std::string {});
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputText(label, &value);
      app[key] = value;
    }

    void bool_field(nlohmann::json &app, const char *key, const char *label) {
      bool value = app.value(key, false);
      ImGui::Checkbox(label, &value);
      app[key] = value;
    }

  }  // namespace

  void render_apps_tab(AppState &state) {
    if (!state.client) {
      ImGui::TextDisabled("No local Titan client available.");
      return;
    }

    if (!state.apps_loaded) {
      reload_apps(state);
    }

    if (ImGui::Button("Reload")) {
      reload_apps(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("New App")) {
      new_app_template(state.edit_app);
      state.selected_app_index = -1;
      state.editing = true;
    }

    ImGui::BeginChild("apps_list", ImVec2(260, 0), ImGuiChildFlags_Borders);
    for (std::size_t i = 0; i < state.apps.size(); ++i) {
      const std::string name = state.apps[i].value("name", std::string {"(unnamed)"});
      const bool is_selected = state.editing && state.selected_app_index == static_cast<int>(i);
      if (ImGui::Selectable((name + "##" + std::to_string(i)).c_str(), is_selected)) {
        state.edit_app = state.apps[i];
        state.selected_app_index = static_cast<int>(i);
        state.editing = true;
      }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("apps_edit", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (!state.editing) {
      ImGui::TextDisabled("Select an app on the left, or click \"New App\".");
    } else {
      text_field(state.edit_app, "name", "Name");
      text_field(state.edit_app, "cmd", "Command");
      text_field(state.edit_app, "working-dir", "Working dir");
      text_field(state.edit_app, "image-path", "Image path");
      text_field(state.edit_app, "remote-path", "Remote path (Destiny --remote-run)");
      text_field(state.edit_app, "output", "Log output path");

      bool_field(state.edit_app, "elevated", "Elevated");
      bool_field(state.edit_app, "auto-detach", "Auto-detach");
      bool_field(state.edit_app, "wait-all", "Wait for all processes");
      bool_field(state.edit_app, "exclude-global-prep-cmd", "Exclude global prep commands");

      int exit_timeout = state.edit_app.value("exit-timeout", 5);
      ImGui::SetNextItemWidth(120);
      ImGui::InputInt("Exit timeout (s)", &exit_timeout);
      state.edit_app["exit-timeout"] = exit_timeout;

      ImGui::Separator();

      if (ImGui::Button("Save")) {
        nlohmann::json to_send = state.edit_app;
        to_send["index"] = state.selected_app_index;
        std::string error;
        if (state.client->save_app(to_send, error)) {
          state.set_status("App saved.", false);
          reload_apps(state);
        } else {
          state.set_status("Failed to save app: " + error, true);
        }
      }

      if (state.selected_app_index >= 0) {
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
          std::string error;
          if (state.client->delete_app(state.selected_app_index, error)) {
            state.set_status("App deleted.", false);
            reload_apps(state);
          } else {
            state.set_status("Failed to delete app: " + error, true);
          }
        }
      }
    }
    ImGui::EndChild();
  }

}  // namespace ui

#endif
