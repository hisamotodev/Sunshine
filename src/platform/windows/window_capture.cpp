/**
 * @file src/platform/windows/window_capture.cpp
 * @brief See window_capture.h.
 */
#include "window_capture.h"

// platform includes
#include <dwmapi.h>


// standard includes
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <set>
#include <string_view>
#include <thread>

// local includes
#include "misc.h"
#include "src/logging.h"

using namespace std::literals;

namespace platf::dxgi::window_capture {
  namespace {
    struct criteria_t {
      std::uintptr_t job_handle = 0;
      std::wstring window_class;
      std::wstring window_title;
      std::chrono::milliseconds timeout {10000};
    };

    struct enum_ctx_t {
      const std::set<DWORD> *pids;
      const std::wstring *window_class;
      const std::wstring *window_title;
      HWND best = nullptr;
    };

    // Guards every field below. Contention is negligible: at most one search
    // thread plus occasional reads from wgc_capture_t::init() (once per
    // display (re)init, not per frame).
    std::mutex g_mutex;
    state_e g_state = state_e::idle;
    std::optional<window_capture_target_t> g_target;
    std::optional<criteria_t> g_criteria;
    uint64_t g_generation = 0;
    std::thread g_worker;

    /**
     * @brief `EnumWindows` callback: accepts the first visible top-level
     * window owned by a PID in `ctx->pids` that also matches the configured
     * class/title filters (agent.md 7.3 steps 3-4 -- PID membership first,
     * class/title as additional narrowing, never "biggest visible window"
     * alone).
     */
    BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM lparam) {
      auto *ctx = reinterpret_cast<enum_ctx_t *>(lparam);

      if (!IsWindowVisible(hwnd)) {
        return TRUE;
      }

      DWORD pid = 0;
      GetWindowThreadProcessId(hwnd, &pid);
      if (ctx->pids->find(pid) == ctx->pids->end()) {
        return TRUE;
      }

      if (!ctx->window_class->empty()) {
        wchar_t class_name[256] {};
        GetClassNameW(hwnd, class_name, ARRAYSIZE(class_name));
        if (_wcsicmp(class_name, ctx->window_class->c_str()) != 0) {
          return TRUE;
        }
      }

      if (!ctx->window_title->empty()) {
        wchar_t title[256] {};
        GetWindowTextW(hwnd, title, ARRAYSIZE(title));
        if (wcsstr(title, ctx->window_title->c_str()) == nullptr) {
          return TRUE;
        }
      }

      ctx->best = hwnd;
      return FALSE;  // stop enumeration, we found an acceptable match
    }

    std::optional<HWND> find_window_for_pids(const std::set<DWORD> &pids, const std::wstring &window_class, const std::wstring &window_title) {
      enum_ctx_t ctx {&pids, &window_class, &window_title};
      EnumWindows(enum_windows_proc, reinterpret_cast<LPARAM>(&ctx));
      if (ctx.best) {
        return ctx.best;
      }
      return std::nullopt;
    }

