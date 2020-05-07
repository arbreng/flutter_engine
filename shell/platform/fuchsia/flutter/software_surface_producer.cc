// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "software_surface_producer.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include <memory>
#include <string>
#include <vector>

#include "flutter/fml/trace_event.h"
#include "third_party/skia/include/gpu/GrBackendSemaphore.h"
#include "third_party/skia/include/gpu/GrBackendSurface.h"
#include "third_party/skia/include/gpu/GrContext.h"

namespace flutter_runner {

namespace {

constexpr int kGrCacheMaxCount = 8192;
// Tuning advice:
// If you see the following 3 things happening simultaneously in a trace:
//   * Over budget ("flutter", "GPURasterizer::Draw") durations
//   * Many ("skia", "GrGpu::createTexture") events within the
//     "GPURasterizer::Draw"s
//   * The Skia GPU resource cache is full, as indicated by the
//     "SkiaCacheBytes" field in the ("flutter", "SurfacePool") trace counter
//     (compare it to the bytes value here)
// then you should consider increasing the size of the GPU resource cache.
constexpr size_t kGrCacheMaxByteSize = 1024 * 600 * 12 * 4;

}  // namespace

SoftwareSurfaceProducer::SoftwareSurfaceProducer(
    scenic::Session* scenic_session) = default;

SoftwareSurfaceProducer::~SoftwareSurfaceProducer() = default;

bool SoftwareSurfaceProducer::Initialize(scenic::Session* scenic_session) {
  surface_pool_ =
      std::make_unique<SoftwareSurfacePool>(*this, context_, scenic_session);

  return true;
}

void SoftwareSurfaceProducer::OnSurfacesPresented(
    std::vector<
        std::unique_ptr<flutter::SceneUpdateContext::SurfaceProducerSurface>>
        surfaces) {
  TRACE_EVENT0("flutter", "SoftwareSurfaceProducer::OnSurfacesPresented");

  // Do a single flush for all canvases derived from the context.
  {
    TRACE_EVENT0("flutter", "GrContext::flushAndSignalSemaphores");
    context_->flush();
  }

  if (!TransitionSurfacesToExternal(surfaces))
    FML_LOG(ERROR) << "TransitionSurfacesToExternal failed";

  // Submit surface
  for (auto& surface : surfaces) {
    SubmitSurface(std::move(surface));
  }

  // Buffer management.
  surface_pool_->AgeAndCollectOldBuffers();

  // If no further surface production has taken place for 10 frames (TODO:
  // Don't hardcode refresh rate here), then shrink our surface pool to fit.
  constexpr auto kShouldShrinkThreshold = zx::msec(10 * 16.67);
  async::PostDelayedTask(
      async_get_default_dispatcher(),
      [self = weak_factory_.GetWeakPtr(), kShouldShrinkThreshold] {
        if (!self) {
          return;
        }
        auto time_since_last_produce =
            async::Now(async_get_default_dispatcher()) -
            self->last_produce_time_;
        if (time_since_last_produce >= kShouldShrinkThreshold) {
          self->surface_pool_->ShrinkToFit();
        }
      },
      kShouldShrinkThreshold);
}

bool SoftwareSurfaceProducer::TransitionSurfacesToExternal(
    const std::vector<
        std::unique_ptr<flutter::SceneUpdateContext::SurfaceProducerSurface>>&
        surfaces) {
  for (auto& surface : surfaces) {
    auto vk_surface = static_cast<SoftwareSurface*>(surface.get());

    vulkan::VulkanCommandBuffer* command_buffer =
        vk_surface->GetCommandBuffer(logical_device_->GetCommandPool());
    if (!command_buffer->Begin())
      return false;

    GrBackendRenderTarget backendRT =
        vk_surface->GetSkiaSurface()->getBackendRenderTarget(
            SkSurface::kFlushRead_BackendHandleAccess);
    if (!backendRT.isValid()) {
      return false;
    }
    GrVkImageInfo imageInfo;
    if (!backendRT.getVkImageInfo(&imageInfo)) {
      return false;
    }

    VkImageMemoryBarrier image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = imageInfo.fImageLayout,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = 0,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL_KHR,
        .image = vk_surface->GetVkImage(),
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

    if (!command_buffer->InsertPipelineBarrier(
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,           // dependencyFlags
            0, nullptr,  // memory barriers
            0, nullptr,  // buffer barriers
            1, &image_barrier))
      return false;

    backendRT.setVkImageLayout(image_barrier.newLayout);

    if (!command_buffer->End())
      return false;

    if (!logical_device_->QueueSubmit(
            {}, {}, {vk_surface->GetAcquireVkSemaphore()},
            {command_buffer->Handle()}, vk_surface->GetCommandBufferFence()))
      return false;
  }
  return true;
}

std::unique_ptr<flutter::SceneUpdateContext::SurfaceProducerSurface>
SoftwareSurfaceProducer::ProduceSurface(
    const SkISize& size,
    const flutter::LayerRasterCacheKey& layer_key,
    std::unique_ptr<scenic::EntityNode> entity_node) {
  FML_DCHECK(valid_);
  last_produce_time_ = async::Now(async_get_default_dispatcher());
  auto surface = surface_pool_->AcquireSurface(size);
  surface->SetRetainedInfo(layer_key, std::move(entity_node));
  return surface;
}

void SoftwareSurfaceProducer::SubmitSurface(
    std::unique_ptr<flutter::SceneUpdateContext::SurfaceProducerSurface>
        surface) {
  FML_DCHECK(valid_ && surface != nullptr);
  surface_pool_->SubmitSurface(std::move(surface));
}

}  // namespace flutter_runner
