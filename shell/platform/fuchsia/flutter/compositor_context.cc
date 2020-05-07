// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/fuchsia/flutter/compositor_context.h"

#include "flutter/flow/layers/layer_tree.h"

namespace flutter_runner {

class ScopedFrame final : public flutter::CompositorContext::ScopedFrame {
 public:
  ScopedFrame(GrContext* gr_context,
              SkCanvas* canvas,
              flutter::ExternalViewEmbedder* view_embedder,
              const SkMatrix& root_surface_transformation,
              bool instrumentation_enabled,
              bool surface_supports_readback,
              fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger,
              flutter::CompositorContext& context,
              SessionConnection& session_connection)
      : flutter::CompositorContext::ScopedFrame(
            context,
            gr_context,
            canvas,
            view_embedder,
            root_surface_transformation,
            instrumentation_enabled,
            surface_supports_readback,
            std::move(raster_thread_merger)),
        session_connection_(session_connection) {}

 private:
  SessionConnection& session_connection_;

  flutter::RasterStatus Raster(flutter::LayerTree& layer_tree,
                               bool ignore_raster_cache) override {
    if (!session_connection_.has_metrics()) {
      return flutter::RasterStatus::kSuccess;
    }

    {
      // Preroll the Flutter layer tree. This allows Flutter to perform
      // pre-paint optimizations.
      TRACE_EVENT0("flutter", "Preroll");
      layer_tree.Preroll(*this, ignore_raster_cache);
    }

    {
      // Traverse the Flutter layer tree so that the necessary session ops to
      // represent the frame are enqueued in the underlying session.
      TRACE_EVENT0("flutter", "UpdateScene");
      layer_tree.UpdateScene(session_connection_.scene_update_context(),
                             session_connection_.root_node());
    }

    {
      // Flush all pending session ops.
      TRACE_EVENT0("flutter", "SessionPresent");

      session_connection_.Present();

      // Execute paint tasks and signal fences.
      auto surfaces_to_submit =
          session_connection_.scene_update_context().ExecutePaintTasks(*frame);

      // Tell the surface producer that a present has occurred so it can perform
      // book-keeping on buffer caches.
      session_connection_.surface_producer()->OnSurfacesPresented(
          std::move(surfaces_to_submit));
    }

    return flutter::RasterStatus::kSuccess;
  }

  FML_DISALLOW_COPY_AND_ASSIGN(ScopedFrame);
};

CompositorContext::CompositorContext(
    std::shared_ptr<SessionConnection> session_connection)
    : session_connection_(std::move(session_connection)) {}

CompositorContext::~CompositorContext() = default;

void CompositorContext::OnSessionMetricsDidChange(
    const fuchsia::ui::gfx::Metrics& metrics) {
  session_connection_.set_metrics(metrics);
}

void CompositorContext::OnWireframeEnabled(bool enabled) {
  session_connection_.set_enable_wireframe(enabled);
}

std::unique_ptr<flutter::CompositorContext::ScopedFrame>
CompositorContext::AcquireFrame(
    GrContext* gr_context,
    SkCanvas* canvas,
    flutter::ExternalViewEmbedder* view_embedder,
    const SkMatrix& root_surface_transformation,
    bool instrumentation_enabled,
    bool surface_supports_readback,
    fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) {
  // TODO: The AcquireFrame interface is too broad and must be refactored to get
  // rid of the context and canvas arguments as those seem to be only used for
  // colorspace correctness purposes on the mobile shells.
  return std::make_unique<flutter_runner::ScopedFrame>(
      gr_context, canvas, view_embedder, root_surface_transformation,
      instrumentation_enabled, surface_supports_readback, raster_thread_merger,
      *this, session_connection_);
}

}  // namespace flutter_runner