    /**
     * @brief DWM's extended frame bounds for `hwnd` -- see `title_bar_height()`
     * and `capture_origin()`'s doc comments for why this (not `GetWindowRect`)
     * is the rect WGC actually frames a window at.
     */
    std::optional<RECT> extended_frame_bounds(HWND hwnd) {
      RECT bounds {};
      if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))) {
        return std::nullopt;
      }
      return bounds;
    }

    /**
     * @brief `EnumWindows` callback: collects every visible top-level window
     * whose owner chain (`GA_ROOTOWNER`) leads back to `ctx->target`, e.g.
     * a context menu, combo-box dropdown, or VLC's fullscreen toolbar
     * controller. Unlike `enum_windows_proc` above, this does not stop at
     * the first match -- a target can have more than one owned popup open
     * at once.
     */
    struct owned_enum_ctx_t {
      HWND target;
      std::vector<HWND> *out;
    };

    BOOL CALLBACK enum_owned_windows_proc(HWND hwnd, LPARAM lparam) {
      auto *ctx = reinterpret_cast<owned_enum_ctx_t *>(lparam);

      if (hwnd == ctx->target || !IsWindowVisible(hwnd)) {
        return TRUE;
      }
      if (GetAncestor(hwnd, GA_ROOTOWNER) != ctx->target) {
        return TRUE;
      }

      RECT rect;
      if (!GetWindowRect(hwnd, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) {
        return TRUE;
      }

      ctx->out->push_back(hwnd);
      return TRUE;
    }

    /**
     * @brief GDI `PrintWindow` capture of a single window into a top-down
     * 32bpp BGRA buffer sized to `rect` (a `GetWindowRect()` result).
     */
    struct raw_capture_t {
      int width = 0;
      int height = 0;
      std::vector<std::uint8_t> bgra;
    };

    std::optional<raw_capture_t> print_window(HWND hwnd, const RECT &rect) {
      int w = rect.right - rect.left;
      int h = rect.bottom - rect.top;
      if (w <= 0 || h <= 0) {
        return std::nullopt;
      }

      HDC screen_dc = GetDC(nullptr);
      if (!screen_dc) {
        return std::nullopt;
      }
      HDC mem_dc = CreateCompatibleDC(screen_dc);
      ReleaseDC(nullptr, screen_dc);
      if (!mem_dc) {
        return std::nullopt;
      }

      BITMAPINFO bmi {};
      bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth = w;
      bmi.bmiHeader.biHeight = -h;  // top-down
      bmi.bmiHeader.biPlanes = 1;
      bmi.bmiHeader.biBitCount = 32;
      bmi.bmiHeader.biCompression = BI_RGB;

      void *bits = nullptr;
      HBITMAP bitmap = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
      if (!bitmap) {
        DeleteDC(mem_dc);
        return std::nullopt;
      }
      HGDIOBJ old_bitmap = SelectObject(mem_dc, bitmap);

      BOOL ok = PrintWindow(hwnd, mem_dc, PW_RENDERFULLCONTENT);

      std::optional<raw_capture_t> result;
      if (ok) {
        raw_capture_t capture;
        capture.width = w;
        capture.height = h;
        capture.bgra.resize(static_cast<std::size_t>(w) * h * 4);
        std::memcpy(capture.bgra.data(), bits, capture.bgra.size());
        result = std::move(capture);
      }

      SelectObject(mem_dc, old_bitmap);
      DeleteObject(bitmap);
      DeleteDC(mem_dc);
      return result;
    }

    /**
     * @brief Diagnostic test: Windows 11 rounds a window's corners by
     * default (DWM-composited, purely cosmetic -- doesn't change the
     * window's rect/style, unlike the old WS_CAPTION strip this module used
     * to do). Suspected of leaving a faint few-pixel artifact in a
     * window-target WGC capture that a live-tested black-bar gap survived
     * every resolution/crop fix tried so far. Disabling it doesn't affect
     * local window operability (cosmetic only), so unlike WS_CAPTION this
     * needs no crop-based workaround if it turns out to matter.
     */
    void disable_rounded_corners(HWND hwnd) {
      DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_DONOTROUND;
      DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));

      // Disabling rounded corners alone left a 1px edge on three of four
      // sides in live testing -- Windows 11 also draws a thin accent-color
      // border around a window (a separate DWM feature from corner
      // rounding), which DWMWA_COLOR_NONE turns off.
      COLORREF border_color = DWMWA_COLOR_NONE;
      DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border_color, sizeof(border_color));
    }

    /**
     * @brief Detaches the current `g_worker` (if any). Must be called with
     * `g_mutex` held. The detached thread notices it's been superseded via
     * the generation check in `run_search()` and exits on its own; detaching
     * (rather than joining) avoids blocking the caller for up to 200ms.
     */
    void detach_previous_worker_locked() {
      if (g_worker.joinable()) {
        g_worker.detach();
      }
    }

    void run_search(uint64_t generation, criteria_t criteria) {
      auto deadline = std::chrono::steady_clock::now() + criteria.timeout;
      for (;;) {
        {
          std::lock_guard lock(g_mutex);
          if (g_generation != generation) {
            return;  // superseded by a newer begin_resolution()/on_window_closed()/clear()
          }
        }

        auto pids = platf::process_group_pids(criteria.job_handle);
        if (pids) {
          if (auto hwnd = find_window_for_pids(*pids, criteria.window_class, criteria.window_title)) {
            disable_rounded_corners(*hwnd);
            std::lock_guard lock(g_mutex);
            if (g_generation != generation) {
              return;
            }
            window_capture_target_t target;
            target.hwnd = *hwnd;
            GetWindowThreadProcessId(*hwnd, &target.process_id);
            target.window_class = criteria.window_class;
            target.window_title = criteria.window_title;
            g_target = target;
            g_state = state_e::ready;
            BOOST_LOG(info) << "window_capture: resolved capture target HWND for PID "sv << target.process_id;
            return;
          }
        }

        if (std::chrono::steady_clock::now() >= deadline) {
          std::lock_guard lock(g_mutex);
          if (g_generation == generation) {
            g_state = state_e::failed;
            BOOST_LOG(warning) << "window_capture: timed out waiting for a matching window (job_handle="sv << criteria.job_handle << ')';
          }
          return;
        }

        std::this_thread::sleep_for(200ms);
      }
    }
  }  // namespace

  void begin_resolution(std::uintptr_t job_handle, std::wstring window_class, std::wstring window_title, std::chrono::milliseconds timeout) {
    criteria_t criteria {job_handle, std::move(window_class), std::move(window_title), timeout};

    std::lock_guard lock(g_mutex);
    auto generation = ++g_generation;
    g_criteria = criteria;
    g_target.reset();
    g_state = state_e::pending;
    detach_previous_worker_locked();
    g_worker = std::thread(run_search, generation, std::move(criteria));
  }

  state_e resolution_state() {
    std::lock_guard lock(g_mutex);
    return g_state;
  }

  std::optional<window_capture_target_t> resolved_target() {
    std::lock_guard lock(g_mutex);
    return g_target;
  }

  void on_window_closed() {
    std::lock_guard lock(g_mutex);
    if (!g_criteria) {
      return;  // nothing to re-arm against (already cleared, or a monitor's Closed event)
    }
    BOOST_LOG(info) << "window_capture: capture target window closed, re-resolving"sv;
    g_target.reset();
    g_state = state_e::pending;
    auto generation = ++g_generation;
    auto criteria = *g_criteria;
    detach_previous_worker_locked();
    g_worker = std::thread(run_search, generation, std::move(criteria));
  }

  void clear() {
    std::lock_guard lock(g_mutex);
    ++g_generation;
    g_criteria.reset();
    g_target.reset();
    g_state = state_e::idle;
    detach_previous_worker_locked();
  }

  int title_bar_height(HWND hwnd) {
    // DwmGetWindowAttribute's extended frame bounds (not GetWindowRect,
    // which on Win10+ includes an invisible resize-border pad WGC doesn't
    // capture as visible content) gives the same top edge WGC frames a
    // window at. ClientToScreen's mapped (0,0) gives the client area's
    // actual top edge for this window's current style/DPI. The difference
    // is exactly the caption (+ any visible border) height.
    POINT client_origin {0, 0};
    auto frame_bounds = extended_frame_bounds(hwnd);
    if (!frame_bounds || !ClientToScreen(hwnd, &client_origin)) {
      return 0;
    }
    auto height = client_origin.y - frame_bounds->top;
    return height > 0 ? height : 0;
  }

  std::optional<RECT> frame_bounds(HWND hwnd) {
    return extended_frame_bounds(hwnd);
  }

  POINT capture_origin(HWND hwnd, int content_crop_top) {
    auto frame_bounds = extended_frame_bounds(hwnd);
    if (!frame_bounds) {
      return POINT {0, 0};
    }
    return POINT {frame_bounds->left, frame_bounds->top + content_crop_top};
  }

  std::vector<overlay_region_t> capture_owned_window_overlays(HWND target, POINT origin, int frame_width, int frame_height) {
    std::vector<overlay_region_t> result;
    if (!target || frame_width <= 0 || frame_height <= 0) {
      return result;
    }

    std::vector<HWND> owned;
    owned_enum_ctx_t ctx {target, &owned};
    EnumWindows(enum_owned_windows_proc, reinterpret_cast<LPARAM>(&ctx));
    if (owned.empty()) {
      return result;
    }

    // EnumWindows visits top-level windows in Z-order, topmost first; paint
    // back-to-front here so a genuinely topmost popup still ends up on top
    // once the caller composites this list in order.
    std::reverse(owned.begin(), owned.end());

    for (HWND hwnd : owned) {
      RECT rect;
      if (!GetWindowRect(hwnd, &rect)) {
        continue;
      }

      // Translate to frame-local coordinates, then clip to the frame -- a
      // popup can legitimately extend past the target window's edges (most
      // dropdowns/context menus do).
      int local_left = rect.left - origin.x;
      int local_top = rect.top - origin.y;
      int local_right = rect.right - origin.x;
      int local_bottom = rect.bottom - origin.y;

      int clip_left = std::max(local_left, 0);
      int clip_top = std::max(local_top, 0);
      int clip_right = std::min(local_right, frame_width);
      int clip_bottom = std::min(local_bottom, frame_height);
      if (clip_right <= clip_left || clip_bottom <= clip_top) {
        continue;  // fully off-frame
      }

      auto raw = print_window(hwnd, rect);
      if (!raw) {
        continue;
      }

      overlay_region_t region;
      region.x = clip_left;
      region.y = clip_top;
      region.width = clip_right - clip_left;
      region.height = clip_bottom - clip_top;
      region.bgra.resize(static_cast<std::size_t>(region.width) * region.height * 4);

      int src_x_offset = clip_left - local_left;
      int src_y_offset = clip_top - local_top;
      for (int row = 0; row < region.height; ++row) {
        const auto *src_row = raw->bgra.data() + static_cast<std::size_t>(src_y_offset + row) * raw->width * 4 + static_cast<std::size_t>(src_x_offset) * 4;
        auto *dst_row = region.bgra.data() + static_cast<std::size_t>(row) * region.width * 4;
        std::memcpy(dst_row, src_row, static_cast<std::size_t>(region.width) * 4);
      }

      result.push_back(std::move(region));
    }

    return result;
  }
}  // namespace platf::dxgi::window_capture
