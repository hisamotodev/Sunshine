/**
 * @file src/ui/screen_pairing.cpp
 * @brief Definitions for the embedded UI's pairing PIN entry screen.
 */
#ifdef _WIN32

// standard includes
#include <atomic>
#include <thread>

// lib includes
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

// local includes
#include "screen_pairing.h"
#include "src/nvhttp.h"

namespace ui {

  namespace {
    std::vector<nvhttp::pending_pairing_t> pending_pairings;
    bool pairings_loaded = false;

    std::string selected_pairing_id;
    std::string pin_input;
    std::string name_input;

    std::atomic<bool> pairing_in_progress {false};
    std::atomic<bool> pairing_result_ready {false};
    std::atomic<bool> pairing_result_success {false};

    std::string status_message;
    bool status_is_error = false;

    void refresh_pending_pairings() {
      pending_pairings = nvhttp::get_pending_pairings();
      pairings_loaded = true;
    }
  }  // namespace

  void render_pairing_screen() {
    if (!pairings_loaded) {
      refresh_pending_pairings();
    }

    // Pick up the result of a pairing attempt started on a background thread (see
    // below) - nvhttp::pin() blocks for up to the configured ping_timeout while
    // Moonlight completes its handshake, so it must not run on this render thread.
    if (pairing_result_ready.exchange(false)) {
      if (pairing_result_success.load()) {
        status_message = "Paired successfully.";
        status_is_error = false;
        pin_input.clear();
        refresh_pending_pairings();
      } else {
        status_message = "Pairing failed (wrong PIN, timeout, or cancelled).";
        status_is_error = true;
      }
    }

    if (ImGui::Button("Reload")) {
      refresh_pending_pairings();
    }

    ImGui::BeginChild("pending_pairings", ImVec2(300, 120), ImGuiChildFlags_Borders);
    for (const auto &pairing : pending_pairings) {
      const bool is_selected = selected_pairing_id == pairing.id;
      const std::string label = pairing.name + " (" + pairing.address + ")##" + pairing.id;
      if (ImGui::Selectable(label.c_str(), is_selected)) {
        selected_pairing_id = pairing.id;
        name_input = pairing.name;
      }
    }
    ImGui::EndChild();

    if (pending_pairings.empty()) {
      ImGui::TextDisabled("No pending pairing requests.");
    }

    ImGui::InputText("Client name", &name_input);
    ImGui::InputTextWithHint("PIN", "1234", &pin_input, ImGuiInputTextFlags_CharsDecimal);

    ImGui::BeginDisabled(pairing_in_progress.load());
    if (ImGui::Button("Pair")) {
      if (!nvhttp::is_valid_pairing_id(selected_pairing_id)) {
        status_message = "Select a pending pairing request first.";
        status_is_error = true;
      } else if (!nvhttp::is_valid_pairing_pin(pin_input)) {
        status_message = "PIN must be exactly 4 digits.";
        status_is_error = true;
      } else if (!nvhttp::is_valid_pairing_name(name_input)) {
        status_message = "Client name must be 1-128 characters.";
        status_is_error = true;
      } else {
        status_message.clear();
        pairing_in_progress.store(true);
        std::thread([pairing_id = selected_pairing_id, pin = pin_input, name = name_input]() {
          const bool ok = nvhttp::pin(pairing_id, pin, name);
          pairing_result_success.store(ok);
          pairing_in_progress.store(false);
          pairing_result_ready.store(true);
        }).detach();
      }
    }
    ImGui::EndDisabled();

    if (pairing_in_progress.load()) {
      ImGui::SameLine();
      ImGui::TextDisabled("Waiting for client...");
    }

    if (!status_message.empty()) {
      const auto color = status_is_error ? ImVec4(1.0F, 0.4F, 0.4F, 1.0F) : ImVec4(0.5F, 0.9F, 0.5F, 1.0F);
      ImGui::TextColored(color, "%s", status_message.c_str());
    }
  }

}  // namespace ui

#endif
