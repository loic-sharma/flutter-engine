// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_VIEW_CONTROLLER_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_VIEW_CONTROLLER_H_

#include <flutter_windows.h>
#include <windows.h>

#include <memory>
#include <optional>

#include "dart_project.h"
#include "flutter_engine.h"
#include "flutter_view.h"

namespace flutter {

// A controller for a view displaying Flutter content.
//
// This is the primary wrapper class for the desktop C API.
// If you use this class, you should not call any of the setup or teardown
// methods in the C API directly, as this class will do that internally.
class FlutterViewController {
 public:
  // Creates a FlutterView that can be parented into a Windows View hierarchy
  // using HWNDs.
  //
  // |dart_project| will be used to create an engine to back this view controller.
  explicit FlutterViewController(int width,
                                 int height,
                                 const DartProject& project);

  // Creates a FlutterView that can be parented into a Windows View hierarchy
  // using HWNDs.
  //
  // The view controller will be attached to the |engine|, which will be started
  // if it isn't running already.
  explicit FlutterViewController(int width,
                                 int height,
                                 std::shared_ptr<FlutterEngine> engine);

  virtual ~FlutterViewController();

  // Prevent copying.
  FlutterViewController(FlutterViewController const&) = delete;
  FlutterViewController& operator=(FlutterViewController const&) = delete;

  // Returns the engine running Flutter content in this view.
  FlutterEngine* engine();

  // Returns the view managed by this controller.
  FlutterView* view() { return view_.get(); }

  // Requests new frame from the engine and repaints the view.
  void ForceRedraw();

  // Allows the Flutter engine and any interested plugins an opportunity to
  // handle the given message.
  //
  // If a result is returned, then the message was handled in such a way that
  // further handling should not be done.
  std::optional<LRESULT> HandleTopLevelWindowProc(HWND hwnd,
                                                  UINT message,
                                                  WPARAM wparam,
                                                  LPARAM lparam);

 private:
  // Handle for interacting with the C API's view controller, if any.
  FlutterDesktopViewControllerRef controller_ = nullptr;

  // The shared engine, if any.
  std::shared_ptr<FlutterEngine> shared_engine_;

  // The owned engine, if any.
  std::unique_ptr<FlutterEngine> owned_engine_;

  // The owned FlutterView.
  std::unique_ptr<FlutterView> view_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_VIEW_CONTROLLER_H_
