// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_FLOW_SCENE_UPDATE_CONTEXT_H_
#define FLUTTER_FLOW_SCENE_UPDATE_CONTEXT_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "flutter/flow/instrumentation.h"
#include "flutter/flow/raster_cache.h"
#include "flutter/flow/texture.h"
#include "flutter/fml/macros.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkScalar.h"
#include "third_party/skia/include/core/SkSize.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/gpu/GrContext.h"

namespace flutter {

class Layer;

// Scenic currently lacks an API to enable rendering of alpha channel; this only
// happens if there is a OpacityNode higher in the tree with opacity != 1, or a
// PaintTask creates a texture with transparent pixels (alpha != 1). For now,
// clamp to a infinitesimally smaller value than 1, which does not cause visual
// problems in practice.
constexpr float kOneMinusEpsilon = 1 - FLT_EPSILON;

// How much layers are separated in Scenic z elevation.
constexpr float kScenicZElevationBetweenLayers = 10.f;

class SceneUpdateContext {
 public:
  class SurfaceProducerSurface {
   public:
    virtual ~SurfaceProducerSurface() = default;

    virtual size_t AdvanceAndGetAge() = 0;

    virtual bool FlushSessionAcquireAndReleaseEvents() = 0;

    virtual bool IsValid() const = 0;

    virtual SkISize GetSize() const = 0;

    virtual void SignalWritesFinished(
        const std::function<void(void)>& on_writes_committed) = 0;

    virtual scenic::Image* GetImage() = 0;

    virtual sk_sp<SkSurface> GetSkiaSurface() const = 0;
  };

  class SurfaceProducer {
   public:
    virtual ~SurfaceProducer() = default;

    // Query a retained entity node (owned by a retained surface) for retained
    // rendering.
    virtual bool HasRetainedNode(const LayerRasterCacheKey& key) const = 0;
    virtual scenic::EntityNode* GetRetainedNode(
        const LayerRasterCacheKey& key) = 0;

    // The produced surface owns the entity_node and has a layer_key for
    // retained rendering. The surface will only be retained if the layer_key
    // has a non-null layer pointer (layer_key.id()).
    virtual std::unique_ptr<SurfaceProducerSurface> ProduceSurface(
        const SkISize& size,
        const LayerRasterCacheKey& layer_key,
        std::unique_ptr<scenic::EntityNode> entity_node) = 0;

    virtual void SubmitSurface(
        std::unique_ptr<SurfaceProducerSurface> surface) = 0;

    virtual void OnSurfacesPresented(
        std::vector<std::unique_ptr<
            flutter::SceneUpdateContext::SurfaceProducerSurface>> surfaces) = 0;
  };

  class Entity {
   public:
    Entity(SceneUpdateContext& context);
    virtual ~Entity();

    SceneUpdateContext& context() { return context_; }
    scenic::EntityNode& entity_node() { return entity_node_; }
    virtual scenic::ContainerNode& embedder_node() { return entity_node_; }

   private:
    SceneUpdateContext& context_;
    Entity* const previous_entity_;

    scenic::EntityNode entity_node_;
  };

  class Transform : public Entity {
   public:
    Transform(SceneUpdateContext& context, const SkMatrix& transform);
    Transform(SceneUpdateContext& context,
              float scale_x,
              float scale_y,
              float scale_z);
    virtual ~Transform();

   private:
    float const previous_scale_x_;
    float const previous_scale_y_;
  };

  class Frame : public Entity {
   public:
    // When layer is not nullptr, the frame is associated with a layer subtree
    // rooted with that layer. The frame may then create a surface that will be
    // retained for that layer.
    Frame(SceneUpdateContext& context,
          const SkRRect& rrect,
          SkColor color,
          SkAlpha opacity,
          std::string label,
          float z_translation = 0.0f,
          Layer* layer = nullptr);
    virtual ~Frame();

    scenic::ContainerNode& embedder_node() override { return opacity_node_; }

    void AddPaintLayer(Layer* layer);

   private:
    const SkRRect rrect_;
    SkColor const color_;
    SkAlpha const opacity_;

    scenic::OpacityNodeHACK opacity_node_;
    std::vector<Layer*> paint_layers_;
    SkRect paint_bounds_;
    Layer* layer_;
  };

  class Clip : public Entity {
   public:
    Clip(SceneUpdateContext& context, const SkRect& shape_bounds);
    ~Clip() = default;
  };

  SceneUpdateContext(std::string debug_label,
                     fuchsia::ui::views::ViewToken view_token,
                     scenic::ViewRefPair view_ref_pair,
                     std::unique_ptr<SurfaceProducer> surface_producer,
                     scenic::Session* session);
  ~SceneUpdateContext();

  scenic::Session* session() { return session_; }
  scenic::ContainerNode& root_node() { return root_node_; }
  Entity* top_entity() { return top_entity_; }

  SurfaceProducer* surface_producer() { return surface_producer_.get(); }

