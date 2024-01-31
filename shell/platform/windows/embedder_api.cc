// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/embedder_api.h"

#include "flutter/fml/closure.h"
#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/windows/compositor.h"
#include "flutter/shell/platform/windows/flutter_project_bundle.h"
#include "flutter/shell/platform/windows/task_runner.h"

namespace flutter {

namespace {

#undef GetCurrentTime

// Creates and returns a FlutterRendererConfig that renders to the view (if any)
// of a FlutterWindowsEngine, using OpenGL (via ANGLE).
// The user_data received by the render callbacks refers to the
// FlutterWindowsEngine.
FlutterRendererConfig GetOpenGLRendererConfig() {
  FlutterRendererConfig config = {};
  config.type = kOpenGL;
  config.open_gl.struct_size = sizeof(config.open_gl);
  config.open_gl.make_current = [](void* user_data) -> bool {
    auto callbacks = static_cast<EmbedderApiCallbacks*>(user_data);

    return callbacks->opengl->make_current();
  };
  config.open_gl.clear_current = [](void* user_data) -> bool {
    auto callbacks = static_cast<EmbedderApiCallbacks*>(user_data);

    return callbacks->opengl->clear_current();
  };
  config.open_gl.present = [](void* user_data) -> bool { FML_UNREACHABLE(); };
  config.open_gl.fbo_reset_after_present = true;
  config.open_gl.fbo_with_frame_info_callback =
      [](void* user_data, const FlutterFrameInfo* info) -> uint32_t {
    FML_UNREACHABLE();
  };
  config.open_gl.gl_proc_resolver = [](void* user_data,
                                       const char* what) -> void* {
    auto callbacks = static_cast<EmbedderApiCallbacks*>(user_data);

    return callbacks->opengl->gl_proc_resolver(what);
  };

  config.open_gl.make_resource_current = [](void* user_data) -> bool {
    auto callbacks = static_cast<EmbedderApiCallbacks*>(user_data);

    return callbacks->opengl->make_resource_current();
  };
  config.open_gl.gl_external_texture_frame_callback =
      [](void* user_data, int64_t texture_id, size_t width, size_t height,
         FlutterOpenGLTexture* texture) -> bool {
    auto callbacks = static_cast<EmbedderApiCallbacks*>(user_data);

    return callbacks->opengl->gl_external_texture_frame_callback(
        texture_id, width, height, texture);
  };
  return config;
}

// Creates and returns a FlutterRendererConfig that renders to the view (if any)
// of a FlutterWindowsEngine, using software rasterization.
// The user_data received by the render callbacks refers to the
// FlutterWindowsEngine.
FlutterRendererConfig GetSoftwareRendererConfig() {
  FlutterRendererConfig config = {};
  config.type = kSoftware;
  config.software.struct_size = sizeof(config.software);
  config.software.surface_present_callback =
      [](void* user_data, const void* allocation, size_t row_bytes,
         size_t height) {
        FML_UNREACHABLE();
        return false;
      };
  return config;
}

FlutterCompositor ConvertToFlutterCompositor(Compositor* compositor) {
  FlutterCompositor flutter_compositor = {};
  flutter_compositor.struct_size = sizeof(FlutterCompositor);
  flutter_compositor.user_data = compositor;
  flutter_compositor.create_backing_store_callback =
      [](const FlutterBackingStoreConfig* config,
         FlutterBackingStore* backing_store_out, void* user_data) -> bool {
    auto compositor = static_cast<Compositor*>(user_data);

    return compositor->CreateBackingStore(*config, backing_store_out);
  };

  flutter_compositor.collect_backing_store_callback =
      [](const FlutterBackingStore* backing_store, void* user_data) -> bool {
    auto compositor = static_cast<Compositor*>(user_data);

    return compositor->CollectBackingStore(backing_store);
  };

  flutter_compositor.present_layers_callback = [](const FlutterLayer** layers,
                                                  size_t layers_count,
                                                  void* user_data) -> bool {
    auto compositor = static_cast<Compositor*>(user_data);

    return compositor->Present(layers, layers_count);
  };

  return flutter_compositor;
}

// Converts a LanguageInfo struct to a FlutterLocale struct. |info| must outlive
// the returned value, since the returned FlutterLocale has pointers into it.
FlutterLocale CovertToFlutterLocale(const LanguageInfo& info) {
  FlutterLocale locale = {};
  locale.struct_size = sizeof(FlutterLocale);
  locale.language_code = info.language.c_str();
  if (!info.region.empty()) {
    locale.country_code = info.region.c_str();
  }
  if (!info.script.empty()) {
    locale.script_code = info.script.c_str();
  }
  return locale;
}

}  // namespace

EmbedderApi::EmbedderApi(FlutterEngineProcTable embedder_api,
                         UniqueAotDataPtr aot_data,
                         FLUTTER_API_SYMBOL(FlutterEngine) engine,
                         std::unique_ptr<EmbedderApiCallbacks> callbacks)
    : embedder_api_(embedder_api),
      aot_data_(std::move(aot_data)),
      engine_(engine) {}

std::unique_ptr<EmbedderApi> EmbedderApi::Create(
    const FlutterProjectBundle* project,
    std::string executable_name,
    std::string_view entrypoint,
    TaskRunner* platform_task_runner,
    ThreadPrioritySetter thread_priority_setter,
    Compositor* compositor,
    std::unique_ptr<EmbedderApiCallbacks> callbacks) {
  FlutterEngineProcTable embedder_api = {};
  embedder_api.struct_size = sizeof(FlutterEngineProcTable);
  if (FlutterEngineGetProcAddresses(&embedder_api) != kSuccess) {
    FML_LOG(ERROR) << "Unable to resolve embedder API proc addresses.";
    return nullptr;
  }

  if (!project->HasValidPaths()) {
    FML_LOG(ERROR) << "Missing or unresolvable paths to assets.";
    return nullptr;
  }

  std::string assets_path_string = project->assets_path().u8string();
  std::string icu_path_string = project->icu_path().u8string();

  // FlutterProjectArgs is expecting a full argv, so when processing it for
  // flags the first item is treated as the executable and ignored. Add a dummy
  // value so that all provided arguments are used.
  std::vector<const char*> argv = {executable_name.c_str()};
  std::vector<std::string> switches = project->GetSwitches();
  std::transform(
      switches.begin(), switches.end(), std::back_inserter(argv),
      [](const std::string& arg) -> const char* { return arg.c_str(); });

  const std::vector<std::string>& entrypoint_args =
      project->dart_entrypoint_arguments();
  std::vector<const char*> entrypoint_argv;
  std::transform(
      entrypoint_args.begin(), entrypoint_args.end(),
      std::back_inserter(entrypoint_argv),
      [](const std::string& arg) -> const char* { return arg.c_str(); });

  FlutterProjectArgs args = {};
  args.struct_size = sizeof(FlutterProjectArgs);
  args.shutdown_dart_vm_when_done = true;
  args.assets_path = assets_path_string.c_str();
  args.icu_data_path = icu_path_string.c_str();
  args.command_line_argc = static_cast<int>(argv.size());
  args.command_line_argv = argv.empty() ? nullptr : argv.data();

  // Fail if conflicting non-default entrypoints are specified in the method
  // argument and the project.
  //
  // TODO(cbracken): https://github.com/flutter/flutter/issues/109285
  // The entrypoint method parameter should eventually be removed from this
  // method and only the entrypoint specified in project_ should be used.
  if (!project->dart_entrypoint().empty() && !entrypoint.empty() &&
      project->dart_entrypoint() != entrypoint) {
    FML_LOG(ERROR) << "Conflicting entrypoints were specified in "
                      "FlutterDesktopEngineProperties.dart_entrypoint and "
                      "FlutterDesktopEngineRun(engine, entry_point). ";
    return nullptr;
  }
  if (!entrypoint.empty()) {
    args.custom_dart_entrypoint = entrypoint.data();
  } else if (!project->dart_entrypoint().empty()) {
    args.custom_dart_entrypoint = project->dart_entrypoint().c_str();
  }
  args.dart_entrypoint_argc = static_cast<int>(entrypoint_argv.size());
  args.dart_entrypoint_argv =
      entrypoint_argv.empty() ? nullptr : entrypoint_argv.data();

  // Configure AOT data
  // TODO: AOT data needs to be kept in the embedder API instance!!
  UniqueAotDataPtr aot_data{nullptr, nullptr};
  if (embedder_api.RunsAOTCompiledDartCode()) {
    aot_data = project->LoadAotData(embedder_api);
    if (!aot_data) {
      FML_LOG(ERROR) << "Unable to start engine without AOT data.";
      return nullptr;
    }
  }

  if (aot_data) {
    args.aot_data = aot_data.get();
  }

  // Configure task runners.
  FlutterTaskRunnerDescription platform_task_runner_desc = {};
  platform_task_runner_desc.struct_size = sizeof(FlutterTaskRunnerDescription);
  platform_task_runner_desc.user_data = platform_task_runner;
  platform_task_runner_desc.runs_task_on_current_thread_callback =
      [](void* user_data) -> bool {
    return static_cast<TaskRunner*>(user_data)->RunsTasksOnCurrentThread();
  };
  platform_task_runner_desc.post_task_callback = [](FlutterTask task,
                                                    uint64_t target_time_nanos,
                                                    void* user_data) -> void {
    static_cast<TaskRunner*>(user_data)->PostFlutterTask(task,
                                                         target_time_nanos);
  };
  FlutterCustomTaskRunners custom_task_runners = {};
  custom_task_runners.struct_size = sizeof(FlutterCustomTaskRunners);
  custom_task_runners.platform_task_runner = &platform_task_runner_desc;
  custom_task_runners.thread_priority_setter = thread_priority_setter;

  args.custom_task_runners = &custom_task_runners;

  // Configure callbacks.
  args.platform_message_callback = [](const FlutterPlatformMessage* message,
                                      void* user_data) -> void {
    auto embedder_api = static_cast<EmbedderApi*>(user_data);

    embedder_api->callbacks_->platform_message_callback(message);
  };
  args.vsync_callback = [](void* user_data, intptr_t baton) -> void {
    auto embedder_api = static_cast<EmbedderApi*>(user_data);

    embedder_api->callbacks_->vsync_callback(
        [embedder_api, baton](uint64_t frame_start_time_nanos,
                              uint64_t frame_target_time_nanos) {
          embedder_api->embedder_api_.OnVsync(embedder_api->engine_, baton,
                                              frame_start_time_nanos,
                                              frame_target_time_nanos);
        });
  };
  args.root_isolate_create_callback = [](void* user_data) {
    auto embedder_api = static_cast<EmbedderApi*>(user_data);
    embedder_api->callbacks_->root_isolate_create_callback();
  };
  args.on_pre_engine_restart_callback = [](void* user_data) {
    auto embedder_api = static_cast<EmbedderApi*>(user_data);
    embedder_api->callbacks_->on_pre_engine_restart_callback();
  };
  args.update_semantics_callback2 = [](const FlutterSemanticsUpdate2* update,
                                       void* user_data) {
    auto embedder_api = static_cast<EmbedderApi*>(user_data);
    embedder_api->callbacks_->semantics_update_callback(update);
  };
  args.channel_update_callback = [](const FlutterChannelUpdate* update,
                                    void* user_data) {
    auto embedder_api = static_cast<EmbedderApi*>(user_data);
    std::string channel(update->channel);
    bool listening = update->listening;
    embedder_api->callbacks_->channel_update_callback(channel, listening);
  };

  FlutterCompositor flutter_compositor = ConvertToFlutterCompositor(compositor);
  args.compositor = &flutter_compositor;

  FlutterRendererConfig renderer_config = callbacks->opengl.has_value()
                                              ? GetOpenGLRendererConfig()
                                              : GetSoftwareRendererConfig();

  FLUTTER_API_SYMBOL(FlutterEngine) engine;

  auto result = embedder_api.Run(FLUTTER_ENGINE_VERSION, &renderer_config,
                                 &args, callbacks.get(), &engine);

  if (result != kSuccess) {
    FML_LOG(ERROR) << "Failed to start Flutter engine: error " << result;
    return nullptr;
  }

  return std::make_unique<EmbedderApi>(embedder_api, std::move(aot_data),
                                       engine, std::move(callbacks));
}

bool EmbedderApi::Shutdown() {
  return embedder_api_.Shutdown(engine_) == kSuccess;
}

uint64_t EmbedderApi::GetEngineCurrentTime() const {
  return embedder_api_.GetCurrentTime();
}

void EmbedderApi::SendWindowMetricsEvent(
    const FlutterWindowMetricsEvent* event) {
  embedder_api_.SendWindowMetricsEvent(engine_, event);
}

void EmbedderApi::SendPointerEvent(const FlutterPointerEvent* event) {
  embedder_api_.SendPointerEvent(engine_, event, 1);
}

void EmbedderApi::SendKeyEvent(const FlutterKeyEvent* event,
                               KeyEventCallback callback) {
  struct Captures {
    KeyEventCallback callback;
  };
  auto captures = new Captures();
  captures->callback = std::move(callback);

  if (embedder_api_.SendKeyEvent(
          engine_, event,
          [](bool handled, void* user_data) {
            auto captures = static_cast<Captures*>(user_data);
            captures->callback(handled);
            delete captures;
          },
          &captures) == kSuccess) {
    return;
  }

  delete captures;
}

bool EmbedderApi::SendPlatformMessage(const std::string_view channel,
                                      const uint8_t* message,
                                      const size_t message_size,
                                      DataCallback on_response) {
  struct Captures {
    DataCallback on_response;
  };

  FlutterPlatformMessageResponseHandle* response_handle = nullptr;
  Captures* captures = nullptr;
  if (on_response != nullptr) {
    captures = new Captures();
    captures->on_response = std::move(on_response);
    auto response_callback = [](const uint8_t* data, size_t data_size,
                                void* user_data) {
      auto captures = static_cast<Captures*>(user_data);
      captures->on_response(data, data_size);
      delete captures;
    };

    if (embedder_api_.PlatformMessageCreateResponseHandle(
            engine_, response_callback, &on_response, &response_handle) !=
        kSuccess) {
      FML_LOG(ERROR) << "Failed to create response handle";
      delete captures;
      return false;
    }
  }

  FlutterPlatformMessage platform_message = {};
  platform_message.struct_size = sizeof(FlutterPlatformMessage);
  platform_message.channel = channel.data();
  platform_message.message = message;
  platform_message.message_size = message_size;
  platform_message.response_handle = response_handle;

  FlutterEngineResult result =
      embedder_api_.SendPlatformMessage(engine_, &platform_message);

  if (response_handle != nullptr) {
    embedder_api_.PlatformMessageReleaseResponseHandle(engine_,
                                                       response_handle);
  }

  if (result != kSuccess) {
    delete captures;
  }

  return result == kSuccess;
}

void EmbedderApi::SendPlatformMessageResponse(
    const FlutterPlatformMessageResponseHandle* handle,
    const uint8_t* data,
    size_t data_length) {
  embedder_api_.SendPlatformMessageResponse(engine_, handle, data, data_length);
}

void EmbedderApi::ReloadSystemFonts() {
  embedder_api_.ReloadSystemFonts(engine_);
}

void EmbedderApi::ScheduleFrame() {
  embedder_api_.ScheduleFrame(engine_);
}

void EmbedderApi::SetNextFrameCallback(fml::closure callback) {
  next_frame_callback_ = std::move(callback);

  embedder_api_.SetNextFrameCallback(
      engine_,
      [](void* user_data) {
        auto embedder_api = static_cast<EmbedderApi*>(user_data);

        embedder_api->next_frame_callback_();
        embedder_api->next_frame_callback_ = nullptr;
      },
      this);
}

void EmbedderApi::UpdateLocales(std::vector<LanguageInfo> languages) {
  std::vector<FlutterLocale> flutter_locales;
  flutter_locales.reserve(languages.size());
  for (const auto& info : languages) {
    flutter_locales.push_back(CovertToFlutterLocale(info));
  }

  // Convert the locale list to the locale pointer list that must be provided.
  std::vector<const FlutterLocale*> flutter_locale_list;
  flutter_locale_list.reserve(flutter_locales.size());
  std::transform(flutter_locales.begin(), flutter_locales.end(),
                 std::back_inserter(flutter_locale_list),
                 [](const auto& arg) -> const auto* { return &arg; });

  embedder_api_.UpdateLocales(engine_, flutter_locale_list.data(),
                              flutter_locale_list.size());
}

bool EmbedderApi::RegisterExternalTexture(int64_t texture_id) {
  return embedder_api_.RegisterExternalTexture(engine_, texture_id) == kSuccess;
}

bool EmbedderApi::UnregisterExternalTexture(int64_t texture_id) {
  return embedder_api_.UnregisterExternalTexture(engine_, texture_id) ==
         kSuccess;
}

bool EmbedderApi::MarkExternalTextureFrameAvailable(int64_t texture_id) {
  return embedder_api_.MarkExternalTextureFrameAvailable(engine_, texture_id) ==
         kSuccess;
}

bool EmbedderApi::PostRasterThreadTask(fml::closure callback) const {
  struct Captures {
    fml::closure callback;
  };
  auto captures = new Captures();
  captures->callback = std::move(callback);
  if (embedder_api_.PostRenderThreadTask(
          engine_,
          [](void* opaque) {
            auto captures = reinterpret_cast<Captures*>(opaque);
            captures->callback();
            delete captures;
          },
          captures) == kSuccess) {
    return true;
  }
  delete captures;
  return false;
}

bool EmbedderApi::SetSemanticsEnabled(bool enabled) {
  return embedder_api_.UpdateSemanticsEnabled(engine_, enabled) == kSuccess;
}

bool EmbedderApi::DispatchSemanticsAction(uint64_t target,
                                          FlutterSemanticsAction action,
                                          fml::MallocMapping data) {
  return embedder_api_.DispatchSemanticsAction(engine_, target, action,
                                               data.GetMapping(),
                                               data.GetSize()) == kSuccess;
}

void EmbedderApi::UpdateAccessibilityFeatures(
    FlutterAccessibilityFeature features) {
  embedder_api_.UpdateAccessibilityFeatures(engine_, features);
}

void EmbedderApi::NotifyDisplayUpdate(
    FlutterEngineDisplaysUpdateType update_type,
    std::vector<FlutterEngineDisplay> displays) {
  embedder_api_.NotifyDisplayUpdate(engine_,
                                    update_type,
                                    displays.data(), displays.size());
}

}  // namespace flutter
