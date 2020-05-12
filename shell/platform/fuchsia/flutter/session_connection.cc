// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/fuchsia/flutter/session_connection.h"

#include "flutter/shell/platform/fuchsia/flutter/vsync_recorder.h"
#include "flutter/shell/platform/fuchsia/flutter/vsync_waiter.h"

namespace flutter_runner {

SessionConnection::SessionConnection(
    std::string debug_label,
    fuchsia::ui::scenic::SessionPtr session,
    SessionErrorCallback session_error_callback,
    OnFramePresentedCallback on_frame_presented_callback,
    zx_handle_t vsync_event_handle)
    : session_(std::move(session)), vsync_event_handle_(vsync_event_handle) {
  session_.SetDebugName(debug_label);
  session_.set_error_handler(std::move(session_error_callback));

  // Set the |fuchsia::ui::scenic::OnFramePresented()| event handler that will
  // fire every time a set of one or more frames is presented.
  session_.set_on_frame_presented_handler(
      [this,
       on_frame_presented_callback = std::move(on_frame_presented_callback)](
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
        on_frame_presented_callback(std::move(info));

        if (present_session_pending_) {
          PresentSession();
        }
        ToggleSignal(true);
      }  // callback
  );

  // Get information to finish initialization and only then allow Present()s.
  session_.RequestPresentationTimes(
      /*requested_prediction_span=*/0,
      [this](fuchsia::scenic::scheduling::FuturePresentationTimes info) {
        frames_in_flight_allowed_ = info.remaining_presents_in_flight_allowed;

        // If Scenic alloted us 0 frames to begin with, we should fail here.
        FML_CHECK(frames_in_flight_allowed_ > 0);

        VsyncRecorder::GetInstance().UpdateNextPresentationInfo(
            std::move(info));

        // Signal is initially high indicating availability of the session.
        ToggleSignal(true);
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
  // of frames in flight. This allows the paint tasks for this frame to
  // execute in parallel with the presentation of previous frame but still
  // provides back-pressure to prevent us from enqueuing even more work.
  if (initialized_ && frames_in_flight_ < kMaxFramesInFlight) {
    PresentSession();
  } else {
    // We should never exceed the max frames in flight.
    FML_CHECK(frames_in_flight_ <= kMaxFramesInFlight);

    present_session_pending_ = true;
    ToggleSignal(false);
  }
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

  // Flush all session ops. Paint tasks may not yet have executed but those
  // are fenced. The compositor can start processing ops while we finalize
  // paint tasks.
  session_.Present2(
      /*requested_presentation_time=*/0,
      /*requested_prediction_span=*/0,
      [this](fuchsia::scenic::scheduling::FuturePresentationTimes info) {
        frames_in_flight_allowed_ = info.remaining_presents_in_flight_allowed;
        VsyncRecorder::GetInstance().UpdateNextPresentationInfo(
            std::move(info));
      });
}

void SessionConnection::ToggleSignal(bool set) {
  const auto signal = VsyncWaiter::SessionPresentSignal;
  auto status = zx_object_signal(vsync_event_handle_,  // handle
                                 set ? 0 : signal,     // clear mask
                                 set ? signal : 0      // set mask
  );
  if (status != ZX_OK) {
    FML_LOG(ERROR) << "Could not toggle vsync signal: " << set;
  }
}

}  // namespace flutter_runner
