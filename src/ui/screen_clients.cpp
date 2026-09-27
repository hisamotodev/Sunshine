/**
 * @file src/ui/screen_clients.cpp
 * @brief Definitions for the embedded UI's paired-clients screen.
 */
#ifdef _WIN32

// lib includes
#include <imgui.h>

// local includes
#include "screen_clients.h"
#include "src/nvhttp.h"
#include "src/process.h"
#include "src/rtsp.h"

namespace ui {

  namespace {
    nlohmann::json clients = nlohmann::json::array();
    bool clients_loaded = false;
    std::string status_message;

    void refresh_clients() {
      clients = nvhttp::get_all_clients();
      clients_loaded = true;
    }

    // Mirrors confighttp.cpp's updateClient(): stop any active session for a
    // client being disabled, and stop the app entirely if nothing is left streaming.
    void set_enabled(const std::string &uuid, bool enabled) {
      if (!nvhttp::set_client_enabled(uuid, enabled)) {
        return;
      }
      if (!enabled) {
        auto cert = nvhttp::get_cert_by_uuid(uuid);
        if (!cert.empty()) {
          rtsp_stream::terminate_sessions_by_cert(cert);
        }
        if (rtsp_stream::session_count() == 0 && proc::proc.running() > 0) {
          proc::proc.terminate();
        }
      }
      refresh_clients();
    }

    // Mirrors confighttp.cpp's unpair(): stop the app if no clients remain paired.
    void unpair(const std::string &uuid) {
      if (nvhttp::unpair_client(uuid) && nvhttp::get_all_clients().empty()) {
        proc::proc.terminate();
      }
      refresh_clients();
    }
  }  // namespace

  void render_clients_screen() {
    if (!clients_loaded) {
      refresh_clients();
    }

    if (ImGui::Button("Reload")) {
      refresh_clients();
    }
    ImGui::SameLine();
    if (ImGui::Button("Unpair All")) {
      nvhttp::erase_all_clients();
      proc::proc.terminate();
      status_message = "All clients unpaired.";
      refresh_clients();
    }

    if (clients.empty()) {
      ImGui::TextDisabled("No paired clients.");
    }

    if (ImGui::BeginTable("clients_table", 4, ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Name");
      ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 80.0F);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 90.0F);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 90.0F);

      for (auto &client : clients) {
        const std::string uuid = client.value("uuid", "");
        const std::string name = client.value("name", std::string {"(unnamed)"});
        bool enabled = client.value("enabled", true);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(name.c_str());

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Checkbox(("##enabled_" + uuid).c_str(), &enabled)) {
          set_enabled(uuid, enabled);
          break;  // `clients` was reloaded by set_enabled(); stop iterating this frame.
        }

        ImGui::TableSetColumnIndex(3);
        if (ImGui::Button(("Unpair##" + uuid).c_str())) {
          unpair(uuid);
          break;  // `clients` was reloaded by unpair(); stop iterating this frame.
        }
      }

      ImGui::EndTable();
    }

    if (!status_message.empty()) {
      ImGui::TextColored(ImVec4(0.5F, 0.9F, 0.5F, 1.0F), "%s", status_message.c_str());
    }
  }

}  // namespace ui

#endif
