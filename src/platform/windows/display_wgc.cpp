/**
 * @file src/platform/windows/display_wgc.cpp
 * @brief Definitions for WinRT Windows.Graphics.Capture API
 */
// standard includes
#include <algorithm>
#include <cstring>

// platform includes
#include <dxgi1_2.h>

// local includes
#include "display.h"
#include "misc.h"
#include "src/logging.h"
#include "window_capture.h"

// Gross hack to work around MINGW-packages#22160
#define ____FIReference_1_boolean_INTERFACE_DEFINED__

#include <Windows.Graphics.Capture.Interop.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.foundation.metadata.h>
#include <winrt/windows.graphics.directx.direct3d11.h>

namespace platf {
  using namespace std::literals;
}

namespace winrt {
  using namespace Windows::Foundation;
  using namespace Windows::Foundation::Metadata;
  using namespace Windows::Graphics::Capture;
  using namespace Windows::Graphics::DirectX::Direct3D11;

  extern "C" {
    /**
     * @brief Create direct3 D11 device from DXGI device.
     *
     * @param dxgiDevice DXGI device.
     * @param graphicsDevice Graphics device.
     * @return Created direct3 D11 device from DXGI device object or status.
     */
    HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(::IDXGIDevice *dxgiDevice, ::IInspectable **graphicsDevice);
  }

  /**
   * Windows structures sometimes have compile-time GUIDs. GCC supports this, but in a roundabout way.
   * If WINRT_IMPL_HAS_DECLSPEC_UUID is true, then the compiler supports adding this attribute to a struct. For example, Visual Studio.
   * If not, then MinGW GCC has a workaround to assign a GUID to a structure.
   */
  struct
#if WINRT_IMPL_HAS_DECLSPEC_UUID
    __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
#endif
    IDirect3DDxgiInterfaceAccess: ::IUnknown {
    /**
     * @brief Retrieve a DXGI interface from a WinRT Direct3D object.
     *
     * @param id COM interface ID requested from the WinRT wrapper.
     * @param object Output pointer that receives the requested COM interface.
     * @return HRESULT from the WinRT object's interface query.
     */
    virtual HRESULT __stdcall GetInterface(REFIID id, void **object) = 0;
  };
}  // namespace winrt
#if !WINRT_IMPL_HAS_DECLSPEC_UUID
static constexpr GUID GUID__IDirect3DDxgiInterfaceAccess = {
  0xA9B3D012,
  0x3DF2,
  0x4EE3,
  {0xB8, 0xD1, 0x86, 0x95, 0xF4, 0x57, 0xD3, 0xC1}
  // compare with __declspec(uuid(...)) for the struct above.
};

/**
 * @brief Return the GUID used to request IDirect3DDxgiInterfaceAccess.
 *
 * @return GUID for the WinRT DXGI interface-access helper.
 */
template<>
constexpr auto __mingw_uuidof<winrt::IDirect3DDxgiInterfaceAccess>() -> GUID const & {
  return GUID__IDirect3DDxgiInterfaceAccess;
}
#endif

namespace platf::dxgi {
  wgc_capture_t::wgc_capture_t() {
    InitializeConditionVariable(&frame_present_cv);
  }

  wgc_capture_t::~wgc_capture_t() {
    if (item) {
      item.Closed(item_closed_token);
    }
    if (capture_session) {
      capture_session.Close();
    }
    if (frame_pool) {
      frame_pool.Close();
    }
    item = nullptr;
    capture_session = nullptr;
    frame_pool = nullptr;
  }

