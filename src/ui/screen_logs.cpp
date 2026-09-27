/**
 * @file src/ui/screen_logs.cpp
 * @brief Definitions for the embedded UI's log viewer screen.
 */
#ifdef _WIN32

// lib includes
#include <imgui.h>

// local includes
#include "screen_logs.h"
#include "src/config.h"
#include "src/file_handler.h"

namespace ui {

  namespace {
    std::string log_contents;
    bool loaded = false;

    void reload() {
      log_contents = file_handler::read_file(config::sunshine.log_file.c_str());
      loaded = true;
    }
  }  // namespace

  void render_logs_screen() {
    if (!loaded) {
      reload();
    }

    if (ImGui::Button("Reload")) {
      reload();
    }

    ImGui::BeginChild("log_scroll", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(log_contents.c_str(), log_contents.c_str() + log_contents.size());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
      ImGui::SetScrollHereY(1.0F);
    }
    ImGui::EndChild();
  }

}  // namespace ui

#endif
