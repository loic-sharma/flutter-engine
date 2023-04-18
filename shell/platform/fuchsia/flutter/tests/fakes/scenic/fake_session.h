// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_TESTS_FAKES_SCENIC_FAKE_SESSION_H_
#define FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_TESTS_FAKES_SCENIC_FAKE_SESSION_H_

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/scenic/scheduling/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>  // For std::pair
#include <vector>

#include "flutter/fml/macros.h"

#include "fake_resources.h"

namespace flutter_runner::testing {

// A lightweight fake implementation of the scenic Session API, also called
// the "gfx API".  The fake has no side effects besides mutating its own
// internal state.
//
// The fake allows tests to do a few things that would be difficult using either
// a mock implementation or the real implementation:
//   + It allows the user to hook `Present` invocations and respond with
//   stubbed-out `FuturePresentationTimes`, but more crucially it mimics the
//   real scenic behavior of only processing commands when a `Present` is
//   invoked.
//   + It allows the user to inspect a snapshot of the session's local scene
//   graph at any moment in time, via the `SceneGraph()` accessor.
//   + The resources returned by `SceneGraph()` that the test uses for
//   inspection are decoupled from the resources managed internally by the
//   `FakeSession` itself -- they are a snapshot of the scene graph at that
//   moment in time, with all snapshot state being cloned from the underlying
//   scene graph state.  This allows the `FakeSession` and test to naturally use
//   `shared_ptr` for reference counting and mimic the real scenic behavior
//   exactly, instead of an awkward index-based API.
//   + It stores the various session resources generated by commands into a
//   std::unordered_map, and also correctly manages the resource lifetimes via
//   reference counting. This allows a resource to stay alive if its parent
//   still holds a reference to it, in the same way the real scenic
//   implementation would.
//
// Limitations:
//   +Error handling / session disconnection is still WIP.  FakeSession will
//   likely generate a CHECK in any place where the real scenic would disconnect
//   the session or send a ScenicError.
//   +Deprecated / obsolete commands are not handled.
//   +Input is not handled.
//   +Rendering is not handled.
//   +Cross-session links are not handled; the FakeSession only stores the
//   tokens provided to it in a FakeResourceState.
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

  FakeSession();
  ~FakeSession() override = default;

  bool is_bound() const { return binding_.is_bound() && listener_.is_bound(); }

  const std::string& debug_name() const { return debug_name_; }

  const std::deque<fuchsia::ui::scenic::Command>& command_queue() const {
    return command_queue_;
  }

  // Generate a snapshot of the underlying scene graph for test inspection.
  FakeSceneGraph SceneGraph() const {
    return SceneGraphFromState(scene_graph_);
  }

  // Bind this session's FIDL channels to the `dispatcher` and allow processing
  // of incoming FIDL requests.
  SessionAndListenerClientPair Bind(async_dispatcher_t* dispatcher = nullptr);

  // Set a handler for `Present`-related FIDL calls' return values.
  void SetPresentHandler(PresentHandler present_handler);
  void SetPresent2Handler(Present2Handler present2_handler);
  void SetRequestPresentationTimesHandler(
      RequestPresentationTimesHandler request_presentation_times_handler);

  // Call after a successful `Present` or `Present2` to fire an
  // `OnFramePresented` event, which simulates the frame being displayed.
  void FireOnFramePresentedEvent(
      fuchsia::scenic::scheduling::FramePresentedInfo frame_presented_info);

  // Disconnect the session with an error.
  // TODO: Call this internally upon command error, instead of CHECK'ing.
  void DisconnectSession();

 private:
  // |fuchsia::ui::scenic::testing::Session_TestBase|
  void NotImplemented_(const std::string& name) override;

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

  // Resource management.
  std::shared_ptr<FakeResourceState> GetResource(FakeResourceId id);
  void AddResource(FakeResourceState&& resource);
  void DetachResourceFromParent(
      std::shared_ptr<FakeResourceState> resource_ptr,
      std::shared_ptr<FakeResourceState> new_parent_ptr = nullptr);
  void PruneDeletedResourceRefs();

