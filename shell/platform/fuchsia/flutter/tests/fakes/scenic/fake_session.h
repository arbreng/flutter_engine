// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_TESTS_FAKES_SCENIC_FAKE_SESSION_H_
#define FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_TESTS_FAKES_SCENIC_FAKE_SESSION_H_

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/scenic/scheduling/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <lib/async-testing/test_loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>

#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "flutter/fml/macros.h"

//#include "fake_resources.h"

namespace flutter_runner::testing {

class FakeSession : public fuchsia::ui::scenic::testing::Session_TestBase {
 public:
  using PresentHandler =
      std::function<fuchsia::images::PresentationInfo(uint64_t,
                                                      std::vector<zx::event>,
                                                      std::vector<zx::event>)>;
  using Present2Handler =
      std::function<fuchsia::scenic::scheduling::FuturePresentationTimes(
          fuchsia::ui::scenic::Present2Args)>;
  using RequestPresentationTimesHandler =
      std::function<fuchsia::scenic::scheduling::FuturePresentationTimes(
          int64_t)>;
  using SessionAndListenerClientPair =
      std::pair<fidl::InterfaceHandle<fuchsia::ui::scenic::Session>,
                fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener>>;

  FakeSession(async::TestLoop& loop);
  ~FakeSession() override = default;

  const std::string& debug_name() const { return debug_name_; }

  const std::vector<fuchsia::ui::scenic::Command>& command_queue() {
    return command_queue_;
  }

  bool is_bound() const { return binding_.is_bound() && listener_.is_bound(); }

  SessionAndListenerClientPair Bind();

  //   template <typename R>
  //   std::vector<std::weak_ptr<FakeResource>> ResourcesByType();

  //   template <typename R>
  //   std::vector<std::weak_ptr<FakeResource>> ResourcesByTypeLabel(
  //       const std::string& label);

  // Stub methods.  Call these to set a handler for the specified FIDL calls'
  // return values.
  void SetPresentHandler(PresentHandler present_handler);
  void SetPresent2Handler(Present2Handler present2_handler);
  void SetRequestPresentationTimesHandler(
      RequestPresentationTimesHandler request_presentation_times_handler);

  // Event methods.  Call these to fire the associated FIDL event.
  void FireOnFramePresentedEvent(
      fuchsia::scenic::scheduling::FramePresentedInfo frame_presented_info);

  // Error method.  Call to disconnect the session with an error.
  void DisconnectSession();

 private:
  // |fuchsia::ui::scenic::Session|
  void Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) override;

  // |fuchsia::ui::scenic::Session|
  void Present(uint64_t presentation_time,
               std::vector<zx::event> acquire_fences,
               std::vector<zx::event> release_fences,
               PresentCallback callback) override;

  // |fuchsia::ui::scenic::Session|
  void Present2(fuchsia::ui::scenic::Present2Args args,
                Present2Callback callback) override;

  // |fuchsia::ui::scenic::Session|
  void RequestPresentationTimes(
      int64_t requested_prediction_span,
      RequestPresentationTimesCallback callback) override;

  // |fuchsia::ui::scenic::Session|
  void RegisterBufferCollection(
      uint32_t buffer_id,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token)
      override;

  // |fuchsia::ui::scenic::Session|
  void DeregisterBufferCollection(uint32_t buffer_id) override;

  // |fuchsia::ui::scenic::Session|
  void SetDebugName(std::string debug_name) override;

  // |fuchsia::ui::scenic::testing::Session_TestBase|
  void NotImplemented_(const std::string& name) override;

  async::TestLoop& loop_;
  std::unique_ptr<async::LoopInterface> session_subloop_;

  fidl::Binding<fuchsia::ui::scenic::Session> binding_;
  fuchsia::ui::scenic::SessionListenerPtr listener_;

  std::string debug_name_;

  std::vector<fuchsia::ui::scenic::Command> command_queue_;
  //   std::unordered_map<FakeResourceId, std::shared_ptr<FakeResource>>
  //       resource_map_;
  //   std::unordered_map<FakeResourceId,
  //   std::set<std::shared_ptr<FakeResource>>>
  //       inactive_resource_map_;
  //   std::unordered_map<std::string, std::set<FakeResourceId>> label_map_;

  // These cache the ResourceId's of "active" resources, for easy lookup.
  // "inactive" resource id's are not cached.
  //   std::optional<FakeResourceId> view_;
  //   std::set<FakeResourceId> view_holders_;
  //   std::set<FakeResourceId> entity_nodes_;
  //   std::set<FakeResourceId> shape_nodes_;
  //   std::set<FakeResourceId> opacity_nodes_;
  //   std::set<FakeResourceId> shapes_;
  //   std::set<FakeResourceId> materials_;
  //   std::set<FakeResourceId> images_;

  PresentHandler present_handler_;
  Present2Handler present2_handler_;
  RequestPresentationTimesHandler request_presentation_times_handler_;

  FML_DISALLOW_COPY_AND_ASSIGN(FakeSession);
};

// template <typename R>
// std::vector<std::weak_ptr<FakeResource>> FakeSession::ResourcesByType() {}

// template <typename R>
// std::vector<std::weak_ptr<FakeResource>> FakeSession::ResourcesByTypeLabel(
//     const std::string& label) {}

}  // namespace flutter_runner::testing

#endif  // FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_TESTS_FAKES_SCENIC_FAKE_SESSION_H_
