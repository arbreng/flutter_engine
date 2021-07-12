// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_TESTS_FAKES_SCENIC_FAKE_RESOURCES_H_
#define FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_TESTS_FAKES_SCENIC_FAKE_RESOURCES_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>

#include <array>
#include <memory>
#include <set>
#include <string>
#include <variant>

namespace flutter_runner::testing {

using FakeResourceId = uint32_t;
constexpr FakeResourceId kFakeResourceIdInvalid = 0u;

struct FakeResource;

struct FakeNodeInfo {
  std::array<float, 4> rotation_quaternion = {0.f, 0.f, 0.f, 1.f};
  std::array<float, 3> scale_vector = {1.f, 1.f, 1.f};
  std::array<float, 3> translation_vector = {0.f, 0.f, 0.f};
  std::array<float, 3> anchor_vector = {0.f, 0.f, 0.f};
  fuchsia::ui::gfx::HitTestBehavior hit_test_behavior =
      fuchsia::ui::gfx::HitTestBehavior::kDefault;
  bool semantically_visible = true;

  std::set<std::shared_ptr<FakeResource>> children;
  std::shared_ptr<FakeResource> parent;

  bool operator==(const FakeNodeInfo& other) const {
    return rotation_quaternion == other.rotation_quaternion &&
           scale_vector == other.scale_vector &&
           translation_vector == other.translation_vector &&
           anchor_vector == other.anchor_vector &&
           hit_test_behavior == other.hit_test_behavior &&
           semantically_visible == other.semantically_visible &&
           children == other.children && parent == other.parent;
  }
};

struct FakeViewInfo {
  const std::variant<fuchsia::ui::gfx::ViewArgs, fuchsia::ui::gfx::ViewArgs3>
      view_args;

  std::set<std::shared_ptr<FakeResource>> children;

  bool operator==(const FakeViewInfo& other) const {
    return view_args == other.view_args && children == other.children;
  }
};

struct FakeViewHolderInfo {
  const fuchsia::ui::gfx::ViewHolderArgs view_holder_args;

  fuchsia::ui::gfx::ViewProperties properties;
  std::shared_ptr<FakeResource> parent;

  bool operator==(const FakeViewHolderInfo& other) const {
    return view_holder_args == other.view_holder_args &&
           properties == other.properties && parent == other.parent;
  }
};

struct FakeOpacityNodeInfo : public FakeNodeInfo {
  float opacity = 1.f;

  bool operator==(const FakeOpacityNodeInfo& other) const {
    return FakeNodeInfo::operator==(other) && opacity == other.opacity;
  }
};

struct FakeEntityNodeInfo : public FakeNodeInfo {
  std::vector<fuchsia::ui::gfx::Plane3> clip_planes;

  bool operator==(const FakeEntityNodeInfo& other) const {
    return FakeNodeInfo::operator==(other) && clip_planes == other.clip_planes;
  }
};

struct FakeShapeNodeInfo : public FakeNodeInfo {
  std::shared_ptr<FakeResource> shape;
  std::shared_ptr<FakeResource> material;

  bool operator==(const FakeShapeNodeInfo& other) const {
    return FakeNodeInfo::operator==(other) && shape == other.shape &&
           material == other.material;
  }
};

struct FakeShapeInfo {
  struct CircleInfo {
    const float radius;

    bool operator==(const CircleInfo& other) const {
      return radius == other.radius;
    }
  };

  struct RectangleInfo {
    const float width;
    const float height;

    bool operator==(const RectangleInfo& other) const {
      return width == other.width && height == other.height;
    }
  };

  struct RoundedRectangleInfo {
    const float width;
    const float height;
    const float top_left_radius;
    const float top_right_radius;
    const float bottom_right_radius;
    const float bottom_left_radius;

    bool operator==(const RoundedRectangleInfo& other) const {
      return width == other.width && height == other.height &&
             top_left_radius == other.top_left_radius &&
             top_right_radius == other.top_right_radius &&
             bottom_right_radius == other.bottom_right_radius &&
             bottom_left_radius == other.bottom_left_radius;
    }
  };

  const std::variant<CircleInfo, RectangleInfo, RoundedRectangleInfo>
      shape_info;

  bool operator==(const FakeShapeInfo& other) const {
    return shape_info == other.shape_info;
  }
};

struct FakeMaterialInfo {
  std::array<float, 4> color = {1.f, 1.f, 1.f, 1.f};
  std::shared_ptr<FakeResource> image;

  bool operator==(const FakeMaterialInfo& other) const {
    return color == other.color && image == other.image;
  }
};

struct FakeImageInfo {
  const std::variant<fuchsia::ui::gfx::ImageArgs,
                     fuchsia::ui::gfx::ImageArgs2,
                     fuchsia::ui::gfx::ImageArgs3,
                     fuchsia::ui::gfx::ImagePipeArgs,
                     fuchsia::ui::gfx::ImagePipe2Args>
      image_args;

  bool operator==(const FakeImageInfo& other) const {
    return image_args == other.image_args;
  }
};

struct FakeResource {
  const FakeResourceId id;

  std::string label;
  uint32_t event_mask = 0;

  std::variant<FakeViewInfo,
               FakeViewHolderInfo,
               FakeOpacityNodeInfo,
               FakeEntityNodeInfo,
               FakeShapeNodeInfo,
               FakeShapeInfo,
               FakeMaterialInfo,
               FakeImageInfo>
      resource_info;

  bool operator==(const FakeResource& other) const {
    return id == other.id && label == other.label &&
           event_mask == other.event_mask &&
           resource_info == other.resource_info;
  }
};

}  // namespace flutter_runner::testing

#endif  // FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_TESTS_FAKES_SCENIC_FAKE_RESOURCES_H_
