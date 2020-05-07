// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_SOFTWARE_SURFACE_PRODUCER_H_
#define FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_SOFTWARE_SURFACE_PRODUCER_H_

#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/syslog/global.h>

#include "flutter/flow/scene_update_context.h"
#include "flutter/fml/macros.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"
#include "logging.h"

namespace flutter_runner {

class SoftwareSurfaceProducer final
    : public flutter::SceneUpdateContext::SurfaceProducer {
 public:
  SoftwareSurfaceProducer(scenic::Session* scenic_session);
  ~SoftwareSurfaceProducer() override;

  bool IsValid() const { return valid_; }

  // |flutter::SceneUpdateContext::SurfaceProducer|
  std::unique_ptr<flutter::SceneUpdateContext::SurfaceProducerSurface>
  ProduceSurface(const SkISize& size,
                 const flutter::LayerRasterCacheKey& layer_key,
                 std::unique_ptr<scenic::EntityNode> entity_node) override;

  // |flutter::SceneUpdateContext::SurfaceProducer|
  void SubmitSurface(
      std::unique_ptr<flutter::SceneUpdateContext::SurfaceProducerSurface>
          surface) override;

  // |flutter::SceneUpdateContext::HasRetainedNode|
  bool HasRetainedNode(const flutter::LayerRasterCacheKey& key) const override {
    return surface_pool_->HasRetainedNode(key);
  }

  // |flutter::SceneUpdateContext::GetRetainedNode|
  scenic::EntityNode* GetRetainedNode(
      const flutter::LayerRasterCacheKey& key) override {
    return surface_pool_->GetRetainedNode(key);
  }

  void OnSurfacesPresented(
      std::vector<
          std::unique_ptr<flutter::SceneUpdateContext::SurfaceProducerSurface>>
          surfaces);

  GrContext* gr_context() { return context_.get(); }

 private:
  bool TransitionSurfacesToExternal(
      const std::vector<
          std::unique_ptr<flutter::SceneUpdateContext::SurfaceProducerSurface>>&
          surfaces);

  std::unique_ptr<VulkanSurfacePool> surface_pool_;
  bool valid_ = false;

  // Keep track of the last time we produced a surface.  This is used to
  // determine whether it is safe to shrink |surface_pool_| or not.
  zx::time last_produce_time_ = async::Now(async_get_default_dispatcher());
  fml::WeakPtrFactory<SoftwareSurfaceProducer> weak_factory_{this};

  // Disallow copy and assignment.
  SoftwareSurfaceProducer(const SoftwareSurfaceProducer&) = delete;
  SoftwareSurfaceProducer& operator=(const SoftwareSurfaceProducer&) = delete;
};

}  // namespace flutter_runner

#endif  // FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_SOFTWARE_SURFACE_PRODUCER_H_