// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/fuchsia/flutter/default_session_connection.h"

#include <fuchsia/scenic/scheduling/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-testing/test_loop.h>
#include <lib/sys/cpp/component_context.h>

#include <functional>
#include <string>
#include <vector>

#include "fml/time/time_delta.h"
#include "fml/time/time_point.h"
#include "gtest/gtest.h"

#include "fakes/scenic/fake_session.h"

using fuchsia::scenic::scheduling::FramePresentedInfo;
using fuchsia::scenic::scheduling::FuturePresentationTimes;
using fuchsia::scenic::scheduling::PresentReceivedInfo;

namespace flutter_runner::testing {
namespace {

std::string GetCurrentTestName() {
  return ::testing::UnitTest::GetInstance()->current_test_info()->name();
}

FramePresentedInfo MakeFramePresentedInfoForOnePresent(
    int64_t latched_time,
    int64_t frame_presented_time) {
  std::vector<PresentReceivedInfo> present_infos;
  present_infos.emplace_back();
  present_infos.back().set_present_received_time(0);
  present_infos.back().set_latched_time(0);
  return FramePresentedInfo{
      .actual_presentation_time = 0,
      .presentation_infos = std::move(present_infos),
      .num_presents_allowed = 1,
  };
}

void AwaitVsyncChecked(DefaultSessionConnection& session_connection,
                       bool& condition_variable,
                       fml::TimeDelta expected_frame_start,
                       fml::TimeDelta expected_frame_end) {
  session_connection.AwaitVsync(
      [&condition_variable,
       expected_frame_start = std::move(expected_frame_start),
       expected_frame_end = std::move(expected_frame_end)](
          fml::TimePoint frame_start, fml::TimePoint frame_end) {
        EXPECT_EQ(frame_start.ToEpochDelta(), expected_frame_start);
        EXPECT_EQ(frame_end.ToEpochDelta(), expected_frame_end);
        condition_variable = true;
      });
}

};  // namespace

class SessionConnectionTest : public ::testing::Test,
                              public fuchsia::ui::scenic::SessionListener {
 protected:
  SessionConnectionTest() : session_listener_(this), fake_session_(loop_) {
    FakeSession::SessionAndListenerClientPair session_and_listener =
        fake_session().Bind();

    session_ = std::move(session_and_listener.first);
    session_listener_.Bind(std::move(session_and_listener.second));
  }
  ~SessionConnectionTest() override = default;

  async::TestLoop& loop() { return loop_; }
  FakeSession& fake_session() { return fake_session_; }

  void SetUpSessionStubs(
      FakeSession::RequestPresentationTimesHandler
          request_presentation_times_handler = nullptr,
      FakeSession::Present2Handler present_handler = nullptr) {
    auto non_null_request_presentation_times_handler =
        request_presentation_times_handler ? request_presentation_times_handler
                                           : [](auto...) -> auto {
      return FuturePresentationTimes{
          .future_presentations = {},
          .remaining_presents_in_flight_allowed = 1,
      };
    };
    fake_session().SetRequestPresentationTimesHandler(
        std::move(non_null_request_presentation_times_handler));

    auto non_null_present_handler =
        present_handler ? present_handler : [](auto...) -> auto {
      return FuturePresentationTimes{
          .future_presentations = {},
          .remaining_presents_in_flight_allowed = 1,
      };
    };
    fake_session().SetPresent2Handler(std::move(non_null_present_handler));
  }

  fidl::InterfaceHandle<fuchsia::ui::scenic::Session> TakeSessionHandle() {
    FML_CHECK(session_.is_valid());
    return std::move(session_);
  }

  GetNowCallback GetTestLoopNowCallback() {
    return [this]() -> fml::TimePoint {
      zx::time test_now = loop().Now();
      return fml::TimePoint::FromEpochDelta(
          fml::TimeDelta::FromNanoseconds(test_now.get()));
    };
  }

 private:
  // |fuchsia::ui::scenic::SessionListener|
  void OnScenicError(std::string error) override { FML_CHECK(false); }

  // |fuchsia::ui::scenic::SessionListener|
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override {
    FML_CHECK(false);
  }

  async::TestLoop loop_;  // Must come before FIDL bindings.

  fidl::InterfaceHandle<fuchsia::ui::scenic::Session> session_;
  fidl::Binding<fuchsia::ui::scenic::SessionListener> session_listener_;

  FakeSession fake_session_;
};

TEST_F(SessionConnectionTest, Initialization) {
  SetUpSessionStubs();  // So we don't CHECK

  // Create the SessionConnection but don't pump the loop.  No FIDL calls are
  // completed yet.
  const std::string debug_name = GetCurrentTestName();
  flutter_runner::DefaultSessionConnection session_connection(
      debug_name, TakeSessionHandle(), []() { FML_CHECK(false); },
      GetTestLoopNowCallback(), [](auto...) { FML_CHECK(false); }, 1,
      fml::TimeDelta::Zero());
  EXPECT_EQ(fake_session().debug_name(), "");
  EXPECT_TRUE(fake_session().command_queue().empty());

  // Simulate an AwaitVsync that comes immediately, before
  // `RequestPresentationTimes` returns.
  bool await_vsync_fired = false;
  AwaitVsyncChecked(session_connection, await_vsync_fired,
                    fml::TimeDelta::Zero(), kDefaultPresentationInterval);
  EXPECT_TRUE(await_vsync_fired);

  // Ensure the debug name is set.
  loop().RunUntilIdle();
  EXPECT_EQ(fake_session().debug_name(), debug_name);
  EXPECT_TRUE(fake_session().command_queue().empty());
}

TEST_F(SessionConnectionTest, SessionDisconnect) {
  SetUpSessionStubs();  // So we don't CHECK

  // Set up a callback which allows sensing of the session error state.
  bool session_error_fired = false;
  fml::closure on_session_error = [&session_error_fired]() {
    session_error_fired = true;
  };

  // Create the SessionConnection but don't pump the loop.  No FIDL calls are
  // completed yet.
  flutter_runner::DefaultSessionConnection session_connection(
      GetCurrentTestName(), TakeSessionHandle(), std::move(on_session_error),
      GetTestLoopNowCallback(), [](auto...) { FML_CHECK(false); }, 1,
      fml::TimeDelta::Zero());
  EXPECT_FALSE(session_error_fired);

  // Simulate a session disconnection, then Pump the loop.  The session error
  // callback will fire.
  fake_session().DisconnectSession();
  loop().RunUntilIdle();
  EXPECT_TRUE(session_error_fired);
}

TEST_F(SessionConnectionTest, BasicPresent) {
  // Set up callbacks which allow sensing of how many presents
  // (`RequestPresentationTimes` or `Present` calls) were handled.
  size_t request_times_called = 0u;
  size_t presents_called = 0u;
  SetUpSessionStubs(
      [&request_times_called](auto...) -> auto {
        request_times_called++;
        return FuturePresentationTimes{
            .future_presentations = {},
            .remaining_presents_in_flight_allowed = 1,
        };
      },
      [&presents_called](auto...) -> auto {
        presents_called++;
        return FuturePresentationTimes{
            .future_presentations = {},
            .remaining_presents_in_flight_allowed = 1,
        };
      });

  // Set up a callback which allows sensing of how many vsync's
  // (`OnFramePresented` events) were handled.
  size_t vsyncs_handled = 0u;
  on_frame_presented_event on_frame_presented = [&vsyncs_handled](auto...) {
    vsyncs_handled++;
  };

  // Create the SessionConnection but don't pump the loop.  No FIDL calls are
  // completed yet.
  flutter_runner::DefaultSessionConnection session_connection(
      GetCurrentTestName(), TakeSessionHandle(), []() { FML_CHECK(false); },
      GetTestLoopNowCallback(), std::move(on_frame_presented), 1,
      fml::TimeDelta::Zero());
  EXPECT_TRUE(fake_session().command_queue().empty());
  EXPECT_EQ(request_times_called, 0u);
  EXPECT_EQ(presents_called, 0u);
  EXPECT_EQ(vsyncs_handled, 0u);

  // Pump the loop; `RequestPresentationTimes`, `Present`, and both of their
  // callbacks are called.
  loop().RunUntilIdle();
  EXPECT_TRUE(fake_session().command_queue().empty());
  EXPECT_EQ(request_times_called, 1u);
  EXPECT_EQ(presents_called, 1u);
  EXPECT_EQ(vsyncs_handled, 0u);

  // Fire the `OnFramePresented` event associated with the first `Present`, then
  // pump the loop.  The `OnFramePresented` event is resolved.
  fake_session().FireOnFramePresentedEvent(
      MakeFramePresentedInfoForOnePresent(0, 0));
  loop().RunUntilIdle();
  EXPECT_TRUE(fake_session().command_queue().empty());
  EXPECT_EQ(request_times_called, 1u);
  EXPECT_EQ(presents_called, 1u);
  EXPECT_EQ(vsyncs_handled, 1u);

  // Simulate an AwaitVsync that comes after the first `OnFramePresented`
  // event.
  bool await_vsync_fired = false;
  AwaitVsyncChecked(session_connection, await_vsync_fired,
                    fml::TimeDelta::Zero(), kDefaultPresentationInterval);
  EXPECT_TRUE(await_vsync_fired);

  // Call Present and Pump the loop; `Present` and its callback is called.
  await_vsync_fired = false;
  session_connection.Present();
  loop().RunUntilIdle();
  EXPECT_TRUE(fake_session().command_queue().empty());
  EXPECT_FALSE(await_vsync_fired);
  EXPECT_EQ(request_times_called, 1u);
  EXPECT_EQ(presents_called, 2u);
  EXPECT_EQ(vsyncs_handled, 1u);

  // Fire the `OnFramePresented` event associated with the second `Present`,
  // then pump the loop.  The `OnFramePresented` event is resolved.
  fake_session().FireOnFramePresentedEvent(
      MakeFramePresentedInfoForOnePresent(0, 0));
  loop().RunUntilIdle();
  EXPECT_TRUE(fake_session().command_queue().empty());
  EXPECT_FALSE(await_vsync_fired);
  EXPECT_EQ(request_times_called, 1u);
  EXPECT_EQ(presents_called, 2u);
  EXPECT_EQ(vsyncs_handled, 2u);

  // Simulate an AwaitVsync that comes after the second `OnFramePresented`
  // event.
  await_vsync_fired = false;
  AwaitVsyncChecked(session_connection, await_vsync_fired,
                    kDefaultPresentationInterval,
                    kDefaultPresentationInterval * 2);
  EXPECT_TRUE(await_vsync_fired);
}

TEST_F(SessionConnectionTest, AwaitVsyncBackpressure) {
  // Set up a callback which allows sensing of how many presents
  // (`Present` calls) were handled.
  size_t presents_called = 0u;
  SetUpSessionStubs(
      nullptr /* request_presentation_times_handler */,
      [&presents_called](auto...) -> auto {
        presents_called++;
        return FuturePresentationTimes{
            .future_presentations = {},
            .remaining_presents_in_flight_allowed = 1,
        };
      });

  // Set up a callback which allows sensing of how many vsync's
  // (`OnFramePresented` events) were handled.
  size_t vsyncs_handled = 0u;
  on_frame_presented_event on_frame_presented = [&vsyncs_handled](auto...) {
    vsyncs_handled++;
  };

  // Create the SessionConnection but don't pump the loop.  No FIDL calls are
  // completed yet.
  flutter_runner::DefaultSessionConnection session_connection(
      GetCurrentTestName(), TakeSessionHandle(), []() { FML_CHECK(false); },
      GetTestLoopNowCallback(), std::move(on_frame_presented), 1,
      fml::TimeDelta::Zero());
  EXPECT_EQ(presents_called, 0u);
  EXPECT_EQ(vsyncs_handled, 0u);

  // Pump the loop; `RequestPresentationTimes`, `Present`, and both of their
  // callbacks are called.
  loop().RunUntilIdle();
  EXPECT_EQ(presents_called, 1u);
  EXPECT_EQ(vsyncs_handled, 0u);

  // Simulate an AwaitVsync that comes before the first `OnFramePresented`
  // event.
  bool await_vsync_fired = false;
  AwaitVsyncChecked(session_connection, await_vsync_fired,
                    fml::TimeDelta::Zero(), kDefaultPresentationInterval);
  EXPECT_FALSE(await_vsync_fired);

  // Fire the `OnFramePresented` event associated with the first `Present`, then
  // pump the loop.  The `OnFramePresented` event is resolved.  The AwaitVsync
  // callback is resolved.
  fake_session().FireOnFramePresentedEvent(
      MakeFramePresentedInfoForOnePresent(0, 0));
  loop().RunUntilIdle();
  EXPECT_TRUE(await_vsync_fired);
  EXPECT_EQ(presents_called, 1u);
  EXPECT_EQ(vsyncs_handled, 1u);

  // Simulate an AwaitVsync that comes before the second `Present`.
  await_vsync_fired = false;
  AwaitVsyncChecked(session_connection, await_vsync_fired,
                    kDefaultPresentationInterval,
                    kDefaultPresentationInterval * 2);
  EXPECT_TRUE(await_vsync_fired);

  // Call Present and Pump the loop; `Present` and its callback is called.
  await_vsync_fired = false;
  session_connection.Present();
  loop().RunUntilIdle();
  EXPECT_FALSE(await_vsync_fired);
  EXPECT_EQ(presents_called, 2u);
  EXPECT_EQ(vsyncs_handled, 1u);

  // Simulate an AwaitVsync that comes before the second `OnFramePresented`
  // event.
  await_vsync_fired = false;
  AwaitVsyncChecked(session_connection, await_vsync_fired,
                    kDefaultPresentationInterval * 2,
                    kDefaultPresentationInterval * 3);
  EXPECT_FALSE(await_vsync_fired);

  // Fire the `OnFramePresented` event associated with the second `Present`,
  // then pump the loop.  The `OnFramePresented` event is resolved.
  fake_session().FireOnFramePresentedEvent(
      MakeFramePresentedInfoForOnePresent(0, 0));
  loop().RunUntilIdle();
  EXPECT_TRUE(await_vsync_fired);
  EXPECT_EQ(presents_called, 2u);
  EXPECT_EQ(vsyncs_handled, 2u);
}

TEST_F(SessionConnectionTest, PresentBackpressure) {
  // Set up a callback which allows sensing of how many presents
  // (`Present` calls) were handled.
  size_t presents_called = 0u;
  SetUpSessionStubs(
      nullptr /* request_presentation_times_handler */,
      [&presents_called](auto...) -> auto {
        presents_called++;
        return FuturePresentationTimes{
            .future_presentations = {},
            .remaining_presents_in_flight_allowed = 1,
        };
      });

  // Set up a callback which allows sensing of how many vsync's
  // (`OnFramePresented` events) were handled.
  size_t vsyncs_handled = 0u;
  on_frame_presented_event on_frame_presented = [&vsyncs_handled](auto...) {
    vsyncs_handled++;
  };

  // Create the SessionConnection but don't pump the loop.  No FIDL calls are
  // completed yet.
  flutter_runner::DefaultSessionConnection session_connection(
      GetCurrentTestName(), TakeSessionHandle(), []() { FML_CHECK(false); },
      GetTestLoopNowCallback(), std::move(on_frame_presented), 1,
      fml::TimeDelta::Zero());
  EXPECT_EQ(presents_called, 0u);
  EXPECT_EQ(vsyncs_handled, 0u);

  // Pump the loop; `RequestPresentationTimes`, `Present`, and both of their
  // callbacks are called.
  loop().RunUntilIdle();
  EXPECT_EQ(presents_called, 1u);
  EXPECT_EQ(vsyncs_handled, 0u);

  // Call Present and Pump the loop; `Present` is not called due to backpressue.
  session_connection.Present();
  loop().RunUntilIdle();
  EXPECT_EQ(presents_called, 1u);
  EXPECT_EQ(vsyncs_handled, 0u);

  // Call Present again and Pump the loop; `Present` is not called due to
  // backpressue.
  session_connection.Present();
  loop().RunUntilIdle();
  EXPECT_EQ(presents_called, 1u);
  EXPECT_EQ(vsyncs_handled, 0u);

  // Fire the `OnFramePresented` event associated with the first `Present`, then
  // pump the loop.  The `OnFramePresented` event is resolved.  The pending
  // `Present` calls are resolved.
  fake_session().FireOnFramePresentedEvent(
      MakeFramePresentedInfoForOnePresent(0, 0));
  loop().RunUntilIdle();
  EXPECT_EQ(presents_called, 2u);
  EXPECT_EQ(vsyncs_handled, 1u);

  // Call Present and Pump the loop; `Present` is not called due to
  // backpressue.
  session_connection.Present();
  loop().RunUntilIdle();
  EXPECT_EQ(presents_called, 2u);
  EXPECT_EQ(vsyncs_handled, 1u);

  // Call Present again and Pump the loop; `Present` is not called due to
  // backpressue.
  session_connection.Present();
  loop().RunUntilIdle();
  EXPECT_EQ(presents_called, 2u);
  EXPECT_EQ(vsyncs_handled, 1u);

  // Fire the `OnFramePresented` event associated with the second `Present`,
  // then pump the loop.  The `OnFramePresented` event is resolved.  The pending
  // `Present` calls are resolved.
  fake_session().FireOnFramePresentedEvent(
      MakeFramePresentedInfoForOnePresent(0, 0));
  loop().RunUntilIdle();
  EXPECT_EQ(presents_called, 3u);
  EXPECT_EQ(vsyncs_handled, 2u);

  // Fire the `OnFramePresented` event associated with the third `Present`,
  // then pump the loop.  The `OnFramePresented` event is resolved.  No pending
  // `Present` calls exist, so none are resolved.
  fake_session().FireOnFramePresentedEvent(
      MakeFramePresentedInfoForOnePresent(0, 0));
  loop().RunUntilIdle();
  EXPECT_EQ(presents_called, 3u);
  EXPECT_EQ(vsyncs_handled, 3u);
}

}  // namespace flutter_runner::testing
