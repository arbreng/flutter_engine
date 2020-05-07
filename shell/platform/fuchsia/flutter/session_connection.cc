// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/fuchsia/flutter/session_connection.h"

#include <lib/ui/scenic/cpp/commands.h>

#include "flutter/fml/make_copyable.h"
#include "flutter/shell/platform/fuchsia/flutter/software_surface_producer.h"
#include "flutter/shell/platform/fuchsia/flutter/vsync_recorder.h"
#include "flutter/shell/platform/fuchsia/flutter/vsync_waiter.h"
#include "flutter/shell/platform/fuchsia/flutter/vulkan_surface_producer.h"

namespace flutter_runner {

SessionConnection::SessionConnection(
    std::string debug_label,
    fuchsia::ui::views::ViewToken view_token,
    scenic::ViewRefPair view_ref_pair,
    fidl::InterfaceHandle<fuchsia::ui::scenic::Session> session,
    fml::closure session_error_callback,
    OnFramePresented on_frame_presented_callback,
    zx_handle_t vsync_event_handle,
    bool software)
    : session_wrapper_(session.Bind(), nullptr),
      root_view_(&session_wrapper_,
                 std::move(view_token),
                 std::move(view_ref_pair.control_ref),
                 std::move(view_ref_pair.view_ref),
                 debug_label),
      root_node_(&session_wrapper_),
      surface_producer_(
          software
              ? std::make_unique<SoftwareSurfaceProducer>(&session_wrapper_)
              : std::make_unique<VulkanSurfaceProducer>(&session_wrapper_)),
      scene_update_context_(&session_wrapper_, surface_producer_.get()),
      on_frame_presented_callback_(std::move(on_frame_presented_callback)),
      vsync_event_handle_(vsync_event_handle),
      software_(software) {
  session_wrapper_.set_error_handler(
      [callback = session_error_callback](zx_status_t status) { callback(); });

  // Set the |fuchsia::ui::scenic::OnFramePresented()| event handler that will
  // fire every time a set of one or more frames is presented.
  session_wrapper_.set_on_frame_presented_handler(
      [this, handle = vsync_event_handle_](
          fuchsia::scenic::scheduling::FramePresentedInfo info) {
        // Update Scenic's limit for our remaining frames in flight allowed.
        size_t num_presents_handled = info.presentation_infos.size();
        frames_in_flight_allowed_ = info.num_presents_allowed;

        // A frame was presented: Update our |frames_in_flight| to match the
        // updated unfinalized present requests.
        frames_in_flight_ -= num_presents_handled;
        FML_DCHECK(frames_in_flight_ >= 0);

        VsyncRecorder::GetInstance().UpdateFramePresentedInfo(
            zx::time(info.actual_presentation_time));

        // Call the client-provided callback once we are done using |info|.
        on_frame_presented_callback_(std::move(info));

        if (present_session_pending_) {
          PresentSession();
        }
        ToggleSignal(handle, true);
      }  // callback
  );

  session_wrapper_.SetDebugName(debug_label);

  root_view_.AddChild(root_node_);
  root_node_.SetEventMask(fuchsia::ui::gfx::kMetricsEventMask |
                          fuchsia::ui::gfx::kSizeChangeHintEventMask);

  // Get information to finish initialization and only then allow Present()s.
  session_wrapper_.RequestPresentationTimes(
      /*requested_prediction_span=*/0,
      [this](fuchsia::scenic::scheduling::FuturePresentationTimes info) {
        frames_in_flight_allowed_ = info.remaining_presents_in_flight_allowed;

        // If Scenic alloted us 0 frames to begin with, we should fail here.
        FML_CHECK(frames_in_flight_allowed_ > 0);

        VsyncRecorder::GetInstance().UpdateNextPresentationInfo(
            std::move(info));

        // Signal is initially high indicating availability of the session.
        ToggleSignal(vsync_event_handle_, true);
        initialized_ = true;

        PresentSession();
      });
}

SessionConnection::~SessionConnection() = default;

void SessionConnection::Present() {
  TRACE_EVENT0("gfx", "SessionConnection::Present");

  TRACE_FLOW_BEGIN("gfx", "SessionConnection::PresentSession",
                   next_present_session_trace_id_);
  next_present_session_trace_id_++;

  // Throttle frame submission to Scenic if we already have the maximum amount
  // of frames in flight. This allows the paint tasks for this frame to execute
  // in parallel with the presentation of previous frame but still provides
  // back-pressure to prevent us from enqueuing even more work.
  if (initialized_ && frames_in_flight_ < kMaxFramesInFlight) {
    PresentSession();
  } else {
    // We should never exceed the max frames in flight.
    FML_CHECK(frames_in_flight_ <= kMaxFramesInFlight);

    present_session_pending_ = true;
    ToggleSignal(vsync_event_handle_, false);
  }
}

void SessionConnection::set_enable_wireframe(bool enable) {
  session_wrapper_.Enqueue(
      scenic::NewSetEnableDebugViewBoundsCmd(root_view_.id(), enable));
}

void SessionConnection::EnqueueClearOps() {
  // We are going to be sending down a fresh node hierarchy every frame. So just
  // enqueue a detach op on the imported root node.
  session_wrapper_.Enqueue(scenic::NewDetachChildrenCmd(root_node_.id()));
}

void SessionConnection::PresentSession() {
  TRACE_EVENT0("gfx", "SessionConnection::PresentSession");

  // If we cannot call Present2() because we have no more Scenic frame budget,
  // then we must wait until the OnFramePresented() event fires so we can
  // continue our work.
  if (frames_in_flight_allowed_ == 0) {
    FML_CHECK(!initialized_ || present_session_pending_);
    return;
  }

  present_session_pending_ = false;

  while (processed_present_session_trace_id_ < next_present_session_trace_id_) {
    TRACE_FLOW_END("gfx", "SessionConnection::PresentSession",
                   processed_present_session_trace_id_);
    processed_present_session_trace_id_++;
  }
  TRACE_FLOW_BEGIN("gfx", "Session::Present", next_present_trace_id_);
  next_present_trace_id_++;

  ++frames_in_flight_;

  // Flush all session ops. Paint tasks may not yet have executed but those are
  // fenced. The compositor can start processing ops while we finalize paint
  // tasks.
  session_wrapper_.Present2(
      /*requested_presentation_time=*/0,
      /*requested_prediction_span=*/0,
      [this](fuchsia::scenic::scheduling::FuturePresentationTimes info) {
        frames_in_flight_allowed_ = info.remaining_presents_in_flight_allowed;
        VsyncRecorder::GetInstance().UpdateNextPresentationInfo(
            std::move(info));
      });

  // Prepare for the next frame. These ops won't be processed till the next
  // present.
  EnqueueClearOps();
}

void SessionConnection::ToggleSignal(zx_handle_t handle, bool set) {
  const auto signal = VsyncWaiter::SessionPresentSignal;
  auto status = zx_object_signal(handle,            // handle
                                 set ? 0 : signal,  // clear mask
                                 set ? signal : 0   // set mask
  );
  if (status != ZX_OK) {
    FML_LOG(ERROR) << "Could not toggle vsync signal: " << set;
  }
}

}  // namespace flutter_runner
