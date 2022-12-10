// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_TESTING_PLUGIN_TEST_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_TESTING_PLUGIN_TEST_H_

#include <memory>

#include "flutter/shell/platform/windows/flutter_windows_engine.h"
#include "flutter/shell/platform/windows/testing/mock_window_binding_handler.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

class PluginTest : public ::testing::Test {
 protected:
  FlutterWindowsEngine* engine() { return engine_.get(); }
  FlutterWindowsView* view() { return view_.get(); }
  MockWindowBindingHandler* window() { return window_; }

  void use_headless_engine() {
    // Set properties required to create the engine.
    FlutterDesktopEngineProperties properties = {};
    properties.assets_path = L"C:\\foo\\flutter_assets";
    properties.icu_data_path = L"C:\\foo\\icudtl.dat";
    properties.aot_library_path = L"C:\\foo\\aot.so";
    FlutterProjectBundle project(properties);

    engine_ = std::make_unique<FlutterWindowsEngine>(project);
  }

  void use_engine_with_view() {
    use_headless_engine();

    auto window = std::make_unique<MockWindowBindingHandler>();
    window_ = window.get();

    EXPECT_CALL(*window_, SetView).Times(1);
    EXPECT_CALL(*window_, GetRenderTarget).WillOnce(::testing::Return(nullptr));

    view_ = std::make_unique<FlutterWindowsView>(std::move(window));

    engine_->SetView(view_.get());
  }

 private:
  std::unique_ptr<FlutterWindowsEngine> engine_;
  std::unique_ptr<FlutterWindowsView> view_;
  MockWindowBindingHandler* window_;
};

}  // namespace testing
}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_TESTING_PLUGIN_TEST_H_
