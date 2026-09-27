/**
 * @file src/ui/ui_thread.cpp
 * @brief Definitions for the embedded SDL3 + Dear ImGui management UI.
 */
#ifdef _WIN32

// standard includes
#include <atomic>
#include <thread>

// lib includes
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

// local includes
#include "app_state.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "ui_apps.h"
#include "ui_settings.h"
#include "ui_thread.h"

using namespace std::literals;

namespace ui {

  namespace {
    enum class visibility_request_e {
      none,
      show,
      hide
    };

    std::atomic<visibility_request_e> requested_visibility {visibility_request_e::none};  ///< Pending show()/hide() request from another thread.
    std::atomic<bool> window_visible {false};  ///< Whether the window is currently shown.

    std::jthread &worker_thread() {
      static std::jthread thread;
      return thread;
    }

    /**
     * @brief Render one frame of the embedded UI's Settings/Apps tabs.
     *
     * First-run, pairing, clients, logs, and the rest of the WebUI's functionality
     * land in later phases.
     */
    void render_frame(AppState &state) {
      const auto *viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(viewport->WorkPos);
      ImGui::SetNextWindowSize(viewport->WorkSize);

      ImGui::Begin(
        "Sunshine",
        nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar
      );

      if (!state.status_message.empty()) {
        if (state.status_is_error) {
          ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.4F, 1.0F), "%s", state.status_message.c_str());
        } else {
          ImGui::TextColored(ImVec4(0.5F, 0.9F, 0.5F, 1.0F), "%s", state.status_message.c_str());
        }
        ImGui::Separator();
      }

      if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Settings")) {
          render_settings_tab(state);
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Apps")) {
          render_apps_tab(state);
          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }

      ImGui::End();
    }

    /**
     * @brief Owns the SDL3/ImGui window, context, and event loop for the lifetime of the thread.
     *
     * @param stop_token Stop token owned by the UI worker thread.
     */
    void run(const std::stop_token stop_token) {
      platf::set_thread_name("ui");

      if (!SDL_Init(SDL_INIT_VIDEO)) {
        BOOST_LOG(error) << "Embedded UI: SDL_Init failed: "sv << SDL_GetError();
        return;
      }

      auto main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
      if (main_scale <= 0.0F) {
        main_scale = 1.0F;
      }

      constexpr auto base_width = 900;
      constexpr auto base_height = 640;
      const auto window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
      auto *window = SDL_CreateWindow(
        "Sunshine",
        static_cast<int>(static_cast<float>(base_width) * main_scale),
        static_cast<int>(static_cast<float>(base_height) * main_scale),
        window_flags
      );
      if (!window) {
        BOOST_LOG(error) << "Embedded UI: SDL_CreateWindow failed: "sv << SDL_GetError();
        SDL_Quit();
        return;
      }

      auto *renderer = SDL_CreateRenderer(window, nullptr);
      if (!renderer) {
        BOOST_LOG(error) << "Embedded UI: SDL_CreateRenderer failed: "sv << SDL_GetError();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
      }
      SDL_SetRenderVSync(renderer, 1);
      SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      auto &io = ImGui::GetIO();
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
      io.IniFilename = nullptr;

      ImGui::StyleColorsDark();
      auto &style = ImGui::GetStyle();
      style.ScaleAllSizes(main_scale);
      style.FontScaleDpi = main_scale;

      ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
      ImGui_ImplSDLRenderer3_Init(renderer);

      AppState state;

      BOOST_LOG(info) << "Embedded UI thread started"sv;

      while (!stop_token.stop_requested()) {
        switch (requested_visibility.exchange(visibility_request_e::none)) {
          case visibility_request_e::show:
            SDL_ShowWindow(window);
            SDL_RaiseWindow(window);
            window_visible.store(true);
            break;
          case visibility_request_e::hide:
            SDL_HideWindow(window);
            window_visible.store(false);
            break;
          case visibility_request_e::none:
            break;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
          ImGui_ImplSDL3_ProcessEvent(&event);
          if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
            // Closing the window hides it; Sunshine keeps running in the tray.
            SDL_HideWindow(window);
            window_visible.store(false);
          }
        }

        if (!window_visible.load()) {
          // Wait for the next event (or a show() request, picked up on the next
          // iteration) instead of busy-spinning while the window is hidden.
          SDL_WaitEventTimeout(nullptr, 50);
          continue;
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        render_frame(state);

        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColorFloat(renderer, 0.08F, 0.08F, 0.09F, 1.0F);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
      }

      ImGui_ImplSDLRenderer3_Shutdown();
      ImGui_ImplSDL3_Shutdown();
      ImGui::DestroyContext();
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      SDL_Quit();

      BOOST_LOG(info) << "Embedded UI thread ended"sv;
    }
  }  // namespace

  void start() {
    auto &thread = worker_thread();
    if (thread.joinable()) {
      BOOST_LOG(warning) << "Embedded UI thread is already running"sv;
      return;
    }
    thread = std::jthread(run);
  }

  void stop() {
    auto &thread = worker_thread();
    if (!thread.joinable()) {
      return;
    }
    thread.request_stop();
    thread.join();
  }

  void show() {
    requested_visibility.store(visibility_request_e::show);
  }

  void hide() {
    requested_visibility.store(visibility_request_e::hide);
  }

  bool is_visible() {
    return window_visible.load();
  }

}  // namespace ui

#endif
