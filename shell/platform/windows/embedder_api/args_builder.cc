// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/embedder_api/args_builder.h"

namespace flutter {
namespace embedder_api {

void ArgsBuilder::SetProject(const flutter::FlutterProjectBundle* project) {

}

void ArgsBuilder::SetEGLManager(const egl::Manager* manager) {

}

void ArgsBuilder::SetCompositor(const flutter::Compositor* compositor) {

}

void ArgsBuilder::SetPlatformMessageCallback(PlatformMessageCallback callback) {

}

void ArgsBuilder::SetVSyncCallback(VSyncCallback callback) {

}

void ArgsBuilder::SetPreEngineRestartCallback(std::function<void> callback) {

}

void ArgsBuilder::SetUpdateSemanticsCallback(std::function<const FlutterSemanticsUpdate2&> callback) {

}

void ArgsBuilder::SetRootIsolateCreateCallback(fml::closure callback) {

}

void ArgsBuilder::SetChannelUpdateCallback(ChannelUpdateCallback callback) {

}

std::unique_ptr<FlutterProjectArgs> ArgsBuilder::Build() const {
    std::make_unique<FlutterProjectArgs>();
}

}  // namespace embedder_api
}  // namespace flutter
