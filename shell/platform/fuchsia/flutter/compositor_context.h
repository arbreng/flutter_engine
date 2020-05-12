// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_FUCHSIA_COMPOSITOR_CONTEXT_H_
#define FLUTTER_SHELL_PLATFORM_FUCHSIA_COMPOSITOR_CONTEXT_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <zircon/types.h>

#include <memory>

#include "flutter/flow/compositor_context.h"
#include "flutter/flow/embedded_views.h"
#include "flutter/flow/scene_update_context.h"
#include "flutter/fml/macros.h"
#include "flutter/shell/platform/fuchsia/flutter/session_connection.h"

namespace flutter_runner {

// Holds composition specific state and bindings specific to composition on
// Fuchsia.
class CompositorContext final : public flutter::CompositorContext {
 public:
  CompositorContext(
      std::string debug_label,
      fuchsia::ui::views::ViewToken view_token,
      scenic::ViewRefPair view_ref_pair,
      fuchsia::ui::scenic::SessionPtr session,
      SessionConnection::SessionErrorCallback session_error_callback,
      zx_handle_t vsync_event_handle);
  ~CompositorContext() override;

  void OnSessionMetricsChanged(const fuchsia::ui::gfx::Metrics& metrics);
  void OnDebugViewBoundsEnabled(bool enabled);

 private:
  // |flutter::CompositorContext|
  std::unique_ptr<ScopedFrame> AcquireFrame(
      GrContext* gr_context,
      SkCanvas* canvas,
      flutter::ExternalViewEmbedder* view_embedder,
      const SkMatrix& root_surface_transformation,
      bool instrumentation_enabled,
      bool surface_supports_readback,
      fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) override;

  SessionConnection session_connection_;
  flutter::SceneUpdateContext scene_update_context_;

  FML_DISALLOW_COPY_AND_ASSIGN(CompositorContext);
};

}  // namespace flutter_runner

#endif  // FLUTTER_SHELL_PLATFORM_FUCHSIA_COMPOSITOR_CONTEXT_H_
