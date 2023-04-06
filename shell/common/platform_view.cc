// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/common/platform_view.h"

#include <utility>

#include "flutter/fml/make_copyable.h"
#include "flutter/fml/synchronization/waitable_event.h"
#include "flutter/shell/common/vsync_waiter_fallback.h"
#include "third_party/skia/include/gpu/gl/GrGLInterface.h"

namespace flutter {

PlatformView::PlatformView(Delegate& delegate, const TaskRunners& task_runners)
    : delegate_(delegate), task_runners_(task_runners), weak_factory_(this) {}

PlatformView::~PlatformView() = default;

std::unique_ptr<VsyncWaiter> PlatformView::CreateVSyncWaiter() {
  FML_DLOG(WARNING)
      << "This platform does not provide a Vsync waiter implementation. A "
         "simple timer based fallback is being used.";
  return std::make_unique<VsyncWaiterFallback>(task_runners_);
}

void PlatformView::DispatchPlatformMessage(
    std::unique_ptr<PlatformMessage> message) {
  delegate_.OnPlatformViewDispatchPlatformMessage(std::move(message));
}

void PlatformView::DispatchPointerDataPacket(
    std::unique_ptr<PointerDataPacket> packet) {
  delegate_.OnPlatformViewDispatchPointerDataPacket(
      pointer_data_packet_converter_.Convert(std::move(packet)));
}

void PlatformView::DispatchSemanticsAction(int32_t node_id,
                                           SemanticsAction action,
                                           fml::MallocMapping args) {
  delegate_.OnPlatformViewDispatchSemanticsAction(node_id, action,
                                                  std::move(args));
}

void PlatformView::SetSemanticsEnabled(bool enabled) {
  delegate_.OnPlatformViewSetSemanticsEnabled(enabled);
}

void PlatformView::SetAccessibilityFeatures(int32_t flags) {
  delegate_.OnPlatformViewSetAccessibilityFeatures(flags);
}

void PlatformView::SetViewportMetrics(const ViewportMetrics& metrics) {
  delegate_.OnPlatformViewSetViewportMetrics(metrics);
}

void PlatformView::NotifyCreated() {
  delegate_.OnPlatformViewCreated();
}

void PlatformView::NotifyDestroyed() {
  delegate_.OnPlatformViewDestroyed();
}

std::unique_ptr<Studio> PlatformView::CreateStudio() {
  std::unique_ptr<Studio> studio;
  // Threading: We want to use the platform view on the non-platform thread.
  // Using the weak pointer is illegal. But, we are going to introduce a latch
  // so that the platform view is not collected till the studio and the surface
  // are obtained.
  auto* platform_view = this;
  fml::ManualResetWaitableEvent latch;
  fml::TaskRunner::RunNowOrPostTask(
      task_runners_.GetRasterTaskRunner(), [platform_view, &studio, &latch]() {
        studio = platform_view->CreateRenderingStudio();
        if (!studio || !studio->IsValid()) {
          studio.reset();
        }
        latch.Signal();
      });
  latch.Wait();
  if (!studio) {
    FML_LOG(ERROR) << "Failed to create platform view rendering studio";
    return nullptr;
  }
  return studio;
}

std::unique_ptr<Surface> PlatformView::CreateSurface(int64_t view_id) {
  std::unique_ptr<Surface> surface;
  // Threading: We want to use the platform view on the non-platform thread.
  // Using the weak pointer is illegal. But, we are going to introduce a latch
  // so that the platform view is not collected till the studio and the surface
  // are obtained.
  auto* platform_view = this;
  fml::ManualResetWaitableEvent latch;
  fml::TaskRunner::RunNowOrPostTask(
      task_runners_.GetRasterTaskRunner(),
      [platform_view, &surface, &latch, view_id]() {
        surface = platform_view->CreateRenderingSurface(view_id);
        if (!surface || !surface->IsValid()) {
          surface.reset();
        }
        latch.Signal();
      });
  latch.Wait();
  if (!surface) {
    FML_LOG(ERROR) << "Failed to create platform view rendering surface";
    return nullptr;
  }
  return surface;
}

void PlatformView::ScheduleFrame() {
  delegate_.OnPlatformViewScheduleFrame();
}

sk_sp<GrDirectContext> PlatformView::CreateResourceContext() const {
  FML_DLOG(WARNING) << "This platform does not set up the resource "
                       "context on the IO thread for async texture uploads.";
  return nullptr;
}

std::shared_ptr<impeller::Context> PlatformView::GetImpellerContext() const {
  return nullptr;
}

void PlatformView::ReleaseResourceContext() const {}

PointerDataDispatcherMaker PlatformView::GetDispatcherMaker() {
  return [](DefaultPointerDataDispatcher::Delegate& delegate) {
    return std::make_unique<DefaultPointerDataDispatcher>(delegate);
  };
}

fml::WeakPtr<PlatformView> PlatformView::GetWeakPtr() const {
  return weak_factory_.GetWeakPtr();
}

void PlatformView::UpdateSemantics(
    SemanticsNodeUpdates update,  // NOLINT(performance-unnecessary-value-param)
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    CustomAccessibilityActionUpdates actions) {}

void PlatformView::HandlePlatformMessage(
    std::unique_ptr<PlatformMessage> message) {
  if (auto response = message->response()) {
    response->CompleteEmpty();
  }
}

void PlatformView::OnPreEngineRestart() const {}

void PlatformView::RegisterTexture(std::shared_ptr<flutter::Texture> texture) {
  delegate_.OnPlatformViewRegisterTexture(std::move(texture));
}

void PlatformView::UnregisterTexture(int64_t texture_id) {
  delegate_.OnPlatformViewUnregisterTexture(texture_id);
}

void PlatformView::MarkTextureFrameAvailable(int64_t texture_id) {
  delegate_.OnPlatformViewMarkTextureFrameAvailable(texture_id);
}

std::unique_ptr<Studio> PlatformView::CreateRenderingStudio() {
  // We have a default implementation because tests create a platform view but
  // never a rendering studio.
  FML_DCHECK(false) << "This platform does not provide a rendering studio but "
                       "it was notified of rendering studio creation.";
  return nullptr;
}

std::unique_ptr<Surface> PlatformView::CreateRenderingSurface(int64_t view_id) {
  // We have a default implementation because tests create a platform view but
  // never a rendering surface.
  FML_DCHECK(false) << "This platform does not provide a rendering surface but "
                       "it was notified of rendering surface creation.";
  return nullptr;
}

std::shared_ptr<ExternalViewEmbedder>
PlatformView::CreateExternalViewEmbedder() {
  FML_DLOG(WARNING)
      << "This platform doesn't support embedding external views.";
  return nullptr;
}

void PlatformView::SetNextFrameCallback(const fml::closure& closure) {
  if (!closure) {
    return;
  }

  delegate_.OnPlatformViewSetNextFrameCallback(closure);
}

std::unique_ptr<std::vector<std::string>>
PlatformView::ComputePlatformResolvedLocales(
    const std::vector<std::string>& supported_locale_data) {
  std::unique_ptr<std::vector<std::string>> out =
      std::make_unique<std::vector<std::string>>();
  return out;
}

void PlatformView::RequestDartDeferredLibrary(intptr_t loading_unit_id) {}

void PlatformView::LoadDartDeferredLibrary(
    intptr_t loading_unit_id,
    std::unique_ptr<const fml::Mapping> snapshot_data,
    std::unique_ptr<const fml::Mapping> snapshot_instructions) {}

void PlatformView::LoadDartDeferredLibraryError(
    intptr_t loading_unit_id,
    const std::string
        error_message,  // NOLINT(performance-unnecessary-value-param)
    bool transient) {}

void PlatformView::UpdateAssetResolverByType(
    std::unique_ptr<AssetResolver> updated_asset_resolver,
    AssetResolver::AssetResolverType type) {
  delegate_.UpdateAssetResolverByType(std::move(updated_asset_resolver), type);
}

std::unique_ptr<SnapshotSurfaceProducer>
PlatformView::CreateSnapshotSurfaceProducer() {
  return nullptr;
}

std::shared_ptr<PlatformMessageHandler>
PlatformView::GetPlatformMessageHandler() const {
  return nullptr;
}

const Settings& PlatformView::GetSettings() const {
  return delegate_.OnPlatformViewGetSettings();
}

}  // namespace flutter
