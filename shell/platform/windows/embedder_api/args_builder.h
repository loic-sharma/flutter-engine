// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_FLUTTER_WINDOWS_EMBEDDER_API_ARGS_BUILDER_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_FLUTTER_WINDOWS_EMBEDDER_API_ARGS_BUILDER_H_

#include "flutter/fml/closure.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/windows/compositor.h"
#include "flutter/shell/platform/windows/egl/manager.h"

namespace flutter {
namespace embedder_api {

typedef std::function<
    void(const FlutterPlatformMessage*, PlatformMessageReply reply)>
    PlatformMessageCallback;

typedef std::function<void(const uint8_t* data, size_t data_length)>
    PlatformMessageResponse;

typedef std::function<void(OnVSyncCallback on_vsync)> VSyncCallback;

typdef std::function<
    void(uint64_t frame_start_time_nanos, uint64_t frame_target_time_nanos)>
    OnVSyncCallback;

typedef std::function<void(std::string_view channel, bool listening)>
    ChannelUpdateCallback;

class ArgsBuilder {
 public:
  void SetProject(const flutter::FlutterProjectBundle* project);
  void SetEGLManager(const egl::Manager* manager);
  void SetCompositor(const flutter::Compositor* compositor);

  void SetPlatformMessageCallback(PlatformMessageCallback callback);
  void SetVSyncCallback(VSyncCallback callback);
  void SetPreEngineRestartCallback(std::function<void> callback);
  void SetUpdateSemanticsCallback(
      std::function<const FlutterSemanticsUpdate2&> callback);

  void SetRootIsolateCreateCallback(fml::closure callback);
  void SetChannelUpdateCallback(ChannelUpdateCallback callback);

  std::unique_ptr<FlutterProjectArgs> Build() const;
};

}  // namespace embedder_api
}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_FLUTTER_WINDOWS_EMBEDDER_API_ARGS_BUILDER_H_