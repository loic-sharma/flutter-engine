// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_DISPLAY_LIST_DISPLAY_LIST_BUILDER_H_
#define FLUTTER_DISPLAY_LIST_DISPLAY_LIST_BUILDER_H_

#include "flutter/display_list/display_list.h"
#include "flutter/display_list/display_list_blend_mode.h"
#include "flutter/display_list/display_list_comparable.h"
#include "flutter/display_list/display_list_dispatcher.h"
#include "flutter/display_list/display_list_flags.h"
#include "flutter/display_list/display_list_image.h"
#include "flutter/display_list/display_list_paint.h"
#include "flutter/display_list/display_list_path_effect.h"
#include "flutter/display_list/display_list_sampling_options.h"
#include "flutter/display_list/display_list_utils.h"
#include "flutter/display_list/types.h"
#include "flutter/fml/macros.h"

namespace flutter {

// The primary class used to build a display list. The list of methods
// here matches the list of methods invoked on a |Dispatcher|.
// If there is some code that already renders to an SkCanvas object,
// those rendering commands can be captured into a DisplayList using
// the DisplayListCanvasRecorder class.
class DisplayListBuilder final : public virtual Dispatcher,
                                 public SkRefCnt,
                                 DisplayListOpFlags {
 public:
  static constexpr SkRect kMaxCullRect =
      SkRect::MakeLTRB(-1E9F, -1E9F, 1E9F, 1E9F);

  explicit DisplayListBuilder(bool prepare_rtree)
      : DisplayListBuilder(kMaxCullRect, prepare_rtree) {}

  explicit DisplayListBuilder(const SkRect& cull_rect = kMaxCullRect,
                              bool prepare_rtree = false);

  ~DisplayListBuilder();

  void setAntiAlias(bool aa) override {
    if (current_.isAntiAlias() != aa) {
      onSetAntiAlias(aa);
    }
  }
  void setDither(bool dither) override {
    if (current_.isDither() != dither) {
      onSetDither(dither);
    }
  }
  void setInvertColors(bool invert) override {
    if (current_.isInvertColors() != invert) {
      onSetInvertColors(invert);
    }
  }
  void setStrokeCap(DlStrokeCap cap) override {
    if (current_.getStrokeCap() != cap) {
      onSetStrokeCap(cap);
    }
  }
  void setStrokeJoin(DlStrokeJoin join) override {
    if (current_.getStrokeJoin() != join) {
      onSetStrokeJoin(join);
    }
  }
  void setStyle(DlDrawStyle style) override {
    if (current_.getDrawStyle() != style) {
      onSetStyle(style);
    }
  }
  void setStrokeWidth(float width) override {
    if (current_.getStrokeWidth() != width) {
      onSetStrokeWidth(width);
    }
  }
  void setStrokeMiter(float limit) override {
    if (current_.getStrokeMiter() != limit) {
      onSetStrokeMiter(limit);
    }
  }
  void setColor(DlColor color) override {
    if (current_.getColor() != color) {
      onSetColor(color);
    }
  }
  void setBlendMode(DlBlendMode mode) override {
    if (current_blender_ || current_.getBlendMode() != mode) {
      onSetBlendMode(mode);
    }
  }
  void setBlender(sk_sp<SkBlender> blender) override {
    if (!blender) {
      setBlendMode(DlBlendMode::kSrcOver);
    } else if (current_blender_ != blender) {
      onSetBlender(std::move(blender));
    }
  }
  void setColorSource(const DlColorSource* source) override {
    if (NotEquals(current_.getColorSource(), source)) {
      onSetColorSource(source);
    }
  }
  void setImageFilter(const DlImageFilter* filter) override {
    if (NotEquals(current_.getImageFilter(), filter)) {
      onSetImageFilter(filter);
    }
  }
  void setColorFilter(const DlColorFilter* filter) override {
    if (NotEquals(current_.getColorFilter(), filter)) {
      onSetColorFilter(filter);
    }
  }
  void setPathEffect(const DlPathEffect* effect) override {
    if (NotEquals(current_.getPathEffect(), effect)) {
      onSetPathEffect(effect);
    }
  }
  void setMaskFilter(const DlMaskFilter* filter) override {
    if (NotEquals(current_.getMaskFilter(), filter)) {
      onSetMaskFilter(filter);
    }
  }

  bool isAntiAlias() const { return current_.isAntiAlias(); }
  bool isDither() const { return current_.isDither(); }
  DlDrawStyle getStyle() const { return current_.getDrawStyle(); }
  DlColor getColor() const { return current_.getColor(); }
  float getStrokeWidth() const { return current_.getStrokeWidth(); }
  float getStrokeMiter() const { return current_.getStrokeMiter(); }
  DlStrokeCap getStrokeCap() const { return current_.getStrokeCap(); }
  DlStrokeJoin getStrokeJoin() const { return current_.getStrokeJoin(); }
  std::shared_ptr<const DlColorSource> getColorSource() const {
    return current_.getColorSource();
  }
  std::shared_ptr<const DlColorFilter> getColorFilter() const {
    return current_.getColorFilter();
  }
  bool isInvertColors() const { return current_.isInvertColors(); }
  std::optional<DlBlendMode> getBlendMode() const {
    if (current_blender_) {
      // The setters will turn "Mode" style blenders into "blend_mode"s
      return {};
    }
    return current_.getBlendMode();
  }
  sk_sp<SkBlender> getBlender() const {
    return current_blender_ ? current_blender_
                            : SkBlender::Mode(ToSk(current_.getBlendMode()));
  }
  std::shared_ptr<const DlPathEffect> getPathEffect() const {
    return current_.getPathEffect();
  }
  std::shared_ptr<const DlMaskFilter> getMaskFilter() const {
    return current_.getMaskFilter();
  }
  std::shared_ptr<const DlImageFilter> getImageFilter() const {
    return current_.getImageFilter();
  }

  void save() override;
  // Only the |renders_with_attributes()| option will be accepted here. Any
  // other flags will be ignored and calculated anew as the DisplayList is
  // built. Alternatively, use the |saveLayer(SkRect, bool)| method.
  void saveLayer(const SkRect* bounds,
                 const SaveLayerOptions options,
                 const DlImageFilter* backdrop) override;
  // Convenience method with just a boolean to indicate whether the saveLayer
  // should apply the rendering attributes.
  void saveLayer(const SkRect* bounds, bool renders_with_attributes) {
    saveLayer(bounds,
              renders_with_attributes ? SaveLayerOptions::kWithAttributes
                                      : SaveLayerOptions::kNoAttributes,
              nullptr);
  }
  void saveLayer(const SkRect* bounds,
                 const DlPaint* paint,
                 const DlImageFilter* backdrop = nullptr);
  void restore() override;
  int getSaveCount() { return layer_stack_.size(); }
  void restoreToCount(int restore_count);

  void translate(SkScalar tx, SkScalar ty) override;
  void scale(SkScalar sx, SkScalar sy) override;
  void rotate(SkScalar degrees) override;
  void skew(SkScalar sx, SkScalar sy) override;

  void setAttributesFromPaint(const SkPaint& paint,
                              const DisplayListAttributeFlags flags);

  // clang-format off

  // 2x3 2D affine subset of a 4x4 transform in row major order
  void transform2DAffine(SkScalar mxx, SkScalar mxy, SkScalar mxt,
                         SkScalar myx, SkScalar myy, SkScalar myt) override;
  // full 4x4 transform in row major order
  void transformFullPerspective(
      SkScalar mxx, SkScalar mxy, SkScalar mxz, SkScalar mxt,
      SkScalar myx, SkScalar myy, SkScalar myz, SkScalar myt,
      SkScalar mzx, SkScalar mzy, SkScalar mzz, SkScalar mzt,
      SkScalar mwx, SkScalar mwy, SkScalar mwz, SkScalar mwt) override;
  // clang-format on
  void transformReset() override;
  void transform(const SkMatrix* matrix);
  void transform(const SkM44* matrix44);
  void transform(const SkMatrix& matrix) { transform(&matrix); }
  void transform(const SkM44& matrix44) { transform(&matrix44); }

  /// Returns the 4x4 full perspective transform representing all transform
  /// operations executed so far in this DisplayList within the enclosing
  /// save stack.
  SkM44 getTransformFullPerspective() const { return current_layer_->matrix(); }
  /// Returns the 3x3 partial perspective transform representing all transform
  /// operations executed so far in this DisplayList within the enclosing
  /// save stack.
  SkMatrix getTransform() const { return current_layer_->matrix33(); }

  void clipRect(const SkRect& rect, SkClipOp clip_op, bool is_aa) override;
  void clipRRect(const SkRRect& rrect, SkClipOp clip_op, bool is_aa) override;
  void clipPath(const SkPath& path, SkClipOp clip_op, bool is_aa) override;

  /// Conservative estimate of the bounds of all outstanding clip operations
  /// measured in the coordinate space within which this DisplayList will
  /// be rendered.
  SkRect getDestinationClipBounds() { return current_layer_->clip_bounds(); }
  /// Conservative estimate of the bounds of all outstanding clip operations
  /// transformed into the local coordinate space in which currently
  /// recorded rendering operations are interpreted.
  SkRect getLocalClipBounds();

  /// Return true iff the supplied bounds are easily shown to be outside
  /// of the current clip bounds. This method may conservatively return
  /// false if it cannot make the determination.
  bool quickReject(const SkRect& bounds) const;

  void drawPaint() override;
  void drawPaint(const DlPaint& paint);
  void drawColor(DlColor color, DlBlendMode mode) override;
  void drawLine(const SkPoint& p0, const SkPoint& p1) override;
  void drawLine(const SkPoint& p0, const SkPoint& p1, const DlPaint& paint);
  void drawRect(const SkRect& rect) override;
  void drawRect(const SkRect& rect, const DlPaint& paint);
  void drawOval(const SkRect& bounds) override;
  void drawOval(const SkRect& bounds, const DlPaint& paint);
  void drawCircle(const SkPoint& center, SkScalar radius) override;
  void drawCircle(const SkPoint& center, SkScalar radius, const DlPaint& paint);
  void drawRRect(const SkRRect& rrect) override;
  void drawRRect(const SkRRect& rrect, const DlPaint& paint);
  void drawDRRect(const SkRRect& outer, const SkRRect& inner) override;
  void drawDRRect(const SkRRect& outer,
                  const SkRRect& inner,
                  const DlPaint& paint);
  void drawPath(const SkPath& path) override;
  void drawPath(const SkPath& path, const DlPaint& paint);
  void drawArc(const SkRect& bounds,
               SkScalar start,
               SkScalar sweep,
               bool useCenter) override;
  void drawArc(const SkRect& bounds,
               SkScalar start,
               SkScalar sweep,
               bool useCenter,
               const DlPaint& paint);
  void drawPoints(SkCanvas::PointMode mode,
                  uint32_t count,
                  const SkPoint pts[]) override;
  void drawPoints(SkCanvas::PointMode mode,
                  uint32_t count,
                  const SkPoint pts[],
                  const DlPaint& paint);
  void drawSkVertices(const sk_sp<SkVertices> vertices,
                      SkBlendMode mode) override;
  void drawVertices(const DlVertices* vertices, DlBlendMode mode) override;
  void drawVertices(const std::shared_ptr<const DlVertices> vertices,
                    DlBlendMode mode) {
    drawVertices(vertices.get(), mode);
  }
  void drawVertices(const DlVertices* vertices,
                    DlBlendMode mode,
                    const DlPaint& paint);
  void drawVertices(const std::shared_ptr<const DlVertices> vertices,
                    DlBlendMode mode,
                    const DlPaint& paint) {
    drawVertices(vertices.get(), mode, paint);
  }
  void drawImage(const sk_sp<DlImage> image,
                 const SkPoint point,
                 DlImageSampling sampling,
                 bool render_with_attributes) override;
  void drawImage(const sk_sp<DlImage>& image,
                 const SkPoint point,
                 DlImageSampling sampling,
                 const DlPaint* paint = nullptr);
  void drawImageRect(
      const sk_sp<DlImage> image,
      const SkRect& src,
      const SkRect& dst,
      DlImageSampling sampling,
      bool render_with_attributes,
      SkCanvas::SrcRectConstraint constraint =
          SkCanvas::SrcRectConstraint::kFast_SrcRectConstraint) override;
  void drawImageRect(const sk_sp<DlImage>& image,
                     const SkRect& src,
                     const SkRect& dst,
                     DlImageSampling sampling,
                     const DlPaint* paint = nullptr,
                     SkCanvas::SrcRectConstraint constraint =
                         SkCanvas::SrcRectConstraint::kFast_SrcRectConstraint);
  void drawImageNine(const sk_sp<DlImage> image,
                     const SkIRect& center,
                     const SkRect& dst,
                     DlFilterMode filter,
                     bool render_with_attributes) override;
  void drawImageNine(const sk_sp<DlImage>& image,
                     const SkIRect& center,
                     const SkRect& dst,
                     DlFilterMode filter,
                     const DlPaint* paint = nullptr);
  void drawImageLattice(const sk_sp<DlImage> image,
                        const SkCanvas::Lattice& lattice,
                        const SkRect& dst,
                        DlFilterMode filter,
                        bool render_with_attributes) override;
  void drawAtlas(const sk_sp<DlImage> atlas,
                 const SkRSXform xform[],
                 const SkRect tex[],
                 const DlColor colors[],
                 int count,
                 DlBlendMode mode,
                 DlImageSampling sampling,
                 const SkRect* cullRect,
                 bool render_with_attributes) override;
  void drawAtlas(const sk_sp<DlImage>& atlas,
                 const SkRSXform xform[],
                 const SkRect tex[],
                 const DlColor colors[],
                 int count,
                 DlBlendMode mode,
                 DlImageSampling sampling,
                 const SkRect* cullRect,
                 const DlPaint* paint = nullptr);
  void drawPicture(const sk_sp<SkPicture> picture,
                   const SkMatrix* matrix,
                   bool render_with_attributes) override;
  void drawDisplayList(const sk_sp<DisplayList> display_list) override;
  void drawTextBlob(const sk_sp<SkTextBlob> blob,
                    SkScalar x,
                    SkScalar y) override;
  void drawTextBlob(const sk_sp<SkTextBlob>& blob,
                    SkScalar x,
                    SkScalar y,
                    const DlPaint& paint);
  void drawShadow(const SkPath& path,
                  const DlColor color,
                  const SkScalar elevation,
                  bool transparent_occluder,
                  SkScalar dpr) override;

  sk_sp<DisplayList> Build();

 private:
  void checkForDeferredSave();

  SkAutoTMalloc<uint8_t> storage_;
  size_t used_ = 0;
  size_t allocated_ = 0;
  int op_count_ = 0;

  // bytes and ops from |drawPicture| and |drawDisplayList|
  size_t nested_bytes_ = 0;
  int nested_op_count_ = 0;

  template <typename T, typename... Args>
  void* Push(size_t extra, int op_inc, Args&&... args);

  void setAttributesFromDlPaint(const DlPaint& paint,
                                const DisplayListAttributeFlags flags);
  void intersect(const SkRect& rect);

  // kInvalidSigma is used to indicate that no MaskBlur is currently set.
  static constexpr SkScalar kInvalidSigma = 0.0;
  static bool mask_sigma_valid(SkScalar sigma) {
    return SkScalarIsFinite(sigma) && sigma > 0.0;
  }

  class LayerInfo {
   public:
    explicit LayerInfo(const SkM44& matrix,
                       const SkMatrix& matrix33,
                       const SkRect& clip_bounds,
                       size_t save_layer_offset = 0,
                       bool has_layer = false,
                       std::shared_ptr<const DlImageFilter> filter = nullptr)
        : save_layer_offset_(save_layer_offset),
          has_layer_(has_layer),
          cannot_inherit_opacity_(false),
          has_compatible_op_(false),
          matrix_(matrix),
          matrix33_(matrix33),
          clip_bounds_(clip_bounds),
          filter_(filter),
          is_unbounded_(false) {}

    explicit LayerInfo(const LayerInfo* current_layer,
                       size_t save_layer_offset = 0,
                       bool has_layer = false,
                       std::shared_ptr<const DlImageFilter> filter = nullptr)
        : LayerInfo(current_layer->matrix_,
                    current_layer->matrix33_,
                    current_layer->clip_bounds_,
                    save_layer_offset,
                    has_layer,
                    filter) {}

    // The offset into the memory buffer where the saveLayer DLOp record
    // for this saveLayer() call is placed. This may be needed if the
    // eventual restore() call has discovered important information about
    // the records inside the saveLayer that may impact how the saveLayer
    // is handled (e.g., |cannot_inherit_opacity| == false).
    // This offset is only valid if |has_layer| is true.
    size_t save_layer_offset() const { return save_layer_offset_; }

    bool has_layer() const { return has_layer_; }
    bool cannot_inherit_opacity() const { return cannot_inherit_opacity_; }
    bool has_compatible_op() const { return cannot_inherit_opacity_; }
    SkM44& matrix() { return matrix_; }
    SkMatrix& matrix33() { return matrix33_; }
    SkRect& clip_bounds() { return clip_bounds_; }

    void update_matrix33() { matrix33_ = matrix_.asM33(); }

    bool is_group_opacity_compatible() const {
      return !cannot_inherit_opacity_;
    }

    void mark_incompatible() { cannot_inherit_opacity_ = true; }

    // For now this only allows a single compatible op to mark the
    // layer as being compatible with group opacity. If we start
    // computing bounds of ops in the Builder methods then we
    // can upgrade this to checking for overlapping ops.
    // See https://github.com/flutter/flutter/issues/93899
    void add_compatible_op() {
      if (!cannot_inherit_opacity_) {
        if (has_compatible_op_) {
          cannot_inherit_opacity_ = true;
        } else {
          has_compatible_op_ = true;
        }
      }
    }

    // The filter to apply to the layer bounds when it is restored
    std::shared_ptr<const DlImageFilter> filter() { return filter_; }

    // is_unbounded should be set to true if we ever encounter an operation
    // on a layer that either is unrestricted (|drawColor| or |drawPaint|)
    // or cannot compute its bounds (some effects and filters) and there
    // was no outstanding clip op at the time.
    // When the layer is restored, the outer layer may then process this
    // unbounded state by accumulating its own clip or transferring the
    // unbounded state to its own outer layer.
    // Typically the DisplayList will have been constructed with a cull
    // rect which will act as a default clip for the outermost layer and
    // the unbounded state of all sub layers will eventually be caught by
    // that cull rect so that the overall unbounded state of the entire
    // DisplayList will never be true.
    //
    // SkPicture treats these same conditions as a Nop (they accumulate
    // the SkPicture cull rect, but if it was not specified then it is an
    // empty Rect and so has no effect on the bounds).
    //
    // Flutter is unlikely to ever run into this as the Dart mechanisms
    // all supply a non-null cull rect for all Dart Picture objects,
    // even if that cull rect is kGiantRect.
    void set_unbounded() { is_unbounded_ = true; }

    // |is_unbounded| should be called after |getLayerBounds| in case
    // a problem was found during the computation of those bounds,
    // the layer will have one last chance to flag an unbounded state.
    bool is_unbounded() const { return is_unbounded_; }

   private:
    size_t save_layer_offset_;
    bool has_layer_;
    bool cannot_inherit_opacity_;
    bool has_compatible_op_;
    SkM44 matrix_;
    SkMatrix matrix33_;
    SkRect clip_bounds_;
    std::shared_ptr<const DlImageFilter> filter_;
    bool is_unbounded_;
    bool has_deferred_save_op_ = false;

    friend class DisplayListBuilder;
  };

  std::vector<LayerInfo> layer_stack_;
  LayerInfo* current_layer_;
  std::unique_ptr<BoundsAccumulator> accumulator_;
  BoundsAccumulator* accumulator() { return accumulator_.get(); }

  // This flag indicates whether or not the current rendering attributes
  // are compatible with rendering ops applying an inherited opacity.
  bool current_opacity_compatibility_ = true;

  // Returns the compatibility of a given blend mode for applying an
  // inherited opacity value to modulate the visibility of the op.
  // For now we only accept SrcOver blend modes but this could be expanded
  // in the future to include other (rarely used) modes that also modulate
  // the opacity of a rendering operation at the cost of a switch statement
  // or lookup table.
  static bool IsOpacityCompatible(DlBlendMode mode) {
    return (mode == DlBlendMode::kSrcOver);
  }

  void UpdateCurrentOpacityCompatibility() {
    current_opacity_compatibility_ =             //
        current_.getColorFilter() == nullptr &&  //
        !current_.isInvertColors() &&            //
        current_blender_ == nullptr &&           //
        IsOpacityCompatible(current_.getBlendMode());
  }

  // Update the opacity compatibility flags of the current layer for an op
  // that has determined its compatibility as indicated by |compatible|.
  void UpdateLayerOpacityCompatibility(bool compatible) {
    if (compatible) {
      current_layer_->add_compatible_op();
    } else {
      current_layer_->mark_incompatible();
    }
  }

  // Check for opacity compatibility for an op that may or may not use the
  // current rendering attributes as indicated by |uses_blend_attribute|.
  // If the flag is false then the rendering op will be able to substitute
  // a default Paint object with the opacity applied using the default SrcOver
  // blend mode which is always compatible with applying an inherited opacity.
  void CheckLayerOpacityCompatibility(bool uses_blend_attribute = true) {
    UpdateLayerOpacityCompatibility(!uses_blend_attribute ||
                                    current_opacity_compatibility_);
  }

  void CheckLayerOpacityHairlineCompatibility() {
    UpdateLayerOpacityCompatibility(
        current_opacity_compatibility_ &&
        (current_.getDrawStyle() == DlDrawStyle::kFill ||
         current_.getStrokeWidth() > 0));
  }

  // Check for opacity compatibility for an op that ignores the current
  // attributes and uses the indicated blend |mode| to render to the layer.
  // This is only used by |drawColor| currently.
  void CheckLayerOpacityCompatibility(DlBlendMode mode) {
    UpdateLayerOpacityCompatibility(IsOpacityCompatible(mode));
  }

  void onSetAntiAlias(bool aa);
  void onSetDither(bool dither);
  void onSetInvertColors(bool invert);
  void onSetStrokeCap(DlStrokeCap cap);
  void onSetStrokeJoin(DlStrokeJoin join);
  void onSetStyle(DlDrawStyle style);
  void onSetStrokeWidth(SkScalar width);
  void onSetStrokeMiter(SkScalar limit);
  void onSetColor(DlColor color);
  void onSetBlendMode(DlBlendMode mode);
  void onSetBlender(sk_sp<SkBlender> blender);
  void onSetColorSource(const DlColorSource* source);
  void onSetImageFilter(const DlImageFilter* filter);
  void onSetColorFilter(const DlColorFilter* filter);
  void onSetPathEffect(const DlPathEffect* effect);
  void onSetMaskFilter(const DlMaskFilter* filter);

  // The DisplayList had an unbounded call with no cull rect or clip
  // to contain it. Should only be called after the stream is fully
  // built.
  // Unbounded operations are calls like |drawColor| which are defined
  // to flood the entire surface, or calls that relied on a rendering
  // attribute which is unable to compute bounds (should be rare).
  // In those cases the bounds will represent only the accumulation
  // of the bounded calls and this flag will be set to indicate that
  // condition.
  bool is_unbounded() const {
    FML_DCHECK(layer_stack_.size() == 1);
    return layer_stack_.front().is_unbounded();
  }

  SkRect bounds() const {
    FML_DCHECK(layer_stack_.size() == 1);
    if (is_unbounded()) {
      FML_LOG(INFO) << "returning partial bounds for unbounded DisplayList";
    }

    return accumulator_->bounds();
  }

  sk_sp<DlRTree> rtree() {
    FML_DCHECK(layer_stack_.size() == 1);
    if (is_unbounded()) {
      FML_LOG(INFO) << "returning partial rtree for unbounded DisplayList";
    }

    return accumulator_->rtree();
  }

  bool paint_nops_on_transparency();

  // Computes the bounds of an operation adjusted for a given ImageFilter
  static bool ComputeFilteredBounds(SkRect& bounds,
                                    const DlImageFilter* filter);

  // Adjusts the indicated bounds for the given flags and returns true if
  // the calculation was possible, or false if it could not be estimated.
  bool AdjustBoundsForPaint(SkRect& bounds, DisplayListAttributeFlags flags);

  // Records the fact that we encountered an op that either could not
  // estimate its bounds or that fills all of the destination space.
  void AccumulateUnbounded();

  // Records the bounds for an op after modifying them according to the
  // supplied attribute flags and transforming by the current matrix.
  void AccumulateOpBounds(const SkRect& bounds,
                          DisplayListAttributeFlags flags) {
    SkRect safe_bounds = bounds;
    AccumulateOpBounds(safe_bounds, flags);
  }

  // Records the bounds for an op after modifying them according to the
  // supplied attribute flags and transforming by the current matrix
  // and clipping against the current clip.
  void AccumulateOpBounds(SkRect& bounds, DisplayListAttributeFlags flags);

  // Records the given bounds after transforming by the current matrix
  // and clipping against the current clip.
  void AccumulateBounds(SkRect& bounds);

  DlPaint current_;
  // If |current_blender_| is set then ignore |current_.getBlendMode()|
  sk_sp<SkBlender> current_blender_;
};

}  // namespace flutter

#endif  // FLUTTER_DISPLAY_LIST_DISPLAY_LIST_BUILDER_H_
