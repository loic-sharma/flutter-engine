// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/cursor_handler.h"

#include <memory>

#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_message_codec.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"
#include "flutter/shell/platform/windows/flutter_windows_view.h"
#include "flutter/shell/platform/windows/testing/mock_window_binding_handler.h"
#include "flutter/shell/platform/windows/testing/test_binary_messenger.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

static constexpr char kChannelName[] = "flutter/mousecursor";

std::unique_ptr<EncodableValue> SimulateCursorMessage(
    TestBinaryMessenger* messenger,
    std::unique_ptr<EncodableValue> arguments) {
  MethodCall<> call("activateSystemCursor", std::move(arguments));

  auto message = StandardMethodCodec::GetInstance().EncodeMethodCall(call);

  std::unique_ptr<EncodableValue> result;
  EXPECT_TRUE(messenger->SimulateEngineMessage(
      kChannelName, message->data(), message->size(),
      [result = &result](const uint8_t* reply, size_t reply_size) {
        *result = StandardMessageCodec::GetInstance().DecodeMessage(reply,
                                                                    reply_size);
      }));

  return result;
}

}  // namespace

class CursorHandlerTest : public ::testing::Test {
 protected:
  FlutterWindowsEngine* engine() { return engine_.get(); }
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
    EXPECT_CALL(*window_, GetRenderTarget).WillOnce(Return(nullptr));

    view_ = std::make_unique<FlutterWindowsView>(std::move(window));

    engine_->SetView(view_.get());
  }

 private:
  std::unique_ptr<FlutterWindowsEngine> engine_;
  std::unique_ptr<FlutterWindowsView> view_;
  MockWindowBindingHandler* window_;
};

TEST_F(CursorHandlerTest, ActivateSystemCursor) {
  use_engine_with_view();

  TestBinaryMessenger messenger;
  CursorHandler cursor_handler(&messenger, engine());

  EXPECT_CALL(*window(), UpdateFlutterCursor("click")).Times(1);

  std::unique_ptr<EncodableValue> result = SimulateCursorMessage(
      &messenger, std::make_unique<EncodableValue>(EncodableMap{
                      {EncodableValue("device"), EncodableValue(0)},
                      {EncodableValue("kind"), EncodableValue("click")},
                  }));

  EXPECT_TRUE(result->IsNull());
}

TEST_F(CursorHandlerTest, ActivateSystemCursorWithHeadlessEngine) {
  use_headless_engine();

  TestBinaryMessenger messenger;
  CursorHandler cursor_handler(&messenger, engine());

  std::unique_ptr<EncodableValue> result = SimulateCursorMessage(
      &messenger, std::make_unique<EncodableValue>(EncodableMap{
                      {EncodableValue("device"), EncodableValue(0)},
                      {EncodableValue("kind"), EncodableValue("click")},
                  }));

  EXPECT_TRUE(result->IsNull());
}

}  // namespace testing
}  // namespace flutter