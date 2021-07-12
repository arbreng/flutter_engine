// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_session.h"

#include <iterator>  // For make_move_iterator

#include "flutter/fml/logging.h"

namespace flutter_runner::testing {

FakeSession::FakeSession(async::TestLoop& loop)
    : loop_(loop), session_subloop_(loop_.StartNewLoop()), binding_(this) {}

FakeSession::SessionAndListenerClientPair FakeSession::Bind() {
  FML_CHECK(!listener_.is_bound());
  FML_CHECK(!binding_.is_bound());

  fidl::InterfaceHandle<fuchsia::ui::scenic::Session> session;
  auto listener_request = listener_.NewRequest(session_subloop_->dispatcher());
  binding_.Bind(session.NewRequest(), session_subloop_->dispatcher());

  return std::make_pair(std::move(session), std::move(listener_request));
}

void FakeSession::SetPresentHandler(PresentHandler present_handler) {
  present_handler_ = std::move(present_handler);
}

void FakeSession::SetPresent2Handler(Present2Handler present2_handler) {
  present2_handler_ = std::move(present2_handler);
}

void FakeSession::SetRequestPresentationTimesHandler(
    RequestPresentationTimesHandler request_presentation_times_handler) {
  request_presentation_times_handler_ =
      std::move(request_presentation_times_handler);
}

void FakeSession::FireOnFramePresentedEvent(
    fuchsia::scenic::scheduling::FramePresentedInfo frame_presented_info) {
  FML_CHECK(is_bound());

  binding_.events().OnFramePresented(std::move(frame_presented_info));
}

void FakeSession::DisconnectSession() {
  // Unbind the channels and drop them on the floor, simulating Scenic behavior.
  binding_.Unbind();
  listener_.Unbind();
}

void FakeSession::Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) {
  FML_CHECK(is_bound());

  // Append `cmds` to the end of the command queue, preferring to move elements
  // when possible.
  command_queue_.insert(command_queue_.end(),
                        std::make_move_iterator(cmds.begin()),
                        std::make_move_iterator(cmds.end()));
}

void FakeSession::Present(uint64_t presentation_time,
                          std::vector<zx::event> acquire_fences,
                          std::vector<zx::event> release_fences,
                          PresentCallback callback) {
  FML_CHECK(is_bound());

  if (!present_handler_) {
    return NotImplemented_("Present");
  }

  command_queue_.clear();  // TODO Process commands

  auto present_info = present_handler_(
      presentation_time, std::move(acquire_fences), std::move(release_fences));
  if (callback) {
    callback(std::move(present_info));
  }
}

void FakeSession::Present2(fuchsia::ui::scenic::Present2Args args,
                           Present2Callback callback) {
  FML_CHECK(is_bound());

  if (!present2_handler_) {
    return NotImplemented_("Present2");
  }

  command_queue_.clear();  // TODO Process commands

  auto future_presentation_times = present2_handler_(std::move(args));
  if (callback) {
    callback(std::move(future_presentation_times));
  }
}

void FakeSession::RequestPresentationTimes(
    int64_t requested_prediction_span,
    RequestPresentationTimesCallback callback) {
  FML_CHECK(is_bound());

  if (!request_presentation_times_handler_) {
    return NotImplemented_("RequestPresentationTimes");
  }

  auto future_presentation_times =
      request_presentation_times_handler_(requested_prediction_span);
  if (callback) {
    callback(std::move(future_presentation_times));
  }
}

void FakeSession::RegisterBufferCollection(
    uint32_t buffer_id,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  FML_CHECK(is_bound());

  NotImplemented_("RegisterBufferCollection");  // TODO
}

void FakeSession::DeregisterBufferCollection(uint32_t buffer_id) {
  FML_CHECK(is_bound());

  NotImplemented_("DeregisterBufferCollection");  // TODO
}

void FakeSession::SetDebugName(std::string debug_name) {
  FML_CHECK(is_bound());

  debug_name_ = std::move(debug_name);
}

void FakeSession::NotImplemented_(const std::string& name) {
  FML_LOG(FATAL) << "FakeSession does not implement " << name;
}

}  // namespace flutter_runner::testing
