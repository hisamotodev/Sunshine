/**
 * @file src/ui/screen_virtualhid.cpp
 * @brief Definitions for the embedded UI's Virtual HID Driver status/license screen.
 */
#ifdef _WIN32

// lib includes
#include <imgui.h>
#include <libvirtualhid/license.hpp>
#include <misc/cpp/imgui_stdlib.h>

// local includes
#include "screen_virtualhid.h"
#include "src/confighttp.h"
#include "src/input.h"

namespace ui {

  namespace {
    std::string status_json;
    bool status_loaded = false;
    std::string license_key;
    std::string status_message;

    void refresh_status() {
      nlohmann::json tree;
      tree["virtualhid"] = confighttp::get_virtualhid_driver_status();
      tree["vigembus"] = confighttp::get_vigembus_driver_status();
      status_json = tree.dump(2);
      status_loaded = true;
    }

    void apply_license_result(const lvh::LicenseResult &result) {
      status_message = confighttp::build_virtualhid_license_status(result).dump(2);
      if (result.status.ok()) {
        input::refresh_virtual_input();
      }
      refresh_status();
    }
  }  // namespace

  void render_virtualhid_screen() {
    if (!status_loaded) {
      refresh_status();
    }

    if (ImGui::Button("Reload Status")) {
      refresh_status();
    }

    ImGui::BeginChild("virtualhid_status", ImVec2(0, 220), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(status_json.c_str());
    ImGui::EndChild();

    ImGui::SeparatorText("License");
    ImGui::InputText("License key", &license_key, ImGuiInputTextFlags_Password);
    if (ImGui::Button("Activate")) {
      auto result = lvh::activate_license(license_key);
      license_key.clear();
      apply_license_result(result);
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate")) {
      apply_license_result(lvh::validate_license());
    }
    ImGui::SameLine();
    if (ImGui::Button("Deactivate")) {
      apply_license_result(lvh::deactivate_license());
    }

    if (!status_message.empty()) {
      ImGui::BeginChild("license_result", ImVec2(0, 140), ImGuiChildFlags_Borders);
      ImGui::TextUnformatted(status_message.c_str());
      ImGui::EndChild();
    }
  }

}  // namespace ui

#endif
