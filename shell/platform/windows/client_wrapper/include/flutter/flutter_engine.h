// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_ENGINE_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_ENGINE_H_

#include <flutter_windows.h>

#include <chrono>
#include <memory>
#include <string>

#include "binary_messenger.h"
#include "dart_project.h"
#include "plugin_registrar.h"
#include "plugin_registry.h"

namespace flutter {

// An instance of a Flutter engine.
//
// In the future, this will be the API surface used for all interactions with
// the engine, rather than having them duplicated on FlutterViewController.
// For now it is only used in the rare case where you need a headless Flutter
// engine.
class FlutterEngine : public PluginRegistry {
 public:
  // Creates a new engine for running the given project.
  explicit FlutterEngine(const DartProject& project);

  virtual ~FlutterEngine();

  // Prevent copying.
  FlutterEngine(FlutterEngine const&) = delete;
  FlutterEngine& operator=(FlutterEngine const&) = delete;

  // Starts running the engine at the entrypoint function specified in the
  // DartProject used to configure the engine, or main() by default.
  bool Run();

  // Starts running the engine, with an optional entry point.
  //
  // If provided, entry_point must be the name of a top-level function from the
  // same Dart library that contains the app's main() function, and must be
  // decorated with `@pragma(vm:entry-point)` to ensure the method is not
  // tree-shaken by the Dart compiler. If not provided, defaults to main().
  bool Run(const char* entry_point);

  // Terminates the running engine.
  void ShutDown();

  // Processes any pending events in the Flutter engine, and returns the
  // nanosecond delay until the next scheduled event (or  max, if none).
  //
  // This should be called on every run of the application-level runloop, and
  // a wait for native events in the runloop should never be longer than the
  // last return value from this function.
  std::chrono::nanoseconds ProcessMessages();

  // Tells the engine that the system font list has changed. Should be called
  // by clients when OS-level font changes happen (e.g., WM_FONTCHANGE in a
  // Win32 application).
  void ReloadSystemFonts();

  // Tells the engine that the platform brightness value has changed. Should be
  // called by clients when OS-level theme changes happen (e.g.,
  // WM_DWMCOLORIZATIONCOLORCHANGED in a Win32 application).
  void ReloadPlatformBrightness();

  // flutter::PluginRegistry:
  FlutterDesktopPluginRegistrarRef GetRegistrarForPlugin(
      const std::string& plugin_name) override;

  // Returns the messenger to use for creating channels to communicate with the
  // Flutter engine.
  //
  // This pointer will remain valid for the lifetime of this instance.
  BinaryMessenger* messenger() { return messenger_.get(); }

  // Schedule a callback to be called after the next frame is drawn.
  //
  // This must be called from the platform thread. The callback is executed only
  // once on the platform thread.
  void SetNextFrameCallback(std::function<void()> callback);

 private:
  // For access to |engine|.
  friend class FlutterViewController;

  FlutterDesktopEngineRef RelinquishEngine();

  // Returns the underlying C API engine.
  //
  // This is intended to be used by FlutterViewController and
  // wil remain valid for the lifetime of this instance.
  FlutterDesktopEngineRef engine();

  // Handle for interacting with the C API's engine reference.
  FlutterDesktopEngineRef engine_ = nullptr;

  // Messenger for communicating with the engine.
  std::unique_ptr<BinaryMessenger> messenger_;

  // Whether the engine has been run. This will be true if Run has been called.
  bool has_been_run_ = false;

  bool owns_engine_ = true;

  // The callback to execute once the next frame is drawn.
  std::function<void()> next_frame_callback_ = nullptr;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_ENGINE_H_
