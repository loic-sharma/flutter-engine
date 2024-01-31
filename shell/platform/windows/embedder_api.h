// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_EMBEDDER_API_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_EMBEDDER_API_H_

#include <functional>
#include <string_view>

#include "flutter/fml/closure.h"
#include "flutter/fml/mapping.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/windows/compositor.h"
#include "flutter/shell/platform/windows/egl/manager.h"
#include "flutter/shell/platform/windows/flutter_project_bundle.h"
#include "flutter/shell/platform/windows/system_utils.h"
#include "flutter/shell/platform/windows/task_runner.h"

namespace flutter {

using ThreadPrioritySetter = void (*)(FlutterThreadPriority priority);

typedef std::function<void*(const char* name)> GLProcResolver;

typedef std::function<bool(int64_t texture_id,
                           size_t width,
                           size_t height,
                           FlutterOpenGLTexture* texture)>
    GLExternalTextureFrameCallback;

typedef std::function<void(const uint8_t* data, size_t data_length)>
    DataCallback;

typedef std::function<void(const FlutterPlatformMessage*)>
    PlatformMessageCallback;

typedef std::function<void(uint64_t frame_start_time_nanos,
                           uint64_t frame_target_time_nanos)>
    OnVSyncCallback;

typedef std::function<void(OnVSyncCallback on_vsync)> VSyncCallback;

typedef std::function<void(const FlutterSemanticsUpdate2* update)>
    SemanticsUpdateCallback;

typedef std::function<void(std::string_view channel, bool listening)>
    ChannelUpdateCallback;

typedef std::function<void(bool handled)> KeyEventCallback;

struct OpenGLCallbacks {
  std::function<bool()> make_current;
  std::function<bool()> make_resource_current;
  std::function<bool()> clear_current;
  GLProcResolver gl_proc_resolver;
  GLExternalTextureFrameCallback gl_external_texture_frame_callback;
};

struct EmbedderApiCallbacks {
  std::optional<OpenGLCallbacks> opengl;

  fml::closure root_isolate_create_callback;
  fml::closure on_pre_engine_restart_callback;

  PlatformMessageCallback platform_message_callback;
  VSyncCallback vsync_callback;
  SemanticsUpdateCallback semantics_update_callback;
  ChannelUpdateCallback channel_update_callback;
};

// Mirror: embedder.h and embedder_engine.h
class EmbedderApi {
 public:
  EmbedderApi();

  bool Run(const FlutterProjectBundle* project,
           std::string executable_name,
           std::string_view entrypoint,
           TaskRunner* platform_task_runner,
           ThreadPrioritySetter thread_priority_setter,
           Compositor* compositor,
           std::unique_ptr<EmbedderApiCallbacks> callbacks);

  bool Running();

  bool Shutdown();

  uint64_t CurrentTime() const;

  // Informs the engine that the window metrics have changed.
  void SendWindowMetricsEvent(const FlutterWindowMetricsEvent* event);

  // Informs the engine of an incoming pointer event.
  void SendPointerEvent(const FlutterPointerEvent* event);

  // Informs the engine of an incoming key event.
  void SendKeyEvent(const FlutterKeyEvent* event, KeyEventCallback callback);

  // Sends the given message to the engine, calling |reply| with |user_data|
  // when a response is received from the engine if they are non-null.
  bool SendPlatformMessage(const std::string_view channel,
                           const uint8_t* message,
                           const size_t message_size,
                           DataCallback on_response = nullptr);

  void SendPlatformMessageResponse(
      const FlutterPlatformMessageResponseHandle* handle,
      const uint8_t* data,
      size_t data_length);

  // Informs the engine that the system font list has changed.
  void ReloadSystemFonts();

  // Informs the engine that a new frame is needed to redraw the content.
  void ScheduleFrame();

  // Set the callback that is called when the next frame is drawn.
  void SetNextFrameCallback(fml::closure callback);

  void UpdateLocales(std::vector<LanguageInfo> languages);

  bool RegisterExternalTexture(int64_t texture_id);
  bool UnregisterExternalTexture(int64_t texture_id);
  bool MarkExternalTextureFrameAvailable(int64_t texture_id);

  bool PostRasterThreadTask(fml::closure callback) const;

  bool SetSemanticsEnabled(bool enabled);

  bool DispatchSemanticsAction(uint64_t target,
                               FlutterSemanticsAction action,
                               fml::MallocMapping data);

  void UpdateAccessibilityFeatures(FlutterAccessibilityFeature features);

  void NotifyDisplayUpdate(FlutterEngineDisplaysUpdateType update_type,
                           std::vector<FlutterEngineDisplay> displays);

 private:
  FlutterEngineProcTable embedder_api_ = {};

  // AOT data, if any.
  UniqueAotDataPtr aot_data_;

  // The handle to the embedder.h engine instance.
  FLUTTER_API_SYMBOL(FlutterEngine) engine_ = nullptr;

  std::unique_ptr<EmbedderApiCallbacks> callbacks_;

  fml::closure next_frame_callback_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_EMBEDDER_API_H_