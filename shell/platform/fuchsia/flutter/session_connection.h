// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_FUCHSIA_SESSION_CONNECTION_H_
#define FLUTTER_SHELL_PLATFORM_FUCHSIA_SESSION_CONNECTION_H_

#include <fuchsia/scenic/scheduling/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/ui/scenic/cpp/session.h>
#include <zircon/types.h>

#include "flutter/fml/closure.h"
#include "flutter/fml/macros.h"
#include "flutter/fml/trace_event.h"

namespace flutter_runner {

// This component is responsible for maintaining the Scenic session connection
// and synchronizing any session updates with the Vsync interval..
class SessionConnection {
 public:
  using SessionErrorCallback = std::function<void(zx_status_t)>;
  using OnFramePresentedCallback =
      std::function<void(fuchsia::scenic::scheduling::FramePresentedInfo)>;

  SessionConnection(std::string debug_label,
                    fuchsia::ui::scenic::SessionPtr session,
                    SessionErrorCallback session_error_callback,
                    OnFramePresentedCallback on_frame_presented_callback,
                    zx_handle_t vsync_event_handle);
  ~SessionConnection();

  scenic::Session* session() { return &session_; }
  void Present();

 private:
  void PresentSession();
  void ToggleSignal(bool raise);

  scenic::Session session_;

  zx_handle_t vsync_event_handle_;

  // A flow event trace id for following |Session::Present| calls into
  // Scenic.  This will be incremented each |Session::Present| call.  By
  // convention, the Scenic side will also contain its own trace id that
  // begins at 0, and is incremented each |Session::Present| call.
  uint64_t next_present_trace_id_ = 0;
  uint64_t next_present_session_trace_id_ = 0;
  uint64_t processed_present_session_trace_id_ = 0;

  // The maximum number of frames Flutter sent to Scenic that it can have
  // outstanding at any time. This is equivalent to how many times it has
  // called Present2() before receiving an OnFramePresented() event.
  static constexpr int kMaxFramesInFlight = 3;
  int frames_in_flight_ = 0;
  int frames_in_flight_allowed_ = 0;

  bool initialized_ = false;
  bool present_session_pending_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(SessionConnection);
};

}  // namespace flutter_runner

#endif  // FLUTTER_SHELL_PLATFORM_FUCHSIA_SESSION_CONNECTION_H_
