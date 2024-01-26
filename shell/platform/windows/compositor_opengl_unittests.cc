// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <vector>

#include "flutter/impeller/renderer/backend/gles/gles.h"
#include "flutter/shell/platform/windows/compositor_opengl.h"
#include "flutter/shell/platform/windows/egl/manager.h"
#include "flutter/shell/platform/windows/flutter_windows_view.h"
#include "flutter/shell/platform/windows/testing/egl/mock_manager.h"
#include "flutter/shell/platform/windows/testing/egl/mock_window_surface.h"
#include "flutter/shell/platform/windows/testing/engine_modifier.h"
#include "flutter/shell/platform/windows/testing/flutter_windows_engine_builder.h"
#include "flutter/shell/platform/windows/testing/mock_window_binding_handler.h"
#include "flutter/shell/platform/windows/testing/view_modifier.h"
#include "flutter/shell/platform/windows/testing/windows_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {
using ::testing::Return;

const unsigned char* MockGetString(GLenum name) {
  switch (name) {
    case GL_VERSION:
    case GL_SHADING_LANGUAGE_VERSION:
      return reinterpret_cast<const unsigned char*>("3.0");
    default:
      return reinterpret_cast<const unsigned char*>("");
  }
}

void MockGetIntegerv(GLenum name, int* value) {
  *value = 0;
}

GLenum MockGetError() {
  return GL_NO_ERROR;
}

void DoNothing() {}

const impeller::ProcTableGLES::Resolver kMockResolver = [](const char* name) {
  std::string function_name{name};

  if (function_name == "glGetString") {
    return reinterpret_cast<void*>(&MockGetString);
  } else if (function_name == "glGetIntegerv") {
    return reinterpret_cast<void*>(&MockGetIntegerv);
  } else if (function_name == "glGetError") {
    return reinterpret_cast<void*>(&MockGetError);
  } else {
    return reinterpret_cast<void*>(&DoNothing);
  }
};

class CompositorOpenGLTest : public WindowsTest {
 public:
  CompositorOpenGLTest() = default;
  virtual ~CompositorOpenGLTest() = default;

 protected:
  FlutterWindowsEngine* engine() { return engine_.get(); }
  egl::MockManager* egl_manager() { return egl_manager_; }
  egl::MockWindowSurface* surface() { return surface_; }

  void UseHeadlessEngine() {
    auto egl_manager = std::make_unique<egl::MockManager>();
    egl_manager_ = egl_manager.get();

    FlutterWindowsEngineBuilder builder{GetContext()};

    engine_ = builder.Build();
    EngineModifier modifier(engine_.get());
    modifier.SetEGLManager(std::move(egl_manager));
  }

  void UseEngineWithView() {
    UseHeadlessEngine();

    auto window = std::make_unique<MockWindowBindingHandler>();
    EXPECT_CALL(*window.get(), SetView).Times(1);
    EXPECT_CALL(*window.get(), GetWindowHandle).WillRepeatedly(Return(nullptr));

    view_ = std::make_unique<FlutterWindowsView>(std::move(window));
    engine_->SetView(view_.get());

    auto surface = std::make_unique<egl::MockWindowSurface>();
    surface_ = surface.get();
    ViewModifier modifier(view_.get());
    modifier.SetSurface(std::move(surface));
  }

 private:
  std::unique_ptr<FlutterWindowsEngine> engine_;
  std::unique_ptr<FlutterWindowsView> view_;
  egl::MockManager* egl_manager_;
  egl::MockWindowSurface* surface_;

  FML_DISALLOW_COPY_AND_ASSIGN(CompositorOpenGLTest);
};

}  // namespace

TEST_F(CompositorOpenGLTest, CreateBackingStore) {
  UseHeadlessEngine();

  auto compositor = CompositorOpenGL{engine(), kMockResolver};

  FlutterBackingStoreConfig config = {};
  FlutterBackingStore backing_store = {};

  EXPECT_CALL(*surface(), MakeCurrent).WillOnce(Return(true));
  ASSERT_TRUE(compositor.CreateBackingStore(config, &backing_store));
  ASSERT_TRUE(compositor.CollectBackingStore(&backing_store));
}

TEST_F(CompositorOpenGLTest, InitializationFailure) {
  UseHeadlessEngine();

  auto compositor = CompositorOpenGL{engine(), kMockResolver};

  FlutterBackingStoreConfig config = {};
  FlutterBackingStore backing_store = {};

  EXPECT_CALL(*surface(), MakeCurrent).WillOnce(Return(false));
  EXPECT_FALSE(compositor.CreateBackingStore(config, &backing_store));
}

TEST_F(CompositorOpenGLTest, Present) {
  UseEngineWithView();

  auto compositor = CompositorOpenGL{engine(), kMockResolver};

  FlutterBackingStoreConfig config = {};
  FlutterBackingStore backing_store = {};

  EXPECT_CALL(*surface(), MakeCurrent).WillOnce(Return(true));
  ASSERT_TRUE(compositor.CreateBackingStore(config, &backing_store));

  FlutterLayer layer = {};
  layer.type = kFlutterLayerContentTypeBackingStore;
  layer.backing_store = &backing_store;
  const FlutterLayer* layer_ptr = &layer;

  EXPECT_CALL(*surface(), MakeCurrent).WillOnce(Return(true));
  EXPECT_CALL(*surface(), SwapBuffers).WillOnce(Return(true));
  EXPECT_TRUE(compositor.Present(&layer_ptr, 1));

  ASSERT_TRUE(compositor.CollectBackingStore(&backing_store));
}

TEST_F(CompositorOpenGLTest, PresentEmpty) {
  UseEngineWithView();

  auto compositor = CompositorOpenGL{engine(), kMockResolver};

  // The context will be bound twice: first to initialize the compositor, second
  // to clear the surface.
  EXPECT_CALL(*surface(), MakeCurrent).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(*surface(), SwapBuffers).WillOnce(Return(true));
  EXPECT_TRUE(compositor.Present(nullptr, 0));
}

TEST_F(CompositorOpenGLTest, HeadlessPresentIgnored) {
  UseHeadlessEngine();

  auto compositor = CompositorOpenGL{engine(), kMockResolver};

  FlutterBackingStoreConfig config = {};
  FlutterBackingStore backing_store = {};

  EXPECT_CALL(*surface(), MakeCurrent).WillOnce(Return(true));
  ASSERT_TRUE(compositor.CreateBackingStore(config, &backing_store));

  FlutterLayer layer = {};
  layer.type = kFlutterLayerContentTypeBackingStore;
  layer.backing_store = &backing_store;
  const FlutterLayer* layer_ptr = &layer;

  EXPECT_FALSE(compositor.Present(&layer_ptr, 1));

  ASSERT_TRUE(compositor.CollectBackingStore(&backing_store));
}

}  // namespace testing
}  // namespace flutter
