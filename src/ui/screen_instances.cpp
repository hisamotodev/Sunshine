/**
 * @file src/ui/screen_instances.cpp
 * @brief Definitions for the embedded UI's "Other Instances" screen.
 */
#ifdef _WIN32

// standard includes
#include <memory>

// lib includes
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

// local includes
#include "app_state.h"
#include "credential_cache.h"
#include "discovery.h"
#include "remote_client.h"
#include "screen_instances.h"
#include "src/config.h"
#include "ui_apps.h"
#include "ui_settings.h"

namespace ui {

  namespace {
    std::vector<DiscoveredInstance> discovered;
    bool discovered_loaded = false;
    int selected_index = -1;

    std::string username_input;
    std::string password_input;

    AppState remote_state;
    CredentialCache credential_cache;

    void refresh_discovery() {
      discovered = discover_other_instances(config::sunshine.instance_name);
      discovered_loaded = true;
    }

    void select(int index) {
      selected_index = index;
      remote_state = AppState {};  // drop any previous connection

      if (const auto cached = credential_cache.find(discovered[index].name)) {
        username_input = cached->username;
        password_input = cached->password;
      } else {
        username_input.clear();
        password_input.clear();
      }
    }

    void connect() {
      const auto &instance = discovered[selected_index];

      RemoteCredentials credentials;
      credentials.host = "127.0.0.1";  // discovery only ever finds same-machine instances
      credentials.port = instance.confighttp_port;
      credentials.username = username_input;
      credentials.password = password_input;

      auto client = std::make_unique<RemoteClient>(credentials);
      std::string error;
      if (!client->get_config(error)) {
        remote_state = AppState {};
        remote_state.set_status("Failed to connect: " + error, true);
        return;
      }

      remote_state = AppState {};
      remote_state.client = std::move(client);

      // Only cache credentials that actually worked.
      CachedCredential to_cache;
      to_cache.instance_key = instance.name;
      to_cache.username = username_input;
      to_cache.password = password_input;
      credential_cache.upsert(to_cache);
      credential_cache.save(error);
    }
  }  // namespace

  void render_instances_screen() {
    if (!discovered_loaded) {
      refresh_discovery();
    }

    if (ImGui::Button("Reload")) {
      refresh_discovery();
    }

    if (discovered.empty()) {
      ImGui::TextDisabled("No other Titan instances discovered on this machine.");
    }

    ImGui::BeginChild("discovered_instances", ImVec2(260, 100), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(discovered.size()); ++i) {
      const bool is_selected = selected_index == i;
      if (ImGui::Selectable((discovered[i].display_name + "##" + discovered[i].name).c_str(), is_selected)) {
        select(i);
      }
    }
    ImGui::EndChild();

    if (selected_index < 0 || selected_index >= static_cast<int>(discovered.size())) {
      return;
    }

    ImGui::InputText("Username", &username_input);
    ImGui::InputText("Password", &password_input, ImGuiInputTextFlags_Password);
    if (ImGui::Button("Connect")) {
      connect();
    }

    if (!remote_state.status_message.empty()) {
      ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.4F, 1.0F), "%s", remote_state.status_message.c_str());
    }

    if (!remote_state.client) {
      return;
    }

    ImGui::Separator();
    if (ImGui::BeginTabBar("remote_instance_tabs")) {
      if (ImGui::BeginTabItem("Settings")) {
        render_settings_tab(remote_state);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Apps")) {
        render_apps_tab(remote_state);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }

}  // namespace ui

#endif
