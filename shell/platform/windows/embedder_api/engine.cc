// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/embedder_api/engine.h"

namespace flutter {
namespace embedder_api {

static std::unique_ptr<Engine> Engine::Launch(const FlutterProjectArgs& args) {

}

void Engine::Shutdown() {

}

void Engine::SendWindowMetricsEvent(const FlutterWindowMetricsEvent& event) {

}

void Engine::SendPointerEvent(const FlutterPointerEvent& event) {

}

void Engine::SendKeyEvent(const FlutterKeyEvent& event, KeyEventCallback callback) {

}

bool Engine::SendPlatformMessage(const std::string& channel,
                        const uint8_t* message,
                        const size_t message_size,
                        PlatformMessageReply reply = nullptr) {

                        }

void Engine::ReloadSystemFonts() {

}

bool Engine::ScheduleFrame() {

}

void Engine::SetNextFrameCallback(fml::closure callback) {

}

void Engine::UpdateLocales(std::vector<LanguageInfo> languages) {

}

bool Engine::RegisterExternalTexture(int64_t texture_id) {

}

bool Engine::UnregisterExternalTexture(int64_t texture_id) {

}

bool Engine::MarkExternalTextureFrameAvailable(int64_t texture_id) {

}

bool Engine::PostRasterThreadTask(fml::closure callback) const {

}

bool Engine::SetSemanticsEnabled(bool enabled) {

}

bool Engine::DispatchSemanticsAction(uint64_t target,
                               FlutterSemanticsAction action,
                               fml::MallocMapping data) {

}

void Engine::UpdateAccessibilityFeatures(FlutterAccessibilityFeature features) {

}

void Engine::NotifiyDisplayUpdate(std::vector<FlutterEngineDisplay> displays) {

}

}  // namespace embedder_api
}  // namespace flutter