  bool has_metrics() const {
    return metrics_.scale_x != 0.0f && metrics_.scale_y != 0.0f &&
           metrics_.scale_z != 0.0f;
  }
  void set_metrics(const fuchsia::ui::gfx::Metrics& metrics) {
    metrics_ = metrics;
  }
  const fuchsia::ui::gfx::Metrics& metrics() const { return metrics_; }

  void set_dimensions(const SkISize& frame_physical_size,
                      float frame_physical_depth,
                      float frame_device_pixel_ratio) {
    frame_physical_size_ = frame_physical_size;
    frame_physical_depth_ = frame_physical_depth;
    frame_device_pixel_ratio_ = frame_device_pixel_ratio;
  }
  const SkISize& frame_size() const { return frame_physical_size_; }
  float frame_physical_depth() const { return frame_physical_depth_; }
  float frame_device_pixel_ratio() const { return frame_device_pixel_ratio_; }

  // The transformation matrix of the current context. It's used to construct
  // the LayerRasterCacheKey for a given layer.
  SkMatrix Matrix() const {
    return SkMatrix::MakeScale(metrics_.scale_x * top_scale_x_,
                               metrics_.scale_y * top_scale_y_);
  }

  // The opacity value of the current context.  It's based on all the parent
  // OpacityLayers at a given point in the traversal.
  void set_alphaf(float alpha) { alpha_ = alpha; }
  float alphaf() { return alpha_; }

  // The global scenic elevation at a given point in the traversal.
  float scenic_elevation() { return scenic_elevation_; }

  void set_scenic_elevation(float elevation) { scenic_elevation_ = elevation; }

  float GetGlobalElevationForNextScenicLayer() {
    float elevation = topmost_global_scenic_elevation_;
    topmost_global_scenic_elevation_ += kScenicZElevationBetweenLayers;
    return elevation;
  }

  bool HasRetainedNode(const LayerRasterCacheKey& key) const {
    return surface_producer_->HasRetainedNode(key);
  }
  scenic::EntityNode* GetRetainedNode(const LayerRasterCacheKey& key) {
    return surface_producer_->GetRetainedNode(key);
  }

  void SetDebugViewBoundsEnabled(bool enable);
  void EnqueueClearOps();

  void ExecutePaintTasks(const Stopwatch& raster_time,
                         const Stopwatch& ui_time,
                         TextureRegistry& texture_registry,
                         const RasterCache* raster_cache,
                         GrContext* gr_context);

 private:
  struct PaintTask {
    std::unique_ptr<SurfaceProducerSurface> surface;
    SkScalar left;
    SkScalar top;
    SkScalar scale_x;
    SkScalar scale_y;
    SkColor background_color;
    std::vector<Layer*> layers;
  };

  // Setup the entity_node as a frame that materialize all the paint_layers. In
  // most cases, this creates a VulkanSurface (SurfaceProducerSurface) by
  // calling SetShapeTextureOrColor and GenerageImageIfNeeded. Such surface will
  // own the associated entity_node. If the layer pointer isn't nullptr, the
  // surface (and thus the entity_node) will be retained for that layer to
  // improve the performance.
  void CreateFrame(scenic::EntityNode entity_node,
                   const SkRRect& rrect,
                   SkColor color,
                   SkAlpha opacity,
                   const SkRect& paint_bounds,
                   std::vector<Layer*> paint_layers,
                   Layer* layer);
  void SetMaterialTextureAndColor(scenic::Material& material,
                                  SkColor color,
                                  SkAlpha opacity,
                                  const SkRect& paint_bounds,
                                  std::vector<Layer*> paint_layers,
                                  Layer* layer,
                                  scenic::EntityNode entity_node);
  void SetMaterialColor(scenic::Material& material,
                        SkColor color,
                        SkAlpha opacity);
  scenic::Image* GenerateImageIfNeeded(SkColor color,
                                       const SkRect& paint_bounds,
                                       std::vector<Layer*> paint_layers,
                                       Layer* layer,
                                       scenic::EntityNode entity_node);

  scenic::Session* const session_;
  scenic::View root_view_;
  scenic::EntityNode root_node_;

  std::unique_ptr<SurfaceProducer> surface_producer_;
  std::vector<PaintTask> paint_tasks_;

  fuchsia::ui::gfx::Metrics metrics_ = {0.0f, 0.0f, 0.0f};
  SkISize frame_physical_size_ = {0, 0};
  float frame_physical_depth_ = 0.0f;
  float frame_device_pixel_ratio_ =
      1.0f;  // Ratio between logical and physical pixels.

  Entity* top_entity_ = nullptr;
  float top_scale_x_ = 1.f;
  float top_scale_y_ = 1.f;

  float alpha_ = 1.0f;
  float scenic_elevation_ = 0.f;
  float topmost_global_scenic_elevation_ = kScenicZElevationBetweenLayers;

  FML_DISALLOW_COPY_AND_ASSIGN(SceneUpdateContext);
};

}  // namespace flutter

#endif  // FLUTTER_FLOW_SCENE_UPDATE_CONTEXT_H_
