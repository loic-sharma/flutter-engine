// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/flutter_windows_engine.h"

#include <dwmapi.h>

#include <filesystem>
#include <sstream>

#include "flutter/fml/logging.h"
#include "flutter/fml/paths.h"
#include "flutter/fml/platform/win/wstring_conversion.h"
#include "flutter/shell/platform/common/client_wrapper/binary_messenger_impl.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_message_codec.h"
#include "flutter/shell/platform/common/path_utils.h"
#include "flutter/shell/platform/embedder/embedder_struct_macros.h"
#include "flutter/shell/platform/windows/accessibility_bridge_windows.h"
#include "flutter/shell/platform/windows/compositor_opengl.h"
#include "flutter/shell/platform/windows/compositor_software.h"
#include "flutter/shell/platform/windows/flutter_windows_view.h"
#include "flutter/shell/platform/windows/keyboard_key_channel_handler.h"
#include "flutter/shell/platform/windows/system_utils.h"
#include "flutter/shell/platform/windows/task_runner.h"
#include "flutter/third_party/accessibility/ax/ax_node.h"

// winbase.h defines GetCurrentTime as a macro.
#undef GetCurrentTime

static constexpr char kAccessibilityChannelName[] = "flutter/accessibility";

namespace flutter {

namespace {

// Lifted from vsync_waiter_fallback.cc
static std::chrono::nanoseconds SnapToNextTick(
    std::chrono::nanoseconds value,
    std::chrono::nanoseconds tick_phase,
    std::chrono::nanoseconds tick_interval) {
  std::chrono::nanoseconds offset = (tick_phase - value) % tick_interval;
  if (offset != std::chrono::nanoseconds::zero())
    offset = offset + tick_interval;
  return value + offset;
}

// Converts a FlutterPlatformMessage to an equivalent FlutterDesktopMessage.
static FlutterDesktopMessage ConvertToDesktopMessage(
    const FlutterPlatformMessage& engine_message) {
  FlutterDesktopMessage message = {};
  message.struct_size = sizeof(message);
  message.channel = engine_message.channel;
  message.message = engine_message.message;
  message.message_size = engine_message.message_size;
  message.response_handle = engine_message.response_handle;
  return message;
}

}  // namespace

FlutterWindowsEngine::FlutterWindowsEngine(
    const FlutterProjectBundle& project,
    std::shared_ptr<WindowsProcTable> windows_proc_table)
    : project_(std::make_unique<FlutterProjectBundle>(project)),
      embedder_api_(std::make_unique<EmbedderApi>()),
      windows_proc_table_(std::move(windows_proc_table)),
      lifecycle_manager_(std::make_unique<WindowsLifecycleManager>(this)) {
  if (windows_proc_table_ == nullptr) {
    windows_proc_table_ = std::make_shared<WindowsProcTable>();
  }

  gl_ = egl::ProcTable::Create();

  task_runner_ =
      std::make_unique<TaskRunner>(
          [this]() { return embedder_api_->CurrentTime(); },
          [this](const auto* task) {
            if (!engine_) {
              FML_LOG(ERROR)
                  << "Cannot post an engine task when engine is not running.";
              return;
            }
            if (embedder_api_.RunTask(engine_, task) != kSuccess) {
              FML_LOG(ERROR) << "Failed to post an engine task.";
            }
          });

  // Set up the legacy structs backing the API handles.
  messenger_ =
      fml::RefPtr<FlutterDesktopMessenger>(new FlutterDesktopMessenger());
  messenger_->SetEngine(this);
  plugin_registrar_ = std::make_unique<FlutterDesktopPluginRegistrar>();
  plugin_registrar_->engine = this;

  messenger_wrapper_ =
      std::make_unique<BinaryMessengerImpl>(messenger_->ToRef());
  message_dispatcher_ =
      std::make_unique<IncomingMessageDispatcher>(messenger_->ToRef());
  message_dispatcher_->SetMessageCallback(
      kAccessibilityChannelName,
      [](FlutterDesktopMessengerRef messenger,
         const FlutterDesktopMessage* message, void* data) {
        FlutterWindowsEngine* engine = static_cast<FlutterWindowsEngine*>(data);
        engine->HandleAccessibilityMessage(messenger, message);
      },
      static_cast<void*>(this));

  texture_registrar_ =
      std::make_unique<FlutterWindowsTextureRegistrar>(this, gl_);

  // Check for impeller support.
  auto& switches = project_->GetSwitches();
  enable_impeller_ = std::find(switches.begin(), switches.end(),
                               "--enable-impeller=true") != switches.end();

  egl_manager_ = egl::Manager::Create(enable_impeller_);
  window_proc_delegate_manager_ = std::make_unique<WindowProcDelegateManager>();
  window_proc_delegate_manager_->RegisterTopLevelWindowProcDelegate(
      [](HWND hwnd, UINT msg, WPARAM wpar, LPARAM lpar, void* user_data,
         LRESULT* result) {
        BASE_DCHECK(user_data);
        FlutterWindowsEngine* that =
            static_cast<FlutterWindowsEngine*>(user_data);
        BASE_DCHECK(that->lifecycle_manager_);
        return that->lifecycle_manager_->WindowProc(hwnd, msg, wpar, lpar,
                                                    result);
      },
      static_cast<void*>(this));

  // Set up internal channels.
  // TODO: Replace this with an embedder.h API. See
  // https://github.com/flutter/flutter/issues/71099
  internal_plugin_registrar_ =
      std::make_unique<PluginRegistrar>(plugin_registrar_.get());
  cursor_handler_ =
      std::make_unique<CursorHandler>(messenger_wrapper_.get(), this);
  platform_handler_ =
      std::make_unique<PlatformHandler>(messenger_wrapper_.get(), this);
  settings_plugin_ = std::make_unique<SettingsPlugin>(messenger_wrapper_.get(),
                                                      task_runner_.get());
}

FlutterWindowsEngine::~FlutterWindowsEngine() {
  messenger_->SetEngine(nullptr);
  Stop();
}

void FlutterWindowsEngine::SetSwitches(
    const std::vector<std::string>& switches) {
  project_->SetSwitches(switches);
}

bool FlutterWindowsEngine::Run() {
  return Run("");
}

bool FlutterWindowsEngine::Run(std::string_view entrypoint) {
  // The platform thread creates OpenGL contexts. These
  // must be released to be used by the engine's threads.
  FML_DCHECK(!egl_manager_ || !egl_manager_->HasContextCurrent());

  if (egl_manager_) {
    auto resolver = [](const char* name) -> void* {
      return reinterpret_cast<void*>(::eglGetProcAddress(name));
    };

    compositor_ = std::make_unique<CompositorOpenGL>(this, resolver);
  } else {
    if (enable_impeller_) {
      // Impeller does not support a Software backend. Avoid falling back and
      // confusing the engine on which renderer is selected.
      FML_LOG(ERROR) << "Could not create surface manager. Impeller backend "
                        "does not support software rendering.";
      return false;
    }

    compositor_ = std::make_unique<CompositorSoftware>(this);
  }

  auto embedder_callbacks = std::make_unique<EmbedderApiCallbacks>();

  embedder_callbacks->platform_message_callback =
      [this](const FlutterPlatformMessage* message) {
        HandlePlatformMessage(message);
      };

  embedder_callbacks->vsync_callback = [this](OnVSyncCallback on_vsync) {
    OnVsync(std::move(on_vsync));
  };

  embedder_callbacks->root_isolate_create_callback = [this]() {
    if (root_isolate_create_callback_) {
      root_isolate_create_callback_();
    }
  };

  embedder_callbacks->on_pre_engine_restart_callback = [this]() {
    OnPreEngineRestart();
  };

  embedder_callbacks->semantics_update_callback =
      [this](const FlutterSemanticsUpdate2* update) {
        auto view = view_;
        if (!view) {
          return;
        }

        auto accessibility_bridge = view->accessibility_bridge().lock();
        if (!accessibility_bridge) {
          return;
        }

        for (size_t i = 0; i < update->node_count; i++) {
          const FlutterSemanticsNode2* node = update->nodes[i];
          accessibility_bridge->AddFlutterSemanticsNodeUpdate(*node);
        }

        for (size_t i = 0; i < update->custom_action_count; i++) {
          const FlutterSemanticsCustomAction2* action =
              update->custom_actions[i];
          accessibility_bridge->AddFlutterSemanticsCustomActionUpdate(*action);
        }

        accessibility_bridge->CommitUpdates();
      };

  embedder_callbacks->channel_update_callback = [this](std::string_view channel,
                                                       bool listening) {
    OnChannelUpdate(std::string{channel}, listening);
  };

  bool success = embedder_api_->Run(
      project_.get(), GetExecutableName(), entrypoint, task_runner_.get(),
      &WindowsPlatformThreadPrioritySetter, compositor_.get(),
      std::move(embedder_callbacks));
  if (success == false) {
    FML_LOG(ERROR) << "Engine launch failed.";
    return false;
  }

  // Configure device frame rate displayed via devtools.
  FlutterEngineDisplay display = {};
  display.struct_size = sizeof(FlutterEngineDisplay);
  display.display_id = 0;
  display.single_display = true;
  display.refresh_rate =
      1.0 / (static_cast<double>(FrameInterval().count()) / 1000000000.0);

  std::vector<FlutterEngineDisplay> displays = {display};

  embedder_api_->NotifyDisplayUpdate(kFlutterEngineDisplaysUpdateTypeStartup,
                                     std::move(displays));

  SendSystemLocales();
  SetLifecycleState(flutter::AppLifecycleState::kResumed);

  settings_plugin_->StartWatching();
  settings_plugin_->SendSettings();

  return true;
}

bool FlutterWindowsEngine::Stop() {
  if (!embedder_api_) {
    return false;
  }

  for (const auto& [callback, registrar] :
       plugin_registrar_destruction_callbacks_) {
    callback(registrar);
  }

  bool success = embedder_api_->Shutdown();
  embedder_api_ = nullptr;
  return success;
}

void FlutterWindowsEngine::SetView(FlutterWindowsView* view) {
  view_ = view;
  InitializeKeyboard();
}

void FlutterWindowsEngine::OnVsync(OnVSyncCallback on_vsync) {
  std::chrono::nanoseconds current_time =
      std::chrono::nanoseconds(embedder_api_->CurrentTime());
  std::chrono::nanoseconds frame_interval = FrameInterval();
  auto next = SnapToNextTick(current_time, start_time_, frame_interval);
  on_vsync(next.count(), (next + frame_interval).count());
}

std::chrono::nanoseconds FlutterWindowsEngine::FrameInterval() {
  if (frame_interval_override_.has_value()) {
    return frame_interval_override_.value();
  }
  uint64_t interval = 16600000;

  DWM_TIMING_INFO timing_info = {};
  timing_info.cbSize = sizeof(timing_info);
  HRESULT result = DwmGetCompositionTimingInfo(NULL, &timing_info);
  if (result == S_OK && timing_info.rateRefresh.uiDenominator > 0 &&
      timing_info.rateRefresh.uiNumerator > 0) {
    interval = static_cast<double>(timing_info.rateRefresh.uiDenominator *
                                   1000000000.0) /
               static_cast<double>(timing_info.rateRefresh.uiNumerator);
  }

  return std::chrono::nanoseconds(interval);
}

// Returns the currently configured Plugin Registrar.
FlutterDesktopPluginRegistrarRef FlutterWindowsEngine::GetRegistrar() {
  return plugin_registrar_.get();
}

void FlutterWindowsEngine::AddPluginRegistrarDestructionCallback(
    FlutterDesktopOnPluginRegistrarDestroyed callback,
    FlutterDesktopPluginRegistrarRef registrar) {
  plugin_registrar_destruction_callbacks_[callback] = registrar;
}

void FlutterWindowsEngine::SendWindowMetricsEvent(
    const FlutterWindowMetricsEvent& event) {
  if (embedder_api_->Running()) {
    embedder_api_->SendWindowMetricsEvent(&event);
  }
}

void FlutterWindowsEngine::SendPointerEvent(const FlutterPointerEvent& event) {
  if (embedder_api_->Running()) {
    embedder_api_->SendPointerEvent(&event);
  }
}

void FlutterWindowsEngine::SendKeyEvent(const FlutterKeyEvent& event,
                                        FlutterKeyEventCallback callback,
                                        void* user_data) {
  if (embedder_api_->Running()) {
    // TODO
    embedder_api_->SendKeyEvent(&event, [callback, user_data](bool handled) {
      callback(handled, user_data);
    });
  }
}

bool FlutterWindowsEngine::SendPlatformMessage(
    const char* channel,
    const uint8_t* message,
    const size_t message_size,
    const FlutterDesktopBinaryReply reply,
    void* user_data) {
  FlutterPlatformMessageResponseHandle* response_handle = nullptr;
  if (reply != nullptr && user_data != nullptr) {
    FlutterEngineResult result =
        embedder_api_.PlatformMessageCreateResponseHandle(
            engine_, reply, user_data, &response_handle);
    if (result != kSuccess) {
      FML_LOG(ERROR) << "Failed to create response handle";
      return false;
    }
  }

  FlutterPlatformMessage platform_message = {
      sizeof(FlutterPlatformMessage),
      channel,
      message,
      message_size,
      response_handle,
  };

  FlutterEngineResult message_result =
      embedder_api_.SendPlatformMessage(engine_, &platform_message);
  if (response_handle != nullptr) {
    embedder_api_.PlatformMessageReleaseResponseHandle(engine_,
                                                       response_handle);
  }
  return message_result == kSuccess;
}

void FlutterWindowsEngine::SendPlatformMessageResponse(
    const FlutterDesktopMessageResponseHandle* handle,
    const uint8_t* data,
    size_t data_length) {
  embedder_api_.SendPlatformMessageResponse(engine_, handle, data, data_length);
}

void FlutterWindowsEngine::HandlePlatformMessage(
    const FlutterPlatformMessage* engine_message) {
  if (engine_message->struct_size != sizeof(FlutterPlatformMessage)) {
    FML_LOG(ERROR) << "Invalid message size received. Expected: "
                   << sizeof(FlutterPlatformMessage) << " but received "
                   << engine_message->struct_size;
    return;
  }

  auto message = ConvertToDesktopMessage(*engine_message);

  message_dispatcher_->HandleMessage(
      message, [this] {}, [this] {});
}

void FlutterWindowsEngine::ReloadSystemFonts() {
  embedder_api_->ReloadSystemFonts();
}

void FlutterWindowsEngine::ScheduleFrame() {
  embedder_api_->ScheduleFrame();
}

void FlutterWindowsEngine::SetNextFrameCallback(fml::closure callback) {
  // TODO: Remove field
  // next_frame_callback_ = std::move(callback);

  embedder_api_->SetNextFrameCallback([this, callback = std::move(callback)]() {
    // Embedder callback runs on raster thread. Switch back to platform
    // thread.

    task_runner_->PostTask(std::move(callback));
  });
}

void FlutterWindowsEngine::SetLifecycleState(flutter::AppLifecycleState state) {
  if (lifecycle_manager_) {
    lifecycle_manager_->SetLifecycleState(state);
  }
}

void FlutterWindowsEngine::SendSystemLocales() {
  std::vector<LanguageInfo> languages =
      GetPreferredLanguageInfo(*windows_proc_table_);

  embedder_api_->UpdateLocales(std::move(languages));
}

void FlutterWindowsEngine::InitializeKeyboard() {
  if (view_ == nullptr) {
    FML_LOG(ERROR) << "Cannot initialize keyboard on Windows headless mode.";
  }

  auto internal_plugin_messenger = internal_plugin_registrar_->messenger();
  KeyboardKeyEmbedderHandler::GetKeyStateHandler get_key_state = GetKeyState;
  KeyboardKeyEmbedderHandler::MapVirtualKeyToScanCode map_vk_to_scan =
      [](UINT virtual_key, bool extended) {
        return MapVirtualKey(virtual_key,
                             extended ? MAPVK_VK_TO_VSC_EX : MAPVK_VK_TO_VSC);
      };
  keyboard_key_handler_ = std::move(CreateKeyboardKeyHandler(
      internal_plugin_messenger, get_key_state, map_vk_to_scan));
  text_input_plugin_ =
      std::move(CreateTextInputPlugin(internal_plugin_messenger));
}

std::unique_ptr<KeyboardHandlerBase>
FlutterWindowsEngine::CreateKeyboardKeyHandler(
    BinaryMessenger* messenger,
    KeyboardKeyEmbedderHandler::GetKeyStateHandler get_key_state,
    KeyboardKeyEmbedderHandler::MapVirtualKeyToScanCode map_vk_to_scan) {
  auto keyboard_key_handler = std::make_unique<KeyboardKeyHandler>(messenger);
  keyboard_key_handler->AddDelegate(
      std::make_unique<KeyboardKeyEmbedderHandler>(
          [this](const FlutterKeyEvent& event, FlutterKeyEventCallback callback,
                 void* user_data) {
            return SendKeyEvent(event, callback, user_data);
          },
          get_key_state, map_vk_to_scan));
  keyboard_key_handler->AddDelegate(
      std::make_unique<KeyboardKeyChannelHandler>(messenger));
  keyboard_key_handler->InitKeyboardChannel();
  return keyboard_key_handler;
}

std::unique_ptr<TextInputPlugin> FlutterWindowsEngine::CreateTextInputPlugin(
    BinaryMessenger* messenger) {
  return std::make_unique<TextInputPlugin>(messenger, this);
}

bool FlutterWindowsEngine::RegisterExternalTexture(int64_t texture_id) {
  return embedder_api_->RegisterExternalTexture(texture_id);
}

bool FlutterWindowsEngine::UnregisterExternalTexture(int64_t texture_id) {
  return embedder_api_->UnregisterExternalTexture(texture_id);
}

bool FlutterWindowsEngine::MarkExternalTextureFrameAvailable(
    int64_t texture_id) {
  return embedder_api_->MarkExternalTextureFrameAvailable(texture_id);
}

bool FlutterWindowsEngine::PostRasterThreadTask(fml::closure callback) const {
  embedder_api_->PostRasterThreadTask(std::move(callback));
}

bool FlutterWindowsEngine::DispatchSemanticsAction(
    uint64_t target,
    FlutterSemanticsAction action,
    fml::MallocMapping data) {
  return embedder_api_->DispatchSemanticsAction(target, action,
                                                std::move(data));
}

void FlutterWindowsEngine::UpdateSemanticsEnabled(bool enabled) {
  if (embedder_api_->Running() && semantics_enabled_ != enabled) {
    semantics_enabled_ = enabled;
    embedder_api_->SetSemanticsEnabled(enabled);
    view_->UpdateSemanticsEnabled(enabled);
  }
}

void FlutterWindowsEngine::OnPreEngineRestart() {
  // Reset the keyboard's state on hot restart.
  if (view_) {
    InitializeKeyboard();
  }
}

std::string FlutterWindowsEngine::GetExecutableName() const {
  std::pair<bool, std::string> result = fml::paths::GetExecutablePath();
  if (result.first) {
    const std::string& executable_path = result.second;
    size_t last_separator = executable_path.find_last_of("/\\");
    if (last_separator == std::string::npos ||
        last_separator == executable_path.size() - 1) {
      return executable_path;
    }
    return executable_path.substr(last_separator + 1);
  }
  return "Flutter";
}

void FlutterWindowsEngine::UpdateAccessibilityFeatures() {
  UpdateHighContrastMode();
}

void FlutterWindowsEngine::UpdateHighContrastMode() {
  high_contrast_enabled_ = windows_proc_table_->GetHighContrastEnabled();

  SendAccessibilityFeatures();
  settings_plugin_->UpdateHighContrastMode(high_contrast_enabled_);
}

void FlutterWindowsEngine::SendAccessibilityFeatures() {
  int flags = 0;

  if (high_contrast_enabled_) {
    flags |=
        FlutterAccessibilityFeature::kFlutterAccessibilityFeatureHighContrast;
  }

  embedder_api_->UpdateAccessibilityFeatures(
      static_cast<FlutterAccessibilityFeature>(flags));
}

void FlutterWindowsEngine::HandleAccessibilityMessage(
    FlutterDesktopMessengerRef messenger,
    const FlutterDesktopMessage* message) {
  const auto& codec = StandardMessageCodec::GetInstance();
  auto data = codec.DecodeMessage(message->message, message->message_size);
  EncodableMap map = std::get<EncodableMap>(*data);
  std::string type = std::get<std::string>(map.at(EncodableValue("type")));
  if (type.compare("announce") == 0) {
    if (semantics_enabled_) {
      EncodableMap data_map =
          std::get<EncodableMap>(map.at(EncodableValue("data")));
      std::string text =
          std::get<std::string>(data_map.at(EncodableValue("message")));
      std::wstring wide_text = fml::Utf8ToWideString(text);
      view_->AnnounceAlert(wide_text);
    }
  }
  SendPlatformMessageResponse(message->response_handle,
                              reinterpret_cast<const uint8_t*>(""), 0);
}

void FlutterWindowsEngine::RequestApplicationQuit(HWND hwnd,
                                                  WPARAM wparam,
                                                  LPARAM lparam,
                                                  AppExitType exit_type) {
  platform_handler_->RequestAppExit(hwnd, wparam, lparam, exit_type, 0);
}

void FlutterWindowsEngine::OnQuit(std::optional<HWND> hwnd,
                                  std::optional<WPARAM> wparam,
                                  std::optional<LPARAM> lparam,
                                  UINT exit_code) {
  lifecycle_manager_->Quit(hwnd, wparam, lparam, exit_code);
}

void FlutterWindowsEngine::OnDwmCompositionChanged() {
  view_->OnDwmCompositionChanged();
}

void FlutterWindowsEngine::OnWindowStateEvent(HWND hwnd,
                                              WindowStateEvent event) {
  lifecycle_manager_->OnWindowStateEvent(hwnd, event);
}

std::optional<LRESULT> FlutterWindowsEngine::ProcessExternalWindowMessage(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
  if (lifecycle_manager_) {
    return lifecycle_manager_->ExternalWindowMessage(hwnd, message, wparam,
                                                     lparam);
  }
  return std::nullopt;
}

void FlutterWindowsEngine::OnChannelUpdate(std::string name, bool listening) {
  if (name == "flutter/platform" && listening) {
    lifecycle_manager_->BeginProcessingExit();
  } else if (name == "flutter/lifecycle" && listening) {
    lifecycle_manager_->BeginProcessingLifecycle();
  }
}

}  // namespace flutter
