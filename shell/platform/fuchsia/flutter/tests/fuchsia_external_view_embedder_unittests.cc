// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/default_session_connection.h"
#include "flutter/shell/platform/fuchsia/flutter/fuchsia_external_view_embedder.h"

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-testing/test_loop.h>
#include <lib/sys/cpp/component_context.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fml/time/time_delta.h"
#include "fml/time/time_point.h"
#include "gtest/gtest.h"

#include "fakes/scenic/fake_session.h"
#include "flutter/shell/platform/fuchsia/flutter/vulkan_surface.h"
#include "flutter/shell/platform/fuchsia/flutter/vulkan_surface_producer.h"

namespace flutter_runner::testing {
namespace {

class FakeSurfaceProducerSurface : public SurfaceProducerSurface {
 public:
  FakeSurfaceProducerSurface() = default;
  ~FakeSurfaceProducerSurface() override = default;

  bool IsValid() const override { return true; }
  SkISize GetSize() const override { return SkISize::MakeEmpty(); }
  uint32_t GetImageId() override { return 0; }
  sk_sp<SkSurface> GetSkiaSurface() const override {
    return sk_sp<SkSurface>(nullptr);
  }

  size_t AdvanceAndGetAge() override { return 0; }
  bool FlushSessionAcquireAndReleaseEvents() override { return true; }
  void SignalWritesFinished(
      const std::function<void(void)>& on_writes_committed) override {}
};

class FakeSurfaceProducer : public SurfaceProducer {
 public:
  FakeSurfaceProducer() = default;
  ~FakeSurfaceProducer() override = default;

  std::unique_ptr<SurfaceProducerSurface> ProduceSurface(
      const SkISize& size) override {
    return std::make_unique<FakeSurfaceProducerSurface>();
  }

  void SubmitSurfaces(
      std::vector<std::unique_ptr<SurfaceProducerSurface>> surfaces) override {}
};

std::string GetCurrentTestName() {
  return ::testing::UnitTest::GetInstance()->current_test_info()->name();
}

DefaultSessionConnection CreateSessionConnection(
    fidl::Binding<fuchsia::ui::scenic::SessionListener>& session_listener,
    FakeSession& fake_session,
    GetNowCallback get_now_callback) {
  FakeSession::SessionAndListenerClientPair session_and_listener =
      fake_session.Bind();

  session_listener.Bind(std::move(session_and_listener.second));
  return DefaultSessionConnection(
      GetCurrentTestName(), std::move(session_and_listener.first),
      []() { FML_CHECK(false); }, std::move(get_now_callback), [](auto...) {},
      1, fml::TimeDelta::Zero());
}

};  // namespace

class FuchsiaExternalViewEmbedderTest
    : public ::testing::Test,
      public fuchsia::ui::scenic::SessionListener {
 protected:
  FuchsiaExternalViewEmbedderTest()
      : session_listener_(this),
        fake_session_(loop_),
        session_connection_(CreateSessionConnection(session_listener_,
                                                    fake_session_,
                                                    GetTestLoopNowCallback())) {
  }
  ~FuchsiaExternalViewEmbedderTest() override = default;

  async::TestLoop& loop() { return loop_; }
  FakeSession& fake_session() { return fake_session_; }
  FakeSurfaceProducer& fake_surface_producer() {
    return fake_surface_producer_;
  }
  DefaultSessionConnection& session_connection() { return session_connection_; }

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

  fidl::Binding<fuchsia::ui::scenic::SessionListener> session_listener_;

  FakeSession fake_session_;
  FakeSurfaceProducer fake_surface_producer_;
  DefaultSessionConnection session_connection_;
};

TEST_F(FuchsiaExternalViewEmbedderTest, Initialization) {
  FuchsiaExternalViewEmbedder external_view_embedder(
      GetCurrentTestName(), fuchsia::ui::views::ViewToken{},
      scenic::ViewRefPair::New(), session_connection(),
      fake_surface_producer());
}

}  // namespace flutter_runner::testing
