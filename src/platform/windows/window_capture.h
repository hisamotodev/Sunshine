/**
 * @file src/platform/windows/window_capture.h
 * @brief Resolves a launched app's target HWND for `wgc_capture_t`, per
 * agent.md sections 7.1-7.3 and 7.7.
 *
 * Replaces the `SUNSHINE_POC_CAPTURE_HWND` env-var hook used through PoC 3
 * (see docs/research/poc3-titan-hwnd-capture.md): `proc_t::execute()` calls
 * `begin_resolution()` right after launching an app with `capture_window`
 * set, and `wgc_capture_t::init()` (display_wgc.cpp) queries
 * `resolved_target()` instead of reading an env var. An app's Job Object
 * lifetime is longer than any single HWND's, so this module also re-arms
 * resolution on its own after the previously-resolved window closes
 * (`clear()` + `on_window_closed()`), without display_wgc.cpp needing to
 * know how the recovery happens -- it only asks "is there a target right
 * now."
 */
#pragma once

// standard includes
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>

// platform includes
#include <Windows.h>

namespace platf::dxgi::window_capture {
  /**
   * @brief A resolved HWND capture target, per agent.md section 7.2.
   *
   * `hwnd` is intentionally not treated as a long-lived identifier anywhere
   * that consumes this struct -- it is re-resolved from `process_id` any
   * time the app or its window is relaunched.
   */
  struct window_capture_target_t {
    HWND hwnd = nullptr;
    DWORD process_id = 0;
    std::wstring window_class;
    std::wstring window_title;
  };

  /**
   * @brief Resolution progress for the currently-launched app, per agent.md
   * section 8.3's `state` field ("starting"/"ready"/failed).
   */
  enum class state_e {
    idle,  ///< No app with `capture_window` is currently launching/running.
    pending,  ///< Searching for a matching top-level window.
    ready,  ///< A window has been resolved; see `resolved_target()`.
    failed,  ///< Resolution timed out with no match.
  };

  /**
   * @brief Begin (or restart) searching for the launched app's target window.
   *
   * Safe to call again while a previous search for a *different* job is
   * still in flight -- it cancels that search first. Runs its polling loop
   * on a background thread; does not block the caller.
   *
   * @param job_handle Native handle of the app's process group Job Object
   *        (`boost::process::v1::group::native_handle()`), used to resolve
   *        which PIDs belong to this app via `platf::process_group_pids()`
   *        (covers the launcher -> game PID handoff case from agent.md
   *        section 7.3 / 15's "technical matrix").
   * @param window_class Optional `window-class` filter from apps.json (empty
   *        to accept any class).
   * @param window_title Optional window title filter (empty to accept any
   *        title). Currently unused by apps.json parsing but kept for
   *        completeness/future use, per agent.md 7.3 step 4.
   * @param timeout How long to keep searching before giving up (`failed`).
   */
  void begin_resolution(std::uintptr_t job_handle, std::wstring window_class, std::wstring window_title, std::chrono::milliseconds timeout);

  /**
   * @return The current resolution state for the in-flight/most recent search.
   */
  state_e resolution_state();

  /**
   * @return The resolved target, if `resolution_state() == state_e::ready`.
   */
  std::optional<window_capture_target_t> resolved_target();

  /**
   * @brief Called from `wgc_capture_t`'s `GraphicsCaptureItem::Closed` handler
   * when the currently-resolved window goes away. Re-arms a fresh search
   * using the same job/class/title/timeout `begin_resolution()` was last
   * called with, so a launcher's window closing and the real game's window
   * appearing later is treated as "still resolving," not "capture failed
   * permanently."
   */
  void on_window_closed();

  /**
   * @brief Stop searching and discard any resolved target. Called from
   * `proc_t::terminate()` once the app itself has fully exited.
   */
  void clear();

  /**
   * @brief Rows of title bar (+ any visible top border) `wgc_capture_t::init()`
   * (display_wgc.cpp) crops off a window-target capture, so the stream never
   * shows one -- see its call site for why this crops the frame instead of
   * stripping the real window's `WS_CAPTION` style. Also used by rtsp.cpp's
   * capture-window resolution override so the `STREAM_CONFIGURATION` height
   * it negotiates with the client matches what will actually be encoded;
   * both call sites must agree or Hunter letterboxes the (shorter) real
   * video inside a canvas sized for the window's full, uncropped height.
   *
   * @param hwnd The target window.
   * @return The row count to crop, or 0 if it can't be determined (e.g. the
   *         window has no non-client top area, or the DWM query failed).
   */
  int title_bar_height(HWND hwnd);

  /**
   * @brief The rect WGC actually frames a window-target capture at --
   * `DwmGetWindowAttribute`'s extended frame bounds, not `GetWindowRect`,
   * which on Win10+ includes an invisible resize-border pad (a handful of
   * pixels per side) WGC's `GraphicsCaptureItem::Size()` never captures.
   * nvhttp.cpp's remote-run-status endpoint and rtsp.cpp's capture-window
   * resolution override both report a window's size to/for Hunter and must
   * use this, not `GetWindowRect`, or the negotiated width comes out a few
   * pixels wider than the real video -- seen live as thin black bars down
   * both sides once `title_bar_height()`'s crop had already fixed the
   * height to agree.
   *
   * @param hwnd The target window.
   * @return The bounds, or `std::nullopt` if the DWM query failed.
   */
  std::optional<RECT> frame_bounds(HWND hwnd);

  /**
   * @brief Rows/columns still left over on the sides after `title_bar_height()`
   * and `frame_bounds()` (DWM extended frame bounds, not `GetWindowRect`)
   * already account for the invisible resize-border pad. Live testing found
   * WGC's `GraphicsCaptureItem::Size()` for a window-target capture is
   * consistently 1px narrower per side than `frame_bounds()`'s width across
   * every app tried, root cause unconfirmed (docs/research's other
   * unconfirmed-root-cause driver quirks on this dev machine) -- rather than
   * keep chasing it, crop this fixed, small amount off both sides
   * everywhere a window-target capture's width is computed (display_wgc.cpp's
   * crop, and nvhttp.cpp/rtsp.cpp's negotiated width), so all three still
   * agree with each other even though none of them measure the real
   * WGC-captured width exactly.
   */
  constexpr int kWindowCaptureSideCropPx = 3;

}  // namespace platf::dxgi::window_capture
