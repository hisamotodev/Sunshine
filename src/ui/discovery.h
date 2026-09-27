/**
 * @file src/ui/discovery.h
 * @brief Discovers other running Titan instances via their %LOCALAPPDATA%\Titan\<name>\
 *        instance.json manifest (written by main.cpp at startup).
 */
#pragma once

#ifdef _WIN32

// standard includes
#include <cstdint>
#include <string>
#include <vector>

namespace ui {

  struct DiscoveredInstance {
    std::string name;  ///< AppData/Local directory name (== config::sunshine.instance_name resolution).
    std::string display_name;
    std::uint16_t confighttp_port = 0;
  };

  /**
   * @brief Scan %LOCALAPPDATA%\Titan\*\instance.json for other live instances.
   *
   * @param self_instance_name This process's own instance name, excluded from the results.
   * @return Discovered instances, excluding self and any whose pid is no longer running.
   */
  std::vector<DiscoveredInstance> discover_other_instances(const std::string &self_instance_name);

}  // namespace ui

#endif