  /**
   * @brief Initialize the Windows.Graphics.Capture backend.
   * @return 0 on success, -1 on failure.
   */
  int wgc_capture_t::init(display_base_t *display, const ::video::config_t &config) {
    HRESULT status;
    dxgi::dxgi_t dxgi;
    winrt::com_ptr<::IInspectable> d3d_comhandle;
    try {
      if (!winrt::GraphicsCaptureSession::IsSupported()) {
        BOOST_LOG(error) << "Screen capture is not supported on this device for this release of Windows!"sv;
        return -1;
      }
      if (FAILED(status = display->device->QueryInterface(IID_IDXGIDevice, (void **) &dxgi))) {
        BOOST_LOG(error) << "Failed to query DXGI interface from device [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }
      if (FAILED(status = winrt::CreateDirect3D11DeviceFromDXGIDevice(*&dxgi, d3d_comhandle.put()))) {
        BOOST_LOG(error) << "Failed to query WinRT DirectX interface from device [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }
    } catch (winrt::hresult_error &e) {
      BOOST_LOG(error) << "Screen capture is not supported on this device for this release of Windows: failed to acquire device: [0x"sv << util::hex(e.code()).to_string_view() << ']';
      return -1;
    }

    uwp_device = d3d_comhandle.as<winrt::IDirect3DDevice>();

    auto capture_item_factory = winrt::get_activation_factory<winrt::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    if (capture_item_factory == nullptr) {
      BOOST_LOG(error) << "Screen capture is not supported on this device for this release of Windows: failed to acquire capture item factory"sv;
      return -1;
    }

    // Capture a specific HWND instead of the display's monitor when the
    // currently-launched app has resolved one (agent.md section 7/8, see
    // window_capture.h). Reuses the same D3D11 device, frame pool, and
    // frame-delivery machinery as monitor capture -- only the
    // GraphicsCaptureItem's target differs.
    auto window_target = window_capture::resolved_target();
    if (window_target) {
      if (FAILED(status = capture_item_factory->CreateForWindow(window_target->hwnd, winrt::guid_of<winrt::IGraphicsCaptureItem>(), winrt::put_abi(item)))) {
        BOOST_LOG(error) << "Failed to create capture item for HWND [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }
      // A window's size has nothing to do with the monitor's mode that
      // display_base_t::init() just computed width/height/env_* from --
      // without this, display_wgc_ram_t::snapshot()'s "did the source
      // texture size change" check (display_wgc.cpp, compares against
      // `width`/`height`) never matches the window's real captured size and
      // reinits on every single frame. This is the video-pipeline half of
      // the sync agent.md section 7.8 calls for; the FramePool half is
      // already handled in on_frame_arrived() below. See
      // docs/research/poc3-titan-hwnd-capture.md.
      auto captured_size = item.Size();

      // WGC always captures a window's full bounding rect, title bar
      // included -- there's no sub-rect capture option. Rather than
      // stripping the target window's own WS_CAPTION style (which left the
      // real window undraggable/unclosable on the host, see
      // window_capture.cpp's history), crop the top `content_crop_top_` rows
      // off after capture and only report the shorter height downstream, so
      // the stream never shows a title bar while the real window stays
      // fully operable locally. rtsp.cpp's capture-window resolution
      // override must crop by this same amount when it negotiates
      // STREAM_CONFIGURATION's height, or Hunter letterboxes this shorter
      // video inside a canvas sized for the window's full height.
      content_crop_top_ = std::min<int>(window_capture::title_bar_height(window_target->hwnd), captured_size.Height - 1);

      // window_capture::kWindowCaptureSideCropPx's doc comment: a fixed,
      // small per-side crop for a gap between frame_bounds()'s width and
      // what WGC actually captures, found consistent across every app tried
      // in live testing.
      content_crop_side_ = std::min<int>(window_capture::kWindowCaptureSideCropPx, (captured_size.Width - 1) / 2);

      display->width = display->width_before_rotation = display->env_width = captured_size.Width - 2 * content_crop_side_;
      // content_crop_side_'s gap turned out to run along the bottom edge
      // too, not just left/right -- title_bar_height()'s much larger crop
      // already happens to swallow the same gap at the top, which is why
      // only the other three edges ever showed it in live testing.
      display->height = display->height_before_rotation = display->env_height = captured_size.Height - content_crop_top_ - content_crop_side_;
      display->offset_x = 0;
      display->offset_y = 0;

      // Popup-compositing support (window_capture.h's
      // capture_owned_window_overlays()) -- the RAM/VRAM snapshot() overrides
      // read these back every frame.
      target_hwnd_ = window_target->hwnd;
      capture_origin_ = window_capture::capture_origin(window_target->hwnd, content_crop_top_);
      capture_origin_.x += content_crop_side_;

      // Win32 routes mouse input (WM_MOUSEMOVE/button messages, and where
      // SendInput's relative deltas actually land) by OS cursor screen
      // position, independent of which window has keyboard focus. If the
      // host's cursor happened to be left outside the target window's
      // bounds from a previous session, every relative move Hunter sends
      // keeps landing on whatever's under the cursor instead of the
      // captured window -- input silently doing nothing there even though
      // the window itself is focused. Recenter into the window on every
      // (re)init so a fresh capture always starts with the cursor over it.
      RECT window_rect;
      if (GetWindowRect(window_target->hwnd, &window_rect)) {
        SetCursorPos(
          (window_rect.left + window_rect.right) / 2,
          (window_rect.top + window_rect.bottom) / 2
        );
      }

      // agent.md section 7.7: notice when this window closes (most commonly
      // a launcher exiting once it has spawned the real game) so we can
      // recover instead of capturing a dead window forever.
      item_closed.store(false);
      item_closed_token = item.Closed({this, &wgc_capture_t::on_item_closed});
    } else {
      DXGI_OUTPUT_DESC output_desc;
      display->output->GetDesc(&output_desc);
      if (FAILED(status = capture_item_factory->CreateForMonitor(output_desc.Monitor, winrt::guid_of<winrt::IGraphicsCaptureItem>(), winrt::put_abi(item)))) {
        BOOST_LOG(error) << "Screen capture is not supported on this device for this release of Windows: failed to acquire display: [0x"sv << util::hex(status).to_string_view() << ']';
        return -1;
      }
    }

    if (config.dynamicRange) {
      display->capture_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    } else {
      display->capture_format = DXGI_FORMAT_B8G8R8A8_UNORM;
    }

    try {
      capture_pixel_format = static_cast<winrt::Windows::Graphics::DirectX::DirectXPixelFormat>(display->capture_format);
      last_content_size = item.Size();
      frame_pool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(uwp_device, capture_pixel_format, 2, last_content_size);
      capture_session = frame_pool.CreateCaptureSession(item);
      frame_pool.FrameArrived({this, &wgc_capture_t::on_frame_arrived});
    } catch (winrt::hresult_error &e) {
      BOOST_LOG(error) << "Screen capture is not supported on this device for this release of Windows: failed to create capture session: [0x"sv << util::hex(e.code()).to_string_view() << ']';
      return -1;
    }
    try {
      if (winrt::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
        capture_session.IsBorderRequired(false);
      } else {
        BOOST_LOG(warning) << "Can't disable colored border around capture area on this version of Windows";
      }
    } catch (winrt::hresult_error &e) {
      BOOST_LOG(warning) << "Screen capture may not be fully supported on this device for this release of Windows: failed to disable border around capture area: [0x"sv << util::hex(e.code()).to_string_view() << ']';
    }
    try {
      if (winrt::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"MinUpdateInterval")) {
        capture_session.MinUpdateInterval(4ms);  // 250Hz
      } else {
        BOOST_LOG(warning) << "Can't set MinUpdateInterval on this version of Windows";
      }
    } catch (winrt::hresult_error &e) {
      BOOST_LOG(warning) << "Screen capture may be capped to 60fps on this device for this release of Windows: failed to set MinUpdateInterval: [0x"sv << util::hex(e.code()).to_string_view() << ']';
    }
    try {
      capture_session.StartCapture();
    } catch (winrt::hresult_error &e) {
      BOOST_LOG(error) << "Screen capture is not supported on this device for this release of Windows: failed to start capture: [0x"sv << util::hex(e.code()).to_string_view() << ']';
      return -1;
    }
    return 0;
  }

  /**
   * This function runs in a separate thread spawned by the frame pool and is a producer of frames.
   * To maintain parity with the original display interface, this frame will be consumed by the capture thread.
   * Acquire a read-write lock, make the produced frame available to the capture thread, then wake the capture thread.
   */
  void wgc_capture_t::on_frame_arrived(winrt::Direct3D11CaptureFramePool const &sender, winrt::IInspectable const &) {
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame {nullptr};
    try {
      frame = sender.TryGetNextFrame();
    } catch (winrt::hresult_error &e) {
      BOOST_LOG(warning) << "Failed to capture frame: "sv << e.code();
      return;
    }
    if (frame != nullptr) {
      // agent.md section 7.8: a resized window's ContentSize changes
      // independently of the frame pool's buffer size -- see the doc
      // comment on last_content_size in display.h. Recreate() the pool to
      // match; only then will next_frame()/snapshot()'s existing
      // desc.Width/desc.Height check (comparing against the buffer's real
      // texture size) notice the change and trigger a full reinit that
      // re-reads the window's new size via a fresh init().
      auto content_size = frame.ContentSize();
      if (content_size.Width != last_content_size.Width || content_size.Height != last_content_size.Height) {
        BOOST_LOG(info) << "WGC content size changed ["sv << last_content_size.Width << 'x' << last_content_size.Height << " -> "sv << content_size.Width << 'x' << content_size.Height << ']';
        try {
          sender.Recreate(uwp_device, capture_pixel_format, 2, content_size);
        } catch (winrt::hresult_error &e) {
          BOOST_LOG(warning) << "Failed to recreate frame pool for new content size: "sv << e.code();
        }
        last_content_size = content_size;
      }

      AcquireSRWLockExclusive(&frame_lock);
      if (produced_frame) {
        produced_frame.Close();
      }

      produced_frame = frame;
      ReleaseSRWLockExclusive(&frame_lock);
      WakeConditionVariable(&frame_present_cv);
    }
  }

  /**
   * @brief `GraphicsCaptureItem::Closed` handler (agent.md section 7.7).
   * Fires when the target window (or, for monitor capture, the underlying
   * output) goes away. Runs on a WinRT-owned thread, not the Sunshine
   * capture thread -- only touches the atomic flag and the (independently
   * synchronized) window_capture module.
   */
  void wgc_capture_t::on_item_closed(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const &, winrt::IInspectable const &) {
    BOOST_LOG(info) << "WGC capture item closed"sv;
    item_closed.store(true);
    window_capture::on_window_closed();
    WakeConditionVariable(&frame_present_cv);
  }

  /**
   * @brief Get the next frame from the producer thread.
   * If not available, the capture thread blocks until one is, or the wait times out.
   */
  capture_e wgc_capture_t::next_frame(std::chrono::milliseconds timeout, ID3D11Texture2D **out, uint64_t &out_time) {
    // this CONSUMER runs in the capture thread
    release_frame();

    if (item_closed.load()) {
      // Force the generic capture-thread teardown/rebuild path (video.cpp)
      // to run: it destroys this display_t (and this wgc_capture_t with it)
      // and constructs a fresh one, whose init() re-reads whatever
      // window_capture has (re-)resolved to by then. `capture_e::error`
      // would instead just kill the capture thread outright -- only
      // `reinit` drives that recovery (see agent.md section 7.7 and the
      // capture_e switch in video.cpp's captureThread()).
      return capture_e::reinit;
    }

    AcquireSRWLockExclusive(&frame_lock);
    if (produced_frame == nullptr && SleepConditionVariableSRW(&frame_present_cv, &frame_lock, timeout.count(), 0) == 0) {
      ReleaseSRWLockExclusive(&frame_lock);
      if (GetLastError() == ERROR_TIMEOUT) {
        return capture_e::timeout;
      } else {
        return capture_e::error;
      }
    }
    if (produced_frame) {
      consumed_frame = produced_frame;
      produced_frame = nullptr;
    }
    ReleaseSRWLockExclusive(&frame_lock);
    if (consumed_frame == nullptr) {  // spurious wakeup
      return capture_e::timeout;
    }

    auto capture_access = consumed_frame.Surface().as<winrt::IDirect3DDxgiInterfaceAccess>();
    if (capture_access == nullptr) {
      return capture_e::error;
    }
    capture_access->GetInterface(IID_ID3D11Texture2D, (void **) out);
    out_time = consumed_frame.SystemRelativeTime().count();  // raw ticks from query performance counter
    return capture_e::ok;
  }

  capture_e wgc_capture_t::release_frame() {
    if (consumed_frame != nullptr) {
      consumed_frame.Close();
      consumed_frame = nullptr;
    }
    return capture_e::ok;
  }

  int wgc_capture_t::set_cursor_visible(bool x) {
    try {
      if (capture_session.IsCursorCaptureEnabled() != x) {
        capture_session.IsCursorCaptureEnabled(x);
      }
      return 0;
    } catch (winrt::hresult_error &) {
      return -1;
    }
  }

  int display_wgc_ram_t::init(const ::video::config_t &config, const std::string &display_name) {
    if (display_base_t::init(config, display_name)) {
      return -1;
    }

    // wgc_capture_t::init() (below) queries window_capture::resolved_target()
    // itself -- see window_capture.h and the PoC 3 -> real design writeup in
    // docs/research/poc3-titan-hwnd-capture.md.
    if (dup.init(this, config)) {
      return -1;
    }

    texture.reset();
    return 0;
  }

  /**
   * @brief Get the next frame from the Windows.Graphics.Capture API and copy it into a new snapshot texture.
   */
  capture_e display_wgc_ram_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    HRESULT status;
    texture2d_t src;
    uint64_t frame_qpc;
    dup.set_cursor_visible(cursor_visible);
    auto capture_status = dup.next_frame(timeout, &src, frame_qpc);
    if (capture_status != capture_e::ok) {
      return capture_status;
    }

    auto frame_timestamp = std::chrono::steady_clock::now() - qpc_time_difference(qpc_counter(), frame_qpc);
    D3D11_TEXTURE2D_DESC desc;
    src->GetDesc(&desc);

    // Create the staging texture if it doesn't exist. It should match the source in size and format.
    if (texture == nullptr) {
      capture_format = desc.Format;
      BOOST_LOG(info) << "Capture format ["sv << dxgi_format_to_string(capture_format) << ']';

      D3D11_TEXTURE2D_DESC t {};
      t.Width = width;
      t.Height = height;
      t.MipLevels = 1;
      t.ArraySize = 1;
      t.SampleDesc.Count = 1;
      t.Usage = D3D11_USAGE_STAGING;
      t.Format = capture_format;
      t.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

      auto status = device->CreateTexture2D(&t, nullptr, &texture);

      if (FAILED(status)) {
        BOOST_LOG(error) << "Failed to create staging texture [0x"sv << util::hex(status).to_string_view() << ']';
        return capture_e::error;
      }
    }

    // It's possible for our display enumeration to race with mode changes and result in
    // mismatched image pool and desktop texture sizes. If this happens, just reinit again.
    // The source (desc) is WGC's uncropped window capture; `width`/`height`
    // are the post-crop dimensions we report downstream, so add the cropped
    // rows/columns back before comparing against the actual source texture
    // (see wgc_capture_t::content_crop_top_/content_crop_side_'s doc
    // comments in display.h).
    if (desc.Width != width + 2 * dup.content_crop_side() || desc.Height != height + dup.content_crop_top() + dup.content_crop_side()) {
      // See wgc_capture_t::resize_settled()'s doc comment: a live drag-resize
      // fires many of these mismatches a second, and reiniting on every one
      // froze the stream mid-drag in live testing -- only commit once the
      // mismatched size has held steady for a bit.
      if (!dup.resize_settled(desc.Width, desc.Height)) {
        return capture_e::timeout;
      }
      BOOST_LOG(info) << "Capture size changed ["sv << (width + 2 * dup.content_crop_side()) << 'x' << (height + dup.content_crop_top() + dup.content_crop_side()) << " -> "sv << desc.Width << 'x' << desc.Height << ']';
      return capture_e::reinit;
    }
    // It's also possible for the capture format to change on the fly. If that happens,
    // reinitialize capture to try format detection again and create new images.
    if (capture_format != desc.Format) {
      BOOST_LOG(info) << "Capture format changed ["sv << dxgi_format_to_string(capture_format) << " -> "sv << dxgi_format_to_string(desc.Format) << ']';
      return capture_e::reinit;
    }

    // Copy from GPU to CPU, cropping off the title bar rows/side columns/
    // bottom row content_crop_top()/content_crop_side() report (zero, i.e.
    // a full copy, for a monitor-target capture).
    D3D11_BOX crop_box {
      (UINT) dup.content_crop_side(),
      (UINT) dup.content_crop_top(),
      0,
      desc.Width - (UINT) dup.content_crop_side(),
      desc.Height - (UINT) dup.content_crop_side(),
      1
    };
    device_ctx->CopySubresourceRegion(texture.get(), 0, 0, 0, 0, src.get(), 0, &crop_box);

    if (!pull_free_image_cb(img_out)) {
      return capture_e::interrupted;
    }
    auto img = (img_t *) img_out.get();

    // Map the staging texture for CPU access (making it inaccessible for the GPU)
    if (FAILED(status = device_ctx->Map(texture.get(), 0, D3D11_MAP_READ, 0, &img_info))) {
      BOOST_LOG(error) << "Failed to map texture [0x"sv << util::hex(status).to_string_view() << ']';

      return capture_e::error;
    }

    // Now that we know the capture format, we can finish creating the image
    if (complete_img(img, false)) {
      device_ctx->Unmap(texture.get(), 0);
      img_info.pData = nullptr;
      return capture_e::error;
    }

    auto row_pitch = img_info.RowPitch;
    std::copy_n((std::uint8_t *) img_info.pData, height * row_pitch, (std::uint8_t *) img->data);

    // Unmap the staging texture to allow GPU access again
    device_ctx->Unmap(texture.get(), 0);
    img_info.pData = nullptr;

    // window_capture.h: WGC's per-window capture only composites the target
    // window's own DWM surface -- paint owned popups (VLC's fullscreen
    // toolbar controller, context menus, combo-box dropdowns) back in here,
    // CPU side, directly into the frame buffer just copied above. Only
    // supported for the 8-bit BGRA capture format (the non-HDR default,
    // display_wgc.cpp's init()); an HDR stream's FP16 texture would need a
    // linear-space conversion this skips for now.
    if (auto target = dup.target_hwnd(); target && capture_format == DXGI_FORMAT_B8G8R8A8_UNORM) {
      for (auto &region : window_capture::capture_owned_window_overlays(target, dup.capture_origin(), width, height)) {
        for (int row = 0; row < region.height; ++row) {
          std::memcpy(
            (std::uint8_t *) img->data + (std::size_t) (region.y + row) * row_pitch + (std::size_t) region.x * 4,
            region.bgra.data() + (std::size_t) row * region.width * 4,
            (std::size_t) region.width * 4
          );
        }
      }
    }

    if (img) {
      img->frame_timestamp = frame_timestamp;
    }

    return capture_e::ok;
  }

  capture_e display_wgc_ram_t::release_snapshot() {
    return dup.release_frame();
  }
}  // namespace platf::dxgi
