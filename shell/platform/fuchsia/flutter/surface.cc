// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/fuchsia/flutter/surface.h"

namespace flutter_runner {

Surface::Surface(std::shared_ptr<SessionConnection> session_connection,
                 bool software)
    : session_connection_(std::move(session_connection)) {}

Surface::~Surface() = default;

bool Surface::IsValid() {
  return true;
}

std::unique_ptr<flutter::SurfaceFrame> Surface::AcquireFrame(
    const SkISize& size) {
  return std::make_unique<flutter::SurfaceFrame>(
      nullptr, true,
      [](const flutter::SurfaceFrame& surface_frame, SkCanvas* canvas) {
        return true;
      });
}

GrContext* Surface::GetContext() {
  return nullptr;
}

SkMatrix Surface::GetRootTransformation() const {
  // This backend does not support delegating to the underlying platform to
  // query for root surface transformations. Just return identity.
  SkMatrix matrix;
  matrix.reset();
  return matrix;
}

}  // namespace flutter_runner
