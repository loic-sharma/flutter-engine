// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_FLUTTER_WINDOWS_EMBEDDER_API_ENGINE_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_FLUTTER_WINDOWS_EMBEDDER_API_ENGINE_H_

#include <string_view>

#include "flutter/shell/platform/embedder/embedder.h"

namespace flutter {
namespace embedder_api {

typedef std::function<void(bool handled) KeyEventCallback;

typedef std::function<void(const uint8_t* reply, size_t reply_size)>
    PlatformMessageReply;

// Mirror: embedder.h and embedder_engine.h
class Engine {
 public:
  static std::unique_ptr<Engine> Launch(
    const FlutterProjectArgs& args);

  void Shutdown();

  // Informs the engine that the window metrics have changed.
  void SendWindowMetricsEvent(const FlutterWindowMetricsEvent& event);

  // Informs the engine of an incoming pointer event.
  void SendPointerEvent(const FlutterPointerEvent& event);

  // Informs the engine of an incoming key event.
  void SendKeyEvent(const FlutterKeyEvent& event, KeyEventCallback callback);

  // Sends the given message to the engine, calling |reply| with |user_data|
  // when a response is received from the engine if they are non-null.
  bool SendPlatformMessage(const std::string& channel,
                           const uint8_t* message,
                           const size_t message_size,
                           PlatformMessageReply reply = nullptr);

  // Informs the engine that the system font list has changed.
  void ReloadSystemFonts();

  // Informs the engine that a new frame is needed to redraw the content.
  bool ScheduleFrame();

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

  void NotifiyDisplayUpdate(std::vector<FlutterEngineDisplay> displays);

 private:
  // The handle to the embedder.h engine instance.
  FLUTTER_API_SYMBOL(FlutterEngine) engine_ = nullptr;

  FlutterEngineProcTable embedder_api_ = {};
};

}  // namespace embedder_api
}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_FLUTTER_WINDOWS_EMBEDDER_API_ENGINE_H_