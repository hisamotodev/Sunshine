/**
 * @file src/ui/discovery.cpp
 * @brief Definitions for cross-instance discovery.
 */
#ifdef _WIN32

// standard includes
#include <filesystem>
#include <fstream>
#include <sstream>

// lib includes
#include <nlohmann/json.hpp>

// clang-format off
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// clang-format on

// local includes
#include "discovery.h"

namespace ui {

  namespace {

    bool process_is_running(DWORD pid) {
      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
      if (!process) {
        return false;
      }
      DWORD exit_code = 0;
      const bool running = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
      CloseHandle(process);
      return running;
    }

  }  // namespace

  std::vector<DiscoveredInstance> discover_other_instances(const std::string &self_instance_name) {
    std::vector<DiscoveredInstance> instances;

    WCHAR local_appdata[MAX_PATH];
    auto len = GetEnvironmentVariableW(L"LOCALAPPDATA", local_appdata, _countof(local_appdata));
    if (len == 0 || len >= _countof(local_appdata)) {
      return instances;
    }

    std::filesystem::path titan_dir = std::filesystem::path {local_appdata} / L"Titan";
    std::error_code ec;
    if (!std::filesystem::exists(titan_dir, ec)) {
      return instances;
    }

    for (const auto &entry : std::filesystem::directory_iterator {titan_dir, ec}) {
      if (!entry.is_directory()) {
        continue;
      }

      const auto name = entry.path().filename().string();
      if (name == self_instance_name) {
        continue;
      }

      auto manifest_path = entry.path() / "instance.json";
      std::ifstream file(manifest_path, std::ios::binary);
      if (!file) {
        continue;
      }
      std::ostringstream contents;
      contents << file.rdbuf();

      try {
        const auto parsed = nlohmann::json::parse(contents.str());
        const auto pid = parsed.value("pid", 0U);
        if (pid == 0 || !process_is_running(pid)) {
          continue;  // stale manifest from a process that's no longer running
        }

        DiscoveredInstance instance;
        instance.name = name;
        instance.display_name = parsed.value("display_name", name);
        instance.confighttp_port = static_cast<std::uint16_t>(parsed.value("confighttp_port", 0));
        instances.push_back(std::move(instance));
      } catch (const std::exception &) {
        continue;  // malformed manifest, skip it
      }
    }

    return instances;
  }

}  // namespace ui

#endif