  // Apply queued commands and mutate the resource map.
  void ApplyCommands();
  void ApplyCreateResourceCmd(fuchsia::ui::gfx::CreateResourceCmd command);
  void ApplyReleaseResourceCmd(fuchsia::ui::gfx::ReleaseResourceCmd command);
  void ApplyAddChildCmd(fuchsia::ui::gfx::AddChildCmd command);
  void ApplyDetachCmd(fuchsia::ui::gfx::DetachCmd command);
  void ApplyDetachChildrenCmd(fuchsia::ui::gfx::DetachChildrenCmd command);
  void ApplySetTranslationCmd(fuchsia::ui::gfx::SetTranslationCmd command);
  void ApplySetScaleCmd(fuchsia::ui::gfx::SetScaleCmd command);
  void ApplySetRotationCmd(fuchsia::ui::gfx::SetRotationCmd command);
  void ApplySetAnchorCmd(fuchsia::ui::gfx::SetAnchorCmd command);
  void ApplySetOpacityCmd(fuchsia::ui::gfx::SetOpacityCmd command);
  void ApplySetShapeCmd(fuchsia::ui::gfx::SetShapeCmd command);
  void ApplySetMaterialCmd(fuchsia::ui::gfx::SetMaterialCmd command);
  void ApplySetClipPlanesCmd(fuchsia::ui::gfx::SetClipPlanesCmd command);
  void ApplySetViewPropertiesCmd(
      fuchsia::ui::gfx::SetViewPropertiesCmd command);
  void ApplySetHitTestBehaviorCmd(
      fuchsia::ui::gfx::SetHitTestBehaviorCmd command);
  void ApplySetSemanticVisibilityCmd(
      fuchsia::ui::gfx::SetSemanticVisibilityCmd command);
  void ApplySetTextureCmd(fuchsia::ui::gfx::SetTextureCmd command);
  void ApplySetColorCmd(fuchsia::ui::gfx::SetColorCmd command);
  void ApplySetEventMaskCmd(fuchsia::ui::gfx::SetEventMaskCmd command);
  void ApplySetLabelCmd(fuchsia::ui::gfx::SetLabelCmd command);
  void ApplySetEnableViewDebugBoundsCmd(
      fuchsia::ui::gfx::SetEnableDebugViewBoundsCmd command);
  void ApplySetViewHolderBoundsColorCmd(
      fuchsia::ui::gfx::SetViewHolderBoundsColorCmd command);
  void ApplyCreateMemory(FakeResourceId id, fuchsia::ui::gfx::MemoryArgs args);
  void ApplyCreateImage(FakeResourceId id, fuchsia::ui::gfx::ImageArgs args);
  void ApplyCreateImage2(FakeResourceId id, fuchsia::ui::gfx::ImageArgs2 args);
  void ApplyCreateImage3(FakeResourceId id, fuchsia::ui::gfx::ImageArgs3 args);
  void ApplyCreateImagePipe2(FakeResourceId id,
                             fuchsia::ui::gfx::ImagePipe2Args args);
  void ApplyCreateRectangle(FakeResourceId id,
                            fuchsia::ui::gfx::RectangleArgs args);
  void ApplyCreateRoundedRectangle(FakeResourceId id,
                                   fuchsia::ui::gfx::RoundedRectangleArgs args);
  void ApplyCreateCircle(FakeResourceId id, fuchsia::ui::gfx::CircleArgs args);
  void ApplyCreateMaterial(FakeResourceId id,
                           fuchsia::ui::gfx::MaterialArgs args);
  void ApplyCreateView(FakeResourceId id, fuchsia::ui::gfx::ViewArgs args);
  void ApplyCreateViewHolder(FakeResourceId id,
                             fuchsia::ui::gfx::ViewHolderArgs args);
  void ApplyCreateView(FakeResourceId id, fuchsia::ui::gfx::ViewArgs3 args);
  void ApplyCreateEntityNode(FakeResourceId id,
                             fuchsia::ui::gfx::EntityNodeArgs args);
  void ApplyCreateOpacityNode(FakeResourceId id,
                              fuchsia::ui::gfx::OpacityNodeArgsHACK args);
  void ApplyCreateShapeNode(FakeResourceId id,
                            fuchsia::ui::gfx::ShapeNodeArgs args);

  fidl::Binding<fuchsia::ui::scenic::Session> binding_;
  fuchsia::ui::scenic::SessionListenerPtr listener_;

  std::string debug_name_;

  FakeSceneGraphState scene_graph_;
  std::deque<fuchsia::ui::scenic::Command> command_queue_;

  // This map is used to cache parent refs for `AddChildCmd`.
  //
  // Ideally we would like to map weak(parent) -> weak(child), but std::weak_ptr
  // cannot be the Key for an associative container. Instead we key on the raw
  // parent pointer and store pair(weak(parent), weak(child)) in the map.
  std::unordered_map<FakeResourceState*,
                     std::pair<std::weak_ptr<FakeResourceState>,
                               std::weak_ptr<FakeResourceState>>>
      parents_map_;

  PresentHandler present_handler_;
  Present2Handler present2_handler_;
  RequestPresentationTimesHandler request_presentation_times_handler_;

  FML_DISALLOW_COPY_AND_ASSIGN(FakeSession);
};

}  // namespace flutter_runner::testing

#endif  // FLUTTER_SHELL_PLATFORM_FUCHSIA_FLUTTER_TESTS_FAKES_SCENIC_FAKE_SESSION_H_
