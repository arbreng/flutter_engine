// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_FUCHSIA_SURFACE_H_
#define FLUTTER_SHELL_PLATFORM_FUCHSIA_SURFACE_H_

#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/common/surface.h"
#include "flutter/shell/platform/fuchsia/flutter/session_connection.h"

namespace flutter_runner {

// The interface between the Flutter rasterizer and the underlying platform. May
// be constructed on any thread but will be used by the engine only on the
// raster thread.
class FuchsiaSurface {
 public:
  Surface(std::shared_ptr<SessionConnection> session_connection);
  ~Surface() override;

  std::unique_ptr<Surface> CreateGPUSurface();

 private:
  std::shared_ptr<SessionConnection> session_connection_;

  // |flutter::Surface|
  bool IsValid() override;

  // |flutter::Surface|
  std::unique_ptr<flutter::SurfaceFrame> AcquireFrame(
      const SkISize& size) override;

  // |flutter::Surface|
  GrContext* GetContext() override;

  // |flutter::Surface|
  SkMatrix GetRootTransformation() const override;

  static bool CanConnectToDisplay();

  FML_DISALLOW_COPY_AND_ASSIGN(Surface);
};

}  // namespace flutter_runner

#endif  // FLUTTER_SHELL_PLATFORM_FUCHSIA_SURFACE_H_