/**
 * @file src/platform/windows/misc.h
 * @brief Miscellaneous declarations for Windows.
 */
#pragma once

// standard includes
#include <chrono>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>

// platform includes
#include <Windows.h>
#include <winnt.h>

namespace platf {
  /**
   * @brief Write status details to the log.
   *
   * @param prefix Text prefix used when formatting the message.
   * @param status Native status code returned by the platform API.
   */
  void print_status(const std::string_view &prefix, HRESULT status);
  /**
   * @brief Synchronize thread desktop.
   *
   * @return true when the thread desktop was synchronized successfully.
   */
  HDESK syncThreadDesktop();

  /**
   * @brief Read the current Windows high-resolution performance counter.
   *
   * @return Raw QPC tick value from `QueryPerformanceCounter`.
   */
  int64_t qpc_counter();

  /**
   * @brief Convert the difference between two QPC readings to nanoseconds.
   *
   * @param performance_counter1 Newer performance-counter reading.
   * @param performance_counter2 Older performance-counter reading.
   * @return Duration represented by the difference between two QPC values.
   */
  std::chrono::nanoseconds qpc_time_difference(int64_t performance_counter1, int64_t performance_counter2);

  /**
   * @brief Get file version information from a Windows executable or driver file.
   * @param file_path Path to the file to query.
   * @param version_str Output parameter for version string in format "major.minor.build.revision".
   * @return true if version info was successfully extracted, false otherwise.
   */
  bool getFileVersionInfo(const std::filesystem::path &file_path, std::string &version_str);

  /**
   * @brief Enumerate the process ids currently in a Job Object.
   *
   * Shared by `request_process_group_exit()` and (agent.md section 7.3)
   * `window_capture`'s HWND resolution, both of which need "which processes
   * belong to this launched app" from the same Job Object handle
   * (`boost::process::v1::group::native_handle()`).
   *
   * @param native_handle The native handle of the process group (Job Object).
   * @return Process ids currently in the job (empty if all have exited), or
   *         `std::nullopt` if the Job Object query itself failed.
   */
  std::optional<std::set<DWORD>> process_group_pids(std::uintptr_t native_handle);
}  // namespace platf
