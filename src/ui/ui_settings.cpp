/**
 * @file src/ui/ui_settings.cpp
 * @brief Definitions for the embedded UI's "Settings" tab. Ported from config-gui/ui_settings.cpp.
 */
#ifdef _WIN32

// local includes
#include "ui_settings.h"

// standard includes
#include <algorithm>
#include <cctype>
#include <cfloat>

// lib includes
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace ui {

  namespace {

    std::string to_lower(std::string value) {
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return value;
    }

  }  // namespace

  void render_settings_tab(AppState &state) {
    if (!state.client) {
      ImGui::TextDisabled("No local Titan client available.");
      return;
    }

    if (!state.settings_loaded) {
      reload_settings(state);
    }

    if (ImGui::Button("Reload")) {
      reload_settings(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
      std::string error;
      if (state.client->save_config(state.config_values, error)) {
        state.set_status("Settings saved.", false);
      } else {
        state.set_status("Failed to save settings: " + error, true);
      }
    }

    ImGui::InputTextWithHint("##filter", "Filter keys...", &state.settings_filter);

    ImGui::BeginChild("settings_scroll", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const std::string filter_lower = to_lower(state.settings_filter);

    if (ImGui::BeginTable("settings_table", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
      ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 260.0F);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

      for (auto &[key, value] : state.config_values) {
        if (!filter_lower.empty() && to_lower(key).find(filter_lower) == std::string::npos) {
          continue;
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(key.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText(("##" + key).c_str(), &value);
      }

      ImGui::EndTable();
    }
    ImGui::EndChild();
  }

}  // namespace ui

#endif
