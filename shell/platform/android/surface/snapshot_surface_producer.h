// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHELL_PLATFORM_ANDROID_SURFACE_SNAPSHOT_SURFACE_PRODUCER_H_
#define SHELL_PLATFORM_ANDROID_SURFACE_SNAPSHOT_SURFACE_PRODUCER_H_

#include "flutter/shell/common/snapshot_surface_producer.h"
#include "flutter/shell/platform/android/surface/android_surface.h"

namespace flutter {

class AndroidSnapshotSurfaceProducer : public SnapshotSurfaceProducer {
 public:
  explicit AndroidSnapshotSurfaceProducer(AndroidSurface& android_surface);

  // |SnapshotSurfaceProducer|
  std::unique_ptr<Studio> CreateSnapshotStudio() override;

 private:
  AndroidSurface& android_surface_;
};

}  // namespace flutter
#endif  // SHELL_PLATFORM_ANDROID_SURFACE_SNAPSHOT_SURFACE_PRODUCER_H_
