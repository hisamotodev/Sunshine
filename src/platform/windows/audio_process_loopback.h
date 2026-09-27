/**
 * @file src/platform/windows/audio_process_loopback.h
 * @brief Process-scoped WASAPI loopback audio capture, per agent.md section 9.
 *
 * EXPERIMENTAL -- code-complete but unverified end-to-end. On the machine
 * this was written on, `ActivateAudioInterfaceAsync()` fails synchronously
 * with `E_ILLEGAL_METHOD_CALL` (suspected third-party audio middleware
 * interference, root cause unconfirmed); see
 * docs/research/poc2-process-loopback-audio.md. This module ports that PoC's
 * validated struct layout/API sequence into Titan's `mic_t` capture
 * interface, gated behind the opt-in `audio-process` apps.json key (see
 * process.h's `ctx_t::audio_process`) so it can never affect an app that
 * doesn't explicitly request it, and falls back to ordinary system-wide
 * loopback (`mic_wasapi_t`) on any failure.
 */
#pragma once

// standard includes
#include <cstdint>
#include <memory>
#include <string>

// platform includes
#include <Windows.h>

// local includes
#include "src/platform/common.h"

namespace platf::audio::process_loopback {
  /**
   * @brief Configure the process-loopback target for the currently-launched
   * app. Called by `proc_t::execute()` when `ctx_t::audio_process` is set.
   *
   * @param job_handle Native handle of the app's process group Job Object
   *        (`boost::process::v1::group::native_handle()`), used to resolve
   *        `process_name` to a PID via `platf::process_group_pids()`.
   * @param process_name Executable name (not full path), e.g. `L"AppA.exe"`.
   */
  void set_target(std::uintptr_t job_handle, std::wstring process_name);

  /**
   * @brief Discard the current target. Called by `proc_t::terminate()`.
   */
  void clear();

  /**
   * @brief Attempt to construct a process-scoped loopback capture source for
   * the target configured via `set_target()`.
   *
   * @param frame_size Number of samples captured per audio frame.
   * @param channels Number of output channels requested by the audio
   *        pipeline. Process-loopback only supports stereo (Microsoft's
   *        `ApplicationLoopback` sample documents this as a fixed format);
   *        any other value fails immediately.
   * @param continuous_audio Whether silent audio should continue to be
   *        emitted during gaps, matching `mic_t`'s usual contract.
   * @return A ready-to-sample `mic_t`, or `nullptr` if no target is
   *         configured, the target process can't be found, the requested
   *         channel count isn't 2, or activation fails for any reason --
   *         callers should fall back to `mic_wasapi_t` in all of those cases.
   */
  std::unique_ptr<mic_t> try_create(std::uint32_t frame_size, std::uint32_t channels, bool continuous_audio);
}  // namespace platf::audio::process_loopback
