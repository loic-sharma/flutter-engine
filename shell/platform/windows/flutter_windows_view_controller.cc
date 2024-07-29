// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/flutter_windows_view_controller.h"

namespace flutter {

FlutterWindowsViewController::~FlutterWindowsViewController() {
  Destroy();
}

void FlutterWindowsViewController::Destroy() {
  if (!view_) {
    return;
  }

  // Stop the engine from rendering into the view.
  // This must happen before the view or its surface are destroyed
  // as the engine makes the view's surface current to clean up resources.
  view_->GetEngine()->Stop();

  // Destroy the view, followed by the engine if it is owned by this controller.
  view_.reset();
  engine_.reset();
}

}  // namespace flutter
