// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/fuchsia/flutter/compositor_context.h"

#include "flutter/flow/layers/layer_tree.h"
#include "flutter/shell/platform/fuchsia/flutter/vulkan_surface_producer.h"

namespace flutter_runner {

class ScopedFrame final : public flutter::CompositorContext::ScopedFrame {
 public:
  ScopedFrame(flutter::CompositorContext& context,
              GrContext* gr_context,
              SkCanvas* canvas,
              flutter::ExternalViewEmbedder* view_embedder,
              const SkMatrix& root_surface_transformation,
              bool instrumentation_enabled,
              bool surface_supports_readback,
              fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger,
              SessionConnection& session_connection,
              flutter::SceneUpdateContext& scene_update_context)
      : flutter::CompositorContext::ScopedFrame(context,
                                                gr_context,
                                                canvas,
                                                view_embedder,
                                                root_surface_transformation,
                                                instrumentation_enabled,
                                                surface_supports_readback,
                                                raster_thread_merger),
        session_connection_(session_connection),
        scene_update_context_(scene_update_context) {}

 private:
  SessionConnection& session_connection_;
  flutter::SceneUpdateContext& scene_update_context_;

  flutter::RasterStatus Raster(flutter::LayerTree& layer_tree,
                               bool ignore_raster_cache) override {
    if (!scene_update_context_.has_metrics()) {
      return flutter::RasterStatus::kSuccess;
    }

    {
      TRACE_EVENT0("flutter", "Preroll");

      // Preroll the Flutter layer tree. This allows Flutter to perform
      // pre-paint optimizations.
      layer_tree.Preroll(*this, ignore_raster_cache);
    }

    {
      TRACE_EVENT0("flutter", "UpdateScene");

      // Prepare for the current frame by discarding all of the Scenic resources
      // used in the previous frame.
      scene_update_context_.EnqueueClearOps();

      // Traverse the Flutter layer tree so that the necessary session ops to
      // represent the frame are enqueued in the underlying session.
      //
      // `ExecutePaintTasks`, below, handles `Paint` traversal.
      layer_tree.UpdateScene(scene_update_context_);
    }

    {
      TRACE_EVENT0("flutter", "SessionPresent");

      // Flush all pending session ops before painting the layers.  Scenic will
      // wait internally on its acquire fences before actually using the layer
      // contents.  This allows us to parallelize the layer-painting work with
      // Scenic's work.
      session_connection_.Present();

      // Execute all of the deferred `PaintTask`s that the `UpdateScenes` pass
      // queued up.  The method will siganl Scenic's acquire fences once the
      // painting is complete.
      scene_update_context_.ExecutePaintTasks(
          context().raster_time(), context().ui_time(),
          context().texture_registry(), &context().raster_cache(),
          gr_context());
    }

    return flutter::RasterStatus::kSuccess;
  }

  FML_DISALLOW_COPY_AND_ASSIGN(ScopedFrame);
};

CompositorContext::CompositorContext(
    std::string debug_label,
    fuchsia::ui::views::ViewToken view_token,
    scenic::ViewRefPair view_ref_pair,
    fuchsia::ui::scenic::SessionPtr session,
    SessionConnection::SessionErrorCallback session_error_callback,
    zx_handle_t vsync_event_handle)
    : session_connection_(
          debug_label,
          std::move(session),
          std::move(session_error_callback),
          [](auto) {},
          vsync_event_handle),
      scene_update_context_(std::move(debug_label),
                            std::move(view_token),
                            std::move(view_ref_pair),
                            std::make_unique<VulkanSurfaceProducer>(
                                session_connection_.session()),
                            session_connection_.session()) {}

CompositorContext::~CompositorContext() = default;

void CompositorContext::OnSessionMetricsChanged(
    const fuchsia::ui::gfx::Metrics& metrics) {
  scene_update_context_.set_metrics(metrics);
}

void CompositorContext::OnDebugViewBoundsEnabled(bool enabled) {
  scene_update_context_.SetDebugViewBoundsEnabled(enabled);
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
  return std::make_unique<flutter_runner::ScopedFrame>(
      *this, gr_context, canvas, view_embedder, root_surface_transformation,
      instrumentation_enabled, surface_supports_readback, raster_thread_merger,
      session_connection_, scene_update_context_);
}

}  // namespace flutter_runner
