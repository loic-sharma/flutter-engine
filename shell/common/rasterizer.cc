// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/common/rasterizer.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "flow/frame_timings.h"
#include "flutter/common/graphics/persistent_cache.h"
#include "flutter/flow/layers/offscreen_surface.h"
#include "flutter/fml/time/time_delta.h"
#include "flutter/fml/time/time_point.h"
#include "flutter/shell/common/serialization_callbacks.h"
#include "fml/make_copyable.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPictureRecorder.h"
#include "third_party/skia/include/core/SkSerialProcs.h"
#include "third_party/skia/include/core/SkSize.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/core/SkSurfaceCharacterization.h"
#include "third_party/skia/include/utils/SkBase64.h"

namespace flutter {

// The rasterizer will tell Skia to purge cached resources that have not been
// used within this interval.
static constexpr std::chrono::milliseconds kSkiaCleanupExpiration(15000);

Rasterizer::Rasterizer(Delegate& delegate,
                       MakeGpuImageBehavior gpu_image_behavior)
    : delegate_(delegate),
      gpu_image_behavior_(gpu_image_behavior),
      compositor_context_(std::make_unique<flutter::CompositorContext>(*this)),
      user_override_resource_cache_bytes_(false),
      snapshot_controller_(
          SnapshotController::Make(*this, delegate.GetSettings())),
      weak_factory_(this) {
  FML_DCHECK(compositor_context_);
}

Rasterizer::~Rasterizer() = default;

fml::TaskRunnerAffineWeakPtr<Rasterizer> Rasterizer::GetWeakPtr() const {
  return weak_factory_.GetWeakPtr();
}

fml::TaskRunnerAffineWeakPtr<SnapshotDelegate> Rasterizer::GetSnapshotDelegate()
    const {
  return weak_factory_.GetWeakPtr();
}

void Rasterizer::SetImpellerContext(
    std::weak_ptr<impeller::Context> impeller_context) {
  impeller_context_ = std::move(impeller_context);
}

void Rasterizer::Setup(std::unique_ptr<Studio> studio) {
  studio_ = std::move(studio);

  if (max_cache_bytes_.has_value()) {
    SetResourceCacheMaxBytes(max_cache_bytes_.value(),
                             user_override_resource_cache_bytes_);
  }

  auto context_switch = studio_->MakeRenderContextCurrent();
  if (context_switch->GetResult()) {
    compositor_context_->OnGrContextCreated();
  }

  if (external_view_embedder_ &&
      external_view_embedder_->SupportsDynamicThreadMerging() &&
      !raster_thread_merger_) {
    const auto platform_id =
        delegate_.GetTaskRunners().GetPlatformTaskRunner()->GetTaskQueueId();
    const auto gpu_id =
        delegate_.GetTaskRunners().GetRasterTaskRunner()->GetTaskQueueId();
    raster_thread_merger_ = fml::RasterThreadMerger::CreateOrShareThreadMerger(
        delegate_.GetParentRasterThreadMerger(), platform_id, gpu_id);
  }
  if (raster_thread_merger_) {
    raster_thread_merger_->SetMergeUnmergeCallback([=]() {
      // Clear the GL context after the thread configuration has changed.
      if (studio_) {
        studio_->ClearRenderContext();
      }
    });
  }
}

void Rasterizer::TeardownExternalViewEmbedder() {
  if (external_view_embedder_) {
    external_view_embedder_->Teardown();
  }
}

void Rasterizer::Teardown() {
  if (studio_) {
    auto context_switch = studio_->MakeRenderContextCurrent();
    if (context_switch->GetResult()) {
      compositor_context_->OnGrContextDestroyed();
      if (auto* context = studio_->GetContext()) {
        context->purgeUnlockedResources(/*scratchResourcesOnly=*/false);
      }
    }
    studio_.reset();
  }
  surfaces_.clear();

  if (raster_thread_merger_.get() != nullptr &&
      raster_thread_merger_.get()->IsMerged()) {
    FML_DCHECK(raster_thread_merger_->IsEnabled());
    raster_thread_merger_->UnMergeNowIfLastOne();
    raster_thread_merger_->SetMergeUnmergeCallback(nullptr);
  }
}

void Rasterizer::EnableThreadMergerIfNeeded() {
  if (raster_thread_merger_) {
    raster_thread_merger_->Enable();
  }
}

void Rasterizer::DisableThreadMergerIfNeeded() {
  if (raster_thread_merger_) {
    raster_thread_merger_->Disable();
  }
}

void Rasterizer::NotifyLowMemoryWarning() const {
  if (!studio_) {
    FML_DLOG(INFO)
        << "Rasterizer::NotifyLowMemoryWarning called with no surface.";
    return;
  }
  auto context = studio_->GetContext();
  if (!context) {
    FML_DLOG(INFO)
        << "Rasterizer::NotifyLowMemoryWarning called with no GrContext.";
    return;
  }
  auto context_switch = studio_->MakeRenderContextCurrent();
  if (!context_switch->GetResult()) {
    return;
  }
  context->performDeferredCleanup(std::chrono::milliseconds(0));
}

void Rasterizer::AddSurface(int64_t view_id, std::unique_ptr<Surface> surface) {
  bool insertion_happened =
      surfaces_
          .try_emplace(/* map key=*/view_id, /*constructor args:*/ view_id,
                       std::move(surface))
          .second;
  if (!insertion_happened) {
    FML_DLOG(INFO) << "Rasterizer::AddSurface called with an existing view ID "
                   << view_id << ".";
  }
}

void Rasterizer::RemoveSurface(int64_t view_id) {
  surfaces_.erase(view_id);
}

std::shared_ptr<flutter::TextureRegistry> Rasterizer::GetTextureRegistry() {
  return compositor_context_->texture_registry();
}

GrDirectContext* Rasterizer::GetGrContext() {
  return studio_ ? studio_->GetContext() : nullptr;
}

bool Rasterizer::HasLastLayerTree() const {
  // TODO(dkwingsmt): This method is only available in unittests now
  for (auto& record_pair : surfaces_) {
    auto& layer_tree = record_pair.second.last_tree;
    if (layer_tree) {
      return true;
    }
  }
  return false;
}

int Rasterizer::DrawLastLayerTree(
    std::unique_ptr<FrameTimingsRecorder> frame_timings_recorder,
    bool enable_leaf_layer_tracing) {
  if (!studio_) {
    return 0;
  }
  int success_count = 0;
  bool should_resubmit_frame = false;
  for (auto& record_pair : surfaces_) {
    Surface* surface = record_pair.second.surface.get();
    flutter::LayerTree* layer_tree = record_pair.second.last_tree.get();
    float device_pixel_ratio = record_pair.second.last_pixel_ratio;
    if (!surface || !layer_tree) {
      continue;
    }
    if (enable_leaf_layer_tracing) {
      layer_tree->enable_leaf_layer_tracing(true);
    }
    RasterStatus raster_status =
        DrawToSurface(*frame_timings_recorder, layer_tree, device_pixel_ratio,
                      &record_pair.second);
    if (enable_leaf_layer_tracing) {
      layer_tree->enable_leaf_layer_tracing(false);
    }
    should_resubmit_frame =
        should_resubmit_frame || ShouldResubmitFrame(raster_status);
    success_count += 1;
  }

  // EndFrame should perform cleanups for the external_view_embedder.
  if (external_view_embedder_ && external_view_embedder_->GetUsedThisFrame()) {
    external_view_embedder_->SetUsedThisFrame(false);
    external_view_embedder_->EndFrame(should_resubmit_frame,
                                      raster_thread_merger_);
  }
  return success_count;
}

RasterStatus Rasterizer::Draw(
    const std::shared_ptr<LayerTreePipeline>& pipeline,
    LayerTreeDiscardCallback discard_callback) {
  TRACE_EVENT0("flutter", "GPURasterizer::Draw");
  if (raster_thread_merger_ &&
      !raster_thread_merger_->IsOnRasterizingThread()) {
    // we yield and let this frame be serviced on the right thread.
    return RasterStatus::kYielded;
  }
  FML_DCHECK(delegate_.GetTaskRunners()
                 .GetRasterTaskRunner()
                 ->RunsTasksOnCurrentThread());

  DoDrawResult draw_result;
  LayerTreePipeline::Consumer consumer =
      [this, &draw_result,
       &discard_callback](std::unique_ptr<LayerTreeItem> item) {
        std::unique_ptr<FrameTimingsRecorder> frame_timings_recorder =
            std::move(item->frame_timings_recorder);

        // TODO: We should record the start time for each layer tree.
        // Currently this records the start time for the first layer tree.
        // Frame_timings_recorder->RecordRasterStart(fml::TimePoint::Now());

        frame_timings_recorder->RecordRasterStart(fml::TimePoint::Now());
        float device_pixel_ratio = item->device_pixel_ratio;

        for (auto& item : item->layer_trees) {
          int64_t view_id = item.first;
          std::unique_ptr<LayerTree> layer_tree = std::move(item.second);
          // TODO: Discard checks the layer tree's size matches the view's size.
          // This needs to be updated for multi-view.
          if (discard_callback(view_id, *layer_tree.get())) {
            draw_result.raster_status = RasterStatus::kDiscarded;
          } else {
            draw_result = DoDraw(view_id, *frame_timings_recorder.get(),
                                 std::move(layer_tree), device_pixel_ratio);
          }

          bool should_resubmit_frame =
              ShouldResubmitFrame(draw_result.raster_status);
          if (external_view_embedder_ &&
              external_view_embedder_->GetUsedThisFrame()) {
            external_view_embedder_->SetUsedThisFrame(false);
            external_view_embedder_->EndFrame(should_resubmit_frame,
                                              raster_thread_merger_);
          }
        }
        frame_timings_recorder->RecordRasterEnd(
            &compositor_context_->raster_cache());

        FireNextFrameCallbackIfPresent();

    // TODO: We want to record raster end for each layer tree.
    // Currently this records the end time for the last layer tree.
    // frame_timings_recorder->RecordRasterEnd(
    //     &compositor_context_->raster_cache());

// SceneDisplayLag events are disabled on Fuchsia.
// see: https://github.com/flutter/flutter/issues/56598
#if !defined(OS_FUCHSIA)
        const fml::TimePoint raster_finish_time =
            frame_timings_recorder->GetRasterEndTime();
        fml::TimePoint frame_target_time =
            frame_timings_recorder->GetVsyncTargetTime();
        if (raster_finish_time > frame_target_time) {
          fml::TimePoint latest_frame_target_time =
              delegate_.GetLatestFrameTargetTime();
          const auto frame_budget_millis = delegate_.GetFrameBudget().count();
          if (latest_frame_target_time < raster_finish_time) {
            latest_frame_target_time =
                latest_frame_target_time +
                fml::TimeDelta::FromMillisecondsF(frame_budget_millis);
          }
          const auto frame_lag =
              (latest_frame_target_time - frame_target_time).ToMillisecondsF();
          const int vsync_transitions_missed =
              round(frame_lag / frame_budget_millis);
          fml::tracing::TraceEventAsyncComplete(
              "flutter",                    // category
              "SceneDisplayLag",            // name
              raster_finish_time,           // begin_time
              latest_frame_target_time,     // end_time
              "frame_target_time",          // arg_key_1
              frame_target_time,            // arg_val_1
              "current_frame_target_time",  // arg_key_2
              latest_frame_target_time,     // arg_val_2
              "vsync_transitions_missed",   // arg_key_3
              vsync_transitions_missed      // arg_val_3
          );
        }
#endif
      };

  PipelineConsumeResult consume_result = pipeline->Consume(consumer);
  if (consume_result == PipelineConsumeResult::NoneAvailable) {
    return RasterStatus::kFailed;
  }
  // if the raster status is to resubmit the frame, we push the frame to the
  // front of the queue and also change the consume status to more available.

  bool should_resubmit_frame = ShouldResubmitFrame(draw_result.raster_status);
  if (should_resubmit_frame) {
    // TODO: Handle frame resubmission.
    FML_LOG(ERROR) << "TODO: Support resubmitting multiple layer trees";

    /*
    auto resubmitted_layer_tree_item = std::make_unique<LayerTreeItem>(
        draw_result.resubmitted_view_id,
        std::move(draw_result.resubmitted_layer_tree),
        std::move(draw_result.resubmitted_recorder),
        draw_result.resubmitted_pixel_ratio);
    auto front_continuation = pipeline->ProduceIfEmpty();
    PipelineProduceResult pipeline_result =
        front_continuation.Complete(std::move(resubmitted_layer_tree_item));
    if (pipeline_result.success) {
      consume_result = PipelineConsumeResult::MoreAvailable;
    }
    */
  } else if (draw_result.raster_status == RasterStatus::kEnqueuePipeline) {
    consume_result = PipelineConsumeResult::MoreAvailable;
  }

  // Consume as many pipeline items as possible. But yield the event loop
  // between successive tries.
  switch (consume_result) {
    case PipelineConsumeResult::MoreAvailable: {
      delegate_.GetTaskRunners().GetRasterTaskRunner()->PostTask(
          fml::MakeCopyable(
              [weak_this = weak_factory_.GetWeakPtr(), pipeline,
               discard_callback = std::move(discard_callback)]() mutable {
                if (weak_this) {
                  weak_this->Draw(pipeline, std::move(discard_callback));
                }
              }));
      break;
    }
    default:
      break;
  }

  return draw_result.raster_status;
}

bool Rasterizer::ShouldResubmitFrame(const RasterStatus& raster_status) {
  return raster_status == RasterStatus::kResubmit ||
         raster_status == RasterStatus::kSkipAndRetry;
}

namespace {
std::unique_ptr<SnapshotDelegate::GpuImageResult> MakeBitmapImage(
    const sk_sp<DisplayList>& display_list,
    const SkImageInfo& image_info) {
  FML_DCHECK(display_list);
  // Use 16384 as a proxy for the maximum texture size for a GPU image.
  // This is meant to be large enough to avoid false positives in test contexts,
  // but not so artificially large to be completely unrealistic on any platform.
  // This limit is taken from the Metal specification. D3D, Vulkan, and GL
  // generally have lower limits.
  if (image_info.width() > 16384 || image_info.height() > 16384) {
    return std::make_unique<SnapshotDelegate::GpuImageResult>(
        GrBackendTexture(), nullptr, nullptr,
        "unable to create bitmap render target at specified size " +
            std::to_string(image_info.width()) + "x" +
            std::to_string(image_info.height()));
  };

  sk_sp<SkSurface> surface = SkSurface::MakeRaster(image_info);
  auto canvas = DlSkCanvasAdapter(surface->getCanvas());
  canvas.Clear(DlColor::kTransparent());
  canvas.DrawDisplayList(display_list);

  sk_sp<SkImage> image = surface->makeImageSnapshot();
  return std::make_unique<SnapshotDelegate::GpuImageResult>(
      GrBackendTexture(), nullptr, image,
      image ? "" : "Unable to create image");
}
}  // namespace

std::unique_ptr<Rasterizer::GpuImageResult> Rasterizer::MakeSkiaGpuImage(
    sk_sp<DisplayList> display_list,
    const SkImageInfo& image_info) {
  TRACE_EVENT0("flutter", "Rasterizer::MakeGpuImage");
  FML_DCHECK(display_list);

  std::unique_ptr<SnapshotDelegate::GpuImageResult> result;
  delegate_.GetIsGpuDisabledSyncSwitch()->Execute(
      fml::SyncSwitch::Handlers()
          .SetIfTrue([&result, &image_info, &display_list] {
            // TODO(dnfield): This isn't safe if display_list contains any GPU
            // resources like an SkImage_gpu.
            result = MakeBitmapImage(display_list, image_info);
          })
          .SetIfFalse([&result, &image_info, &display_list,
                       studio = studio_.get(),
                       gpu_image_behavior = gpu_image_behavior_] {
            if (!studio ||
                gpu_image_behavior == MakeGpuImageBehavior::kBitmap) {
              // TODO(dnfield): This isn't safe if display_list contains any GPU
              // resources like an SkImage_gpu.
              result = MakeBitmapImage(display_list, image_info);
              return;
            }

            auto context_switch = studio->MakeRenderContextCurrent();
            if (!context_switch->GetResult()) {
              result = MakeBitmapImage(display_list, image_info);
              return;
            }

            auto* context = studio->GetContext();
            if (!context) {
              result = MakeBitmapImage(display_list, image_info);
              return;
            }

            GrBackendTexture texture = context->createBackendTexture(
                image_info.width(), image_info.height(), image_info.colorType(),
                GrMipmapped::kNo, GrRenderable::kYes);
            if (!texture.isValid()) {
              result = std::make_unique<SnapshotDelegate::GpuImageResult>(
                  GrBackendTexture(), nullptr, nullptr,
                  "unable to create texture render target at specified size " +
                      std::to_string(image_info.width()) + "x" +
                      std::to_string(image_info.height()));
              return;
            }

            sk_sp<SkSurface> sk_surface = SkSurface::MakeFromBackendTexture(
                context, texture, kTopLeft_GrSurfaceOrigin, /*sampleCnt=*/0,
                image_info.colorType(), image_info.refColorSpace(), nullptr);
            if (!sk_surface) {
              result = std::make_unique<SnapshotDelegate::GpuImageResult>(
                  GrBackendTexture(), nullptr, nullptr,
                  "unable to create rendering surface for image");
              return;
            }

            auto canvas = DlSkCanvasAdapter(sk_surface->getCanvas());
            canvas.Clear(DlColor::kTransparent());
            canvas.DrawDisplayList(display_list);

            result = std::make_unique<SnapshotDelegate::GpuImageResult>(
                texture, sk_ref_sp(context), nullptr, "");
          }));
  return result;
}

sk_sp<DlImage> Rasterizer::MakeRasterSnapshot(sk_sp<DisplayList> display_list,
                                              SkISize picture_size) {
  return snapshot_controller_->MakeRasterSnapshot(display_list, picture_size);
}

sk_sp<SkImage> Rasterizer::ConvertToRasterImage(sk_sp<SkImage> image) {
  TRACE_EVENT0("flutter", __FUNCTION__);
  return snapshot_controller_->ConvertToRasterImage(image);
}

fml::Milliseconds Rasterizer::GetFrameBudget() const {
  return delegate_.GetFrameBudget();
};

Rasterizer::DoDrawResult Rasterizer::DoDraw(
    int64_t view_id,
    FrameTimingsRecorder& frame_timings_recorder,
    std::unique_ptr<flutter::LayerTree> layer_tree,
    float device_pixel_ratio) {
  TRACE_EVENT_WITH_FRAME_NUMBER(&frame_timings_recorder, "flutter",
                                "Rasterizer::DoDraw");
  FML_DCHECK(delegate_.GetTaskRunners()
                 .GetRasterTaskRunner()
                 ->RunsTasksOnCurrentThread());
  SurfaceRecord* surface_record = GetSurface(view_id);

  if (!layer_tree || !surface_record) {
    return DoDrawResult{
        .raster_status = RasterStatus::kFailed,
    };
  }

  PersistentCache* persistent_cache = PersistentCache::GetCacheForProcess();
  persistent_cache->ResetStoredNewShaders();

  RasterStatus raster_status =
      DrawToSurface(frame_timings_recorder, layer_tree.get(),
                    device_pixel_ratio, surface_record);
  if (raster_status == RasterStatus::kSuccess) {
    surface_record->last_tree = std::move(layer_tree);
    surface_record->last_pixel_ratio = device_pixel_ratio;
  } else if (ShouldResubmitFrame(raster_status)) {
    return DoDrawResult{
        .raster_status = raster_status,
        .resubmitted_view_id = view_id,
        .resubmitted_layer_tree = std::move(layer_tree),
        .resubmitted_recorder = frame_timings_recorder.CloneUntil(
            FrameTimingsRecorder::State::kBuildEnd),
        .resubmitted_pixel_ratio = device_pixel_ratio,
    };
  } else if (raster_status == RasterStatus::kDiscarded) {
    return DoDrawResult{
        .raster_status = raster_status,
    };
  }

  // TODO: Evaluate everything below. Should it be done once per frame?
  if (persistent_cache->IsDumpingSkp() &&
      persistent_cache->StoredNewShaders()) {
    auto screenshot = ScreenshotLayerTree(ScreenshotType::SkiaPicture, false,
                                          *surface_record);
    persistent_cache->DumpSkp(*screenshot.data);
  }

  // Pipeline pressure is applied from a couple of places:
  // rasterizer: When there are more items as of the time of Consume.
  // animator (via shell): Frame gets produces every vsync.
  // Enqueing here is to account for the following scenario:
  // T = 1
  //  - one item (A) in the pipeline
  //  - rasterizer starts (and merges the threads)
  //  - pipeline consume result says no items to process
  // T = 2
  //  - animator produces (B) to the pipeline
  //  - applies pipeline pressure via platform thread.
  // T = 3
  //   - rasterizes finished (and un-merges the threads)
  //   - |Draw| for B yields as its on the wrong thread.
  // This enqueue ensures that we attempt to consume from the right
  // thread one more time after un-merge.
  if (raster_thread_merger_) {
    if (raster_thread_merger_->DecrementLease() ==
        fml::RasterThreadStatus::kUnmergedNow) {
      return DoDrawResult{
          .raster_status = RasterStatus::kEnqueuePipeline,
      };
    }
  }

  return DoDrawResult{
      .raster_status = raster_status,
  };
}

RasterStatus Rasterizer::DrawToSurface(
    FrameTimingsRecorder& frame_timings_recorder,
    flutter::LayerTree* layer_tree,
    float device_pixel_ratio,
    SurfaceRecord* surface_record) {
  TRACE_EVENT0("flutter", "Rasterizer::DrawToSurface");
  FML_DCHECK(surface_record);

  RasterStatus raster_status;
  if (studio_->AllowsDrawingWhenGpuDisabled()) {
    raster_status = DrawToSurfaceUnsafe(frame_timings_recorder, layer_tree,
                                        device_pixel_ratio, surface_record);
  } else {
    delegate_.GetIsGpuDisabledSyncSwitch()->Execute(
        fml::SyncSwitch::Handlers()
            .SetIfTrue([&] { raster_status = RasterStatus::kDiscarded; })
            .SetIfFalse([&] {
              raster_status =
                  DrawToSurfaceUnsafe(frame_timings_recorder, layer_tree,
                                      device_pixel_ratio, surface_record);
            }));
  }

  return raster_status;
}

/// Unsafe because it assumes we have access to the GPU which isn't the case
/// when iOS is backgrounded, for example.
/// \see Rasterizer::DrawToSurface
RasterStatus Rasterizer::DrawToSurfaceUnsafe(
    FrameTimingsRecorder& frame_timings_recorder,
    flutter::LayerTree* layer_tree,
    float device_pixel_ratio,
    SurfaceRecord* surface_record) {
  Surface* surface = surface_record->surface.get();
  FML_DCHECK(surface);

  compositor_context_->ui_time().SetLapTime(
      frame_timings_recorder.GetBuildDuration());

  DlCanvas* embedder_root_canvas = nullptr;
  if (external_view_embedder_) {
    FML_DCHECK(!external_view_embedder_->GetUsedThisFrame());
    external_view_embedder_->SetUsedThisFrame(true);
    external_view_embedder_->BeginFrame(
        layer_tree->frame_size(), studio_->GetContext(), device_pixel_ratio,
        raster_thread_merger_);
    embedder_root_canvas = external_view_embedder_->GetRootCanvas();
  }

  // On Android, the external view embedder deletes surfaces in `BeginFrame`.
  //
  // Deleting a surface also clears the GL context. Therefore, acquire the
  // frame after calling `BeginFrame` as this operation resets the GL context.
  auto frame =
      surface->AcquireFrame(surface_record->view_id, layer_tree->frame_size());
  if (frame == nullptr) {
    return RasterStatus::kFailed;
  }

  // If the external view embedder has specified an optional root surface, the
  // root surface transformation is set by the embedder instead of
  // having to apply it here.
  SkMatrix root_surface_transformation =
      embedder_root_canvas ? SkMatrix{} : surface->GetRootTransformation();

  auto root_surface_canvas =
      embedder_root_canvas ? embedder_root_canvas : frame->Canvas();

  auto compositor_frame = compositor_context_->AcquireFrame(
      studio_->GetContext(),          // skia GrContext
      root_surface_canvas,            // root surface canvas
      external_view_embedder_.get(),  // external view embedder
      root_surface_transformation,    // root surface transformation
      true,                           // instrumentation enabled
      frame->framebuffer_info()
          .supports_readback,                // surface supports pixel reads
      raster_thread_merger_,                 // thread merger
      frame->GetDisplayListBuilder().get(),  // display list builder
      studio_->GetAiksContext().get()        // aiks context
  );
  if (compositor_frame) {
    compositor_context_->raster_cache().BeginFrame();

    std::unique_ptr<FrameDamage> damage;
    // when leaf layer tracing is enabled we wish to repaint the whole frame
    // for accurate performance metrics.
    if (frame->framebuffer_info().supports_partial_repaint &&
        !layer_tree->is_leaf_layer_tracing_enabled()) {
      // Disable partial repaint if external_view_embedder_ SubmitFrame is
      // involved - ExternalViewEmbedder unconditionally clears the entire
      // surface and also partial repaint with platform view present is
      // something that still need to be figured out.
      bool force_full_repaint =
          external_view_embedder_ &&
          (!raster_thread_merger_ || raster_thread_merger_->IsMerged());

      damage = std::make_unique<FrameDamage>();
      if (frame->framebuffer_info().existing_damage && !force_full_repaint) {
        damage->SetPreviousLayerTree(surface_record->last_tree.get());
        damage->AddAdditionalDamage(*frame->framebuffer_info().existing_damage);
        damage->SetClipAlignment(
            frame->framebuffer_info().horizontal_clip_alignment,
            frame->framebuffer_info().vertical_clip_alignment);
      }
    }

    bool ignore_raster_cache = true;
    if (studio_->EnableRasterCache() &&
        !layer_tree->is_leaf_layer_tracing_enabled()) {
      ignore_raster_cache = false;
    }

    RasterStatus raster_status =
        compositor_frame->Raster(*layer_tree,          // layer tree
                                 ignore_raster_cache,  // ignore raster cache
                                 damage.get()          // frame damage
        );
    if (raster_status == RasterStatus::kFailed ||
        raster_status == RasterStatus::kSkipAndRetry) {
      return raster_status;
    }

    SurfaceFrame::SubmitInfo submit_info;
    // TODO (https://github.com/flutter/flutter/issues/105596): this can be in
    // the past and might need to get snapped to future as this frame could
    // have been resubmitted. `presentation_time` on `submit_info` is not set
    // in this case.
    const auto presentation_time = frame_timings_recorder.GetVsyncTargetTime();
    if (presentation_time > fml::TimePoint::Now()) {
      submit_info.presentation_time = presentation_time;
    }
    if (damage) {
      submit_info.frame_damage = damage->GetFrameDamage();
      submit_info.buffer_damage = damage->GetBufferDamage();
    }

    frame->set_submit_info(submit_info);

    if (external_view_embedder_ &&
        (!raster_thread_merger_ || raster_thread_merger_->IsMerged())) {
      FML_DCHECK(!frame->IsSubmitted());
      external_view_embedder_->SubmitFrame(
          studio_->GetContext(), std::move(frame), surface_record->view_id);
    } else {
      frame->Submit();
    }

    // Do not update raster cache metrics for kResubmit because that status
    // indicates that the frame was not actually painted.
    if (raster_status != RasterStatus::kResubmit) {
      compositor_context_->raster_cache().EndFrame();
    }

    if (studio_->GetContext()) {
      studio_->GetContext()->performDeferredCleanup(kSkiaCleanupExpiration);
    }

    return raster_status;
  }

  return RasterStatus::kFailed;
}

static sk_sp<SkData> ScreenshotLayerTreeAsPicture(
    flutter::LayerTree* tree,
    flutter::CompositorContext& compositor_context) {
  FML_DCHECK(tree != nullptr);
  SkPictureRecorder recorder;
  recorder.beginRecording(
      SkRect::MakeWH(tree->frame_size().width(), tree->frame_size().height()));

  SkMatrix root_surface_transformation;
  root_surface_transformation.reset();
  DlSkCanvasAdapter canvas(recorder.getRecordingCanvas());

  // TODO(amirh): figure out how to take a screenshot with embedded UIView.
  // https://github.com/flutter/flutter/issues/23435
  auto frame = compositor_context.AcquireFrame(
      nullptr, &canvas, nullptr, root_surface_transformation, false, true,
      nullptr, nullptr, nullptr);
  frame->Raster(*tree, true, nullptr);

#if defined(OS_FUCHSIA)
  SkSerialProcs procs = {0};
  procs.fImageProc = SerializeImageWithoutData;
  procs.fTypefaceProc = SerializeTypefaceWithoutData;
#else
  SkSerialProcs procs = {0};
  procs.fTypefaceProc = SerializeTypefaceWithData;
#endif

  return recorder.finishRecordingAsPicture()->serialize(&procs);
}

sk_sp<SkData> Rasterizer::ScreenshotLayerTreeAsImage(
    flutter::LayerTree* tree,
    flutter::CompositorContext& compositor_context,
    GrDirectContext* surface_context,
    bool compressed) {
  // Attempt to create a snapshot surface depending on whether we have access
  // to a valid GPU rendering context.
  std::unique_ptr<OffscreenSurface> snapshot_surface =
      std::make_unique<OffscreenSurface>(surface_context, tree->frame_size());

  if (!snapshot_surface->IsValid()) {
    FML_LOG(ERROR) << "Screenshot: unable to create snapshot surface";
    return nullptr;
  }

  // Draw the current layer tree into the snapshot surface.
  auto* canvas = snapshot_surface->GetCanvas();

  // There is no root surface transformation for the screenshot layer. Reset
  // the matrix to identity.
  SkMatrix root_surface_transformation;
  root_surface_transformation.reset();

  // snapshot_surface->makeImageSnapshot needs the GL context to be set if the
  // render context is GL. frame->Raster() pops the gl context in platforms
  // that gl context switching are used. (For example, older iOS that uses GL)
  // We reset the GL context using the context switch.
  auto context_switch = studio_->MakeRenderContextCurrent();
  if (!context_switch->GetResult()) {
    FML_LOG(ERROR) << "Screenshot: unable to make image screenshot";
    return nullptr;
  }

  auto frame = compositor_context.AcquireFrame(
      surface_context,              // skia context
      canvas,                       // canvas
      nullptr,                      // view embedder
      root_surface_transformation,  // root surface transformation
      false,                        // instrumentation enabled
      true,                         // render buffer readback supported
      nullptr,                      // thread merger
      nullptr,                      // display list builder
      nullptr                       // aiks context
  );
  canvas->Clear(DlColor::kTransparent());
  frame->Raster(*tree, true, nullptr);
  canvas->Flush();

  return snapshot_surface->GetRasterData(compressed);
}

Rasterizer::Screenshot Rasterizer::ScreenshotLastLayerTree(
    Rasterizer::ScreenshotType type,
    bool base64_encode) {
  // TODO(dkwingsmt): Probably screenshot all layer trees and put them together
  // instead of just the first one.
  SurfaceRecord* surface_record = GetFirstSurface();
  if (surface_record == nullptr || surface_record->last_tree == nullptr) {
    FML_LOG(ERROR) << "Last layer tree was null when screenshotting.";
    return {};
  }
  return ScreenshotLayerTree(type, base64_encode, *surface_record);
}

Rasterizer::Screenshot Rasterizer::ScreenshotLayerTree(
    ScreenshotType type,
    bool base64_encode,
    SurfaceRecord& surface_record) {
  auto& surface = surface_record.surface;
  auto* layer_tree = surface_record.last_tree.get();

  sk_sp<SkData> data = nullptr;
  std::string format;

  GrDirectContext* surface_context = studio_ ? studio_->GetContext() : nullptr;

  switch (type) {
    case ScreenshotType::SkiaPicture:
      format = "ScreenshotType::SkiaPicture";
      data = ScreenshotLayerTreeAsPicture(layer_tree, *compositor_context_);
      break;
    case ScreenshotType::UncompressedImage:
      format = "ScreenshotType::UncompressedImage";
      data = ScreenshotLayerTreeAsImage(layer_tree, *compositor_context_,
                                        surface_context, false);
      break;
    case ScreenshotType::CompressedImage:
      format = "ScreenshotType::CompressedImage";
      data = ScreenshotLayerTreeAsImage(layer_tree, *compositor_context_,
                                        surface_context, true);
      break;
    case ScreenshotType::SurfaceData: {
      Surface::SurfaceData surface_data = surface->GetSurfaceData();
      format = surface_data.pixel_format;
      data = surface_data.data;
      break;
    }
  }

  if (data == nullptr) {
    FML_LOG(ERROR) << "Screenshot data was null.";
    return {};
  }

  if (base64_encode) {
    size_t b64_size = SkBase64::Encode(data->data(), data->size(), nullptr);
    auto b64_data = SkData::MakeUninitialized(b64_size);
    SkBase64::Encode(data->data(), data->size(), b64_data->writable_data());
    return Rasterizer::Screenshot{b64_data, layer_tree->frame_size(), format};
  }

  return Rasterizer::Screenshot{data, layer_tree->frame_size(), format};
}

void Rasterizer::SetNextFrameCallback(const fml::closure& callback) {
  next_frame_callback_ = callback;
}

void Rasterizer::SetExternalViewEmbedder(
    const std::shared_ptr<ExternalViewEmbedder>& view_embedder) {
  external_view_embedder_ = view_embedder;
}

void Rasterizer::SetSnapshotSurfaceProducer(
    std::unique_ptr<SnapshotSurfaceProducer> producer) {
  snapshot_surface_producer_ = std::move(producer);
}

fml::RefPtr<fml::RasterThreadMerger> Rasterizer::GetRasterThreadMerger() {
  return raster_thread_merger_;
}

void Rasterizer::FireNextFrameCallbackIfPresent() {
  if (!next_frame_callback_) {
    return;
  }
  // It is safe for the callback to set a new callback.
  auto callback = next_frame_callback_;
  next_frame_callback_ = nullptr;
  callback();
}

void Rasterizer::SetResourceCacheMaxBytes(size_t max_bytes, bool from_user) {
  user_override_resource_cache_bytes_ |= from_user;

  if (!from_user && user_override_resource_cache_bytes_) {
    // We should not update the setting here if a user has explicitly set a
    // value for this over the flutter/skia channel.
    return;
  }

  max_cache_bytes_ = max_bytes;
  if (!studio_) {
    return;
  }

  GrDirectContext* context = studio_->GetContext();
  if (context) {
    auto context_switch = studio_->MakeRenderContextCurrent();
    if (!context_switch->GetResult()) {
      return;
    }

    context->setResourceCacheLimit(max_bytes);
  }
}

std::optional<size_t> Rasterizer::GetResourceCacheMaxBytes() const {
  if (!studio_) {
    return std::nullopt;
  }
  GrDirectContext* context = studio_->GetContext();
  if (context) {
    return context->getResourceCacheLimit();
  }
  return std::nullopt;
}

Rasterizer::Screenshot::Screenshot() {}

Rasterizer::Screenshot::Screenshot(sk_sp<SkData> p_data,
                                   SkISize p_size,
                                   const std::string& p_format)
    : data(std::move(p_data)), frame_size(p_size), format(p_format) {}

Rasterizer::Screenshot::Screenshot(const Screenshot& other) = default;

Rasterizer::Screenshot::~Screenshot() = default;

}  // namespace flutter
