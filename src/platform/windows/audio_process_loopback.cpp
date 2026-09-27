/**
 * @file src/platform/windows/audio_process_loopback.cpp
 * @brief See audio_process_loopback.h.
 */
#include "audio_process_loopback.h"

// standard includes
#include <atomic>
#include <mutex>

// platform includes
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <winrt/base.h>

// local includes
#include "misc.h"
#include "src/logging.h"
#include "src/utility.h"
#include "utf_utils.h"

// Hand-written AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK definitions --
// see poc/process-loopback-audio/win_process_loopback.h (this mingw-w64
// UCRT64 toolchain doesn't ship audioclientactivationparams.h, confirmed in
// docs/research/poc2-process-loopback-audio.md). Struct layout matches
// Microsoft's published ApplicationLoopback sample exactly.
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"

typedef enum _AUDIOCLIENT_ACTIVATION_TYPE {
  AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
  AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1,
} AUDIOCLIENT_ACTIVATION_TYPE;

typedef enum _PROCESS_LOOPBACK_MODE {
  PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
  PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1,
} PROCESS_LOOPBACK_MODE;

typedef struct _AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
  DWORD TargetProcessId;
  PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
} AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS;

typedef struct _AUDIOCLIENT_ACTIVATION_PARAMS {
  AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
  union {
    AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
  } DUMMYUNIONNAME;
} AUDIOCLIENT_ACTIVATION_PARAMS;

using namespace std::literals;

namespace platf::audio::process_loopback {
  namespace {
    struct target_t {
      std::uintptr_t job_handle = 0;
      std::wstring process_name;
    };

    std::mutex g_mutex;
    std::optional<target_t> g_target;

    /**
     * @brief Minimal `IActivateAudioInterfaceCompletionHandler`, matching
     * poc/process-loopback-audio/process_loopback_poc.cpp's validated shape.
     */
    class completion_handler_t: public IActivateAudioInterfaceCompletionHandler {
    public:
      completion_handler_t():
          ref_count_(1),
          event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
      }

      virtual ~completion_handler_t() = default;

      HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
          *ppv = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
          AddRef();
          return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
      }

      ULONG STDMETHODCALLTYPE AddRef() override {
        return ++ref_count_;
      }

      ULONG STDMETHODCALLTYPE Release() override {
        auto n = --ref_count_;
        if (n == 0) {
          delete this;
        }
        return n;
      }

      HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *op) override {
        op_ = op;
        SetEvent(event_.get());
        return S_OK;
      }

      HANDLE event() {
        return event_.get();
      }

      IActivateAudioInterfaceAsyncOperation *op() const {
        return op_;
      }

    private:
      std::atomic<ULONG> ref_count_;
      util::safe_ptr_v2<void, BOOL, CloseHandle> event_;
      IActivateAudioInterfaceAsyncOperation *op_ = nullptr;
    };

    /**
     * @brief Resolve `process_name` to a PID currently in the given Job
     * Object (agent.md section 9.2: the target may be a launcher's child by
     * the time capture starts, so matching by name across the whole process
     * tree -- not just the originally-launched PID -- is required).
     */
    std::optional<DWORD> resolve_pid(std::uintptr_t job_handle, const std::wstring &process_name) {
      auto pids = platf::process_group_pids(job_handle);
      if (!pids) {
        return std::nullopt;
      }

      for (auto pid : *pids) {
        util::safe_ptr_v2<void, BOOL, CloseHandle> process_handle {
          OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)
        };
        if (!process_handle) {
          continue;
        }

        wchar_t image_name[MAX_PATH] {};
        DWORD image_name_len = ARRAYSIZE(image_name);
        if (!QueryFullProcessImageNameW(process_handle.get(), 0, image_name, &image_name_len)) {
          continue;
        }

        std::wstring full_path {image_name, image_name_len};
        auto sep = full_path.find_last_of(L"\\/");
        std::wstring base_name = (sep == std::wstring::npos) ? full_path : full_path.substr(sep + 1);

        if (_wcsicmp(base_name.c_str(), process_name.c_str()) == 0) {
          return pid;
        }
      }

      return std::nullopt;
    }

    /**
     * @brief Process-scoped loopback `mic_t`. Mirrors `mic_wasapi_t`'s
     * sample-buffer refill shape (audio.cpp) so it plugs into the same
     * downstream encoder pipeline; only capture-source setup differs.
     */
    class process_loopback_mic_t: public mic_t {
    public:
      capture_e sample(std::vector<float> &sample_out) override {
        auto sample_size = sample_out.size();

        while (sample_buf_pos - std::begin(sample_buf) < (std::ptrdiff_t) sample_size) {
          auto capture_result = fill_buffer();
          if (capture_result == capture_e::timeout && continuous_audio) {
            std::fill_n(sample_buf_pos, sample_size, 0.0f);
            sample_buf_pos += sample_size;
          } else if (capture_result != capture_e::ok) {
            return capture_result;
          }
        }

        std::copy_n(std::begin(sample_buf), sample_size, std::begin(sample_out));
        std::move(&sample_buf[sample_size], sample_buf_pos, std::begin(sample_buf));
        sample_buf_pos -= sample_size;

        return capture_e::ok;
      }

      int init(DWORD target_pid, std::uint32_t frame_size, bool continuous) {
        continuous_audio = continuous;

        audio_event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!audio_event) {
          BOOST_LOG(error) << "process_loopback: couldn't create event handle"sv;
          return -1;
        }

        AUDIOCLIENT_ACTIVATION_PARAMS params {};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = target_pid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

        PROPVARIANT prop {};
        PropVariantInit(&prop);
        prop.vt = VT_BLOB;
        prop.blob.cbSize = sizeof(params);
        prop.blob.pBlobData = reinterpret_cast<BYTE *>(&params);

        winrt::com_ptr<completion_handler_t> handler;
        handler.attach(new completion_handler_t());
        IActivateAudioInterfaceAsyncOperation *async_op_raw = nullptr;
        HRESULT status = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &prop, handler.get(), &async_op_raw);
        winrt::com_ptr<IActivateAudioInterfaceAsyncOperation> async_op;
        async_op.attach(async_op_raw);
        if (FAILED(status)) {
          BOOST_LOG(error) << "process_loopback: ActivateAudioInterfaceAsync failed [0x"sv << util::hex(status).to_string_view() << ']';
          return -1;
        }

        // Bounded wait, not INFINITE -- see docs/research/poc2-process-loopback-audio.md:
        // under elevation, the completion callback has been observed to never fire.
        if (WaitForSingleObject(handler->event(), 5000) != WAIT_OBJECT_0) {
          BOOST_LOG(error) << "process_loopback: activation did not complete within 5s"sv;
          return -1;
        }

        HRESULT activate_status = S_OK;
        IUnknown *iface_raw = nullptr;
        status = handler->op()->GetActivateResult(&activate_status, &iface_raw);
        winrt::com_ptr<IUnknown> iface;
        iface.attach(iface_raw);
        if (FAILED(status) || FAILED(activate_status)) {
          BOOST_LOG(error) << "process_loopback: activation failed [0x"sv << util::hex(FAILED(status) ? status : activate_status).to_string_view() << ']';
          return -1;
        }

        if (!iface || FAILED(status = iface->QueryInterface(__uuidof(IAudioClient), (void **) &audio_client))) {
          BOOST_LOG(error) << "process_loopback: QueryInterface(IAudioClient) failed [0x"sv << util::hex(status).to_string_view() << ']';
          return -1;
        }

        // Process loopback only supports this exact format, per Microsoft's
        // ApplicationLoopback sample -- not negotiable like mic_wasapi_t's format list.
        WAVEFORMATEX format {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 2;
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 32;
        format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        channels = format.nChannels;

        status = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 10000000, 0, &format, nullptr);
        if (FAILED(status)) {
          BOOST_LOG(error) << "process_loopback: Initialize failed [0x"sv << util::hex(status).to_string_view() << ']';
          return -1;
        }

        status = audio_client->SetEventHandle(audio_event.get());
        if (FAILED(status)) {
          BOOST_LOG(error) << "process_loopback: SetEventHandle failed [0x"sv << util::hex(status).to_string_view() << ']';
          return -1;
        }

        status = audio_client->GetService(IID_IAudioCaptureClient, (void **) &audio_capture);
        if (FAILED(status)) {
          BOOST_LOG(error) << "process_loopback: GetService(IAudioCaptureClient) failed [0x"sv << util::hex(status).to_string_view() << ']';
          return -1;
        }

        std::uint32_t frames = 0;
        audio_client->GetBufferSize(&frames);
        sample_buf = util::buffer_t<float> {std::max(frames, frame_size) * 2 * channels};
        sample_buf_pos = std::begin(sample_buf);

        status = audio_client->Start();
        if (FAILED(status)) {
          BOOST_LOG(error) << "process_loopback: Start failed [0x"sv << util::hex(status).to_string_view() << ']';
          return -1;
        }

        BOOST_LOG(info) << "process_loopback: capturing process-scoped loopback audio for pid "sv << target_pid << " (EXPERIMENTAL, see docs/research/poc2-process-loopback-audio.md)"sv;
        return 0;
      }

      ~process_loopback_mic_t() override {
        if (audio_client) {
          audio_client->Stop();
        }
      }

    private:
      capture_e fill_buffer() {
        HRESULT status = WaitForSingleObjectEx(audio_event.get(), 200, FALSE);
        switch (status) {
          case WAIT_OBJECT_0:
            break;
          case WAIT_TIMEOUT:
            return capture_e::timeout;
          default:
            BOOST_LOG(error) << "process_loopback: couldn't wait for audio event: [0x"sv << util::hex(status).to_string_view() << ']';
            return capture_e::error;
        }

        std::uint32_t packet_size = 0;
        for (
          status = audio_capture->GetNextPacketSize(&packet_size);
          SUCCEEDED(status) && packet_size > 0;
          status = audio_capture->GetNextPacketSize(&packet_size)
        ) {
          BYTE *data = nullptr;
          std::uint32_t num_frames = 0;
          DWORD buffer_flags = 0;
          status = audio_capture->GetBuffer(&data, &num_frames, &buffer_flags, nullptr, nullptr);

          switch (status) {
            case S_OK:
              break;
            case AUDCLNT_E_DEVICE_INVALIDATED:
              return capture_e::reinit;
            default:
              BOOST_LOG(error) << "process_loopback: couldn't capture audio [0x"sv << util::hex(status).to_string_view() << ']';
              return capture_e::error;
          }

          auto uninitialized = std::end(sample_buf) - sample_buf_pos;
          auto n = std::min<std::uint32_t>(uninitialized, num_frames * channels);

          if (buffer_flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            std::fill_n(sample_buf_pos, n, 0.0f);
          } else if (data) {
            std::copy_n(reinterpret_cast<float *>(data), n, sample_buf_pos);
          }
          sample_buf_pos += n;

          audio_capture->ReleaseBuffer(num_frames);
        }

        if (status == AUDCLNT_E_DEVICE_INVALIDATED) {
          return capture_e::reinit;
        }
        if (FAILED(status)) {
          return capture_e::error;
        }
        return capture_e::ok;
      }

      util::safe_ptr_v2<void, BOOL, CloseHandle> audio_event;
      winrt::com_ptr<IAudioClient> audio_client;
      winrt::com_ptr<IAudioCaptureClient> audio_capture;
      util::buffer_t<float> sample_buf;
      float *sample_buf_pos = nullptr;
      int channels = 2;
      bool continuous_audio = false;
    };
  }  // namespace

  void set_target(std::uintptr_t job_handle, std::wstring process_name) {
    std::lock_guard lock(g_mutex);
    g_target = target_t {job_handle, std::move(process_name)};
  }

  void clear() {
    std::lock_guard lock(g_mutex);
    g_target.reset();
  }

  std::unique_ptr<mic_t> try_create(std::uint32_t frame_size, std::uint32_t channels, bool continuous_audio) {
    if (channels != 2) {
      BOOST_LOG(warning) << "process_loopback: requested channel count ["sv << channels << "] != 2, process-loopback only supports stereo -- falling back to system loopback"sv;
      return nullptr;
    }

    target_t target;
    {
      std::lock_guard lock(g_mutex);
      if (!g_target) {
        return nullptr;
      }
      target = *g_target;
    }

    auto pid = resolve_pid(target.job_handle, target.process_name);
    if (!pid) {
      BOOST_LOG(warning) << "process_loopback: couldn't find a running process named ["sv << utf_utils::to_utf8(target.process_name) << "] -- falling back to system loopback"sv;
      return nullptr;
    }

    auto mic = std::make_unique<process_loopback_mic_t>();
    if (mic->init(*pid, frame_size, continuous_audio)) {
      BOOST_LOG(warning) << "process_loopback: activation failed -- falling back to system loopback"sv;
      return nullptr;
    }

    return mic;
  }
}  // namespace platf::audio::process_loopback
