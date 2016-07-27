// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TREES_PROPERTY_TREE_H_
#define CC_TREES_PROPERTY_TREE_H_

#include <vector>

#include "base/basictypes.h"
#include "cc/base/cc_export.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/scroll_offset.h"
#include "ui/gfx/transform.h"

namespace cc {

namespace proto {
class ClipNodeData;
class EffectNodeData;
class PropertyTree;
class PropertyTrees;
class TranformNodeData;
class TransformTreeData;
class TreeNode;
}

// ------------------------------*IMPORTANT*---------------------------------
// Each class declared here has a corresponding proto defined in
// cc/proto/property_tree.proto. When making any changes to a class structure
// including addition/deletion/updation of a field, please also make the
// change to its proto and the ToProtobuf and FromProtobuf methods for that
// class.

template <typename T>
struct CC_EXPORT TreeNode {
  TreeNode() : id(-1), parent_id(-1), owner_id(-1), data() {}
  int id;
  int parent_id;
  int owner_id;
  T data;

  bool operator==(const TreeNode<T>& other) const;

  void ToProtobuf(proto::TreeNode* proto) const;
  void FromProtobuf(const proto::TreeNode& proto);
};

struct CC_EXPORT TransformNodeData {
  TransformNodeData();
  ~TransformNodeData();

  // The local transform information is combined to form to_parent (ignoring
  // snapping) as follows:
  //
  //   to_parent = M_post_local * T_scroll * M_local * M_pre_local.
  //
  // The pre/post may seem odd when read LTR, but we multiply our points from
  // the right, so the pre_local matrix affects the result "first". This lines
  // up with the notions of pre/post used in skia and gfx::Transform.
  //
  // TODO(vollick): The values labeled with "will be moved..." take up a lot of
  // space, but are only necessary for animated or scrolled nodes (otherwise
  // we'll just use the baked to_parent). These values will be ultimately stored
  // directly on the transform/scroll display list items when that's possible,
  // or potentially in a scroll tree.
  //
  // TODO(vollick): will be moved when accelerated effects are implemented.
  gfx::Transform pre_local;
  gfx::Transform local;
  gfx::Transform post_local;

  gfx::Transform to_parent;

  gfx::Transform to_target;
  gfx::Transform from_target;

  gfx::Transform to_screen;
  gfx::Transform from_screen;

  int target_id;
  // This id is used for all content that draws into a render surface associated
  // with this transform node.
  int content_target_id;

  // This is the node with respect to which source_offset is defined. This will
  // not be needed once layerization moves to cc, but is needed in order to
  // efficiently update the transform tree for changes to position in the layer
  // tree.
  int source_node_id;

  // TODO(vollick): will be moved when accelerated effects are implemented.
  bool needs_local_transform_update : 1;

  bool is_invertible : 1;
  bool ancestors_are_invertible : 1;

  bool is_animated : 1;
  bool to_screen_is_animated : 1;
  bool has_only_translation_animations : 1;
  bool to_screen_has_scale_animation : 1;

  // Flattening, when needed, is only applied to a node's inherited transform,
  // never to its local transform.
  bool flattens_inherited_transform : 1;

  // This is true if the to_parent transform at every node on the path to the
  // root is flat.
  bool node_and_ancestors_are_flat : 1;

  // This is needed to know if a layer can use lcd text.
  bool node_and_ancestors_have_only_integer_translation : 1;

  bool scrolls : 1;

  bool needs_sublayer_scale : 1;

  // These are used to position nodes wrt the right or bottom of the inner or
  // outer viewport.
  bool affected_by_inner_viewport_bounds_delta_x : 1;
  bool affected_by_inner_viewport_bounds_delta_y : 1;
  bool affected_by_outer_viewport_bounds_delta_x : 1;
  bool affected_by_outer_viewport_bounds_delta_y : 1;

  // Layer scale factor is used as a fallback when we either cannot adjust
  // raster scale or if the raster scale cannot be extracted from the screen
  // space transform. For layers in the subtree of the page scale layer, the
  // layer scale factor should include the page scale factor.
  bool in_subtree_of_page_scale_layer : 1;

  // TODO(vollick): will be moved when accelerated effects are implemented.
  float post_local_scale_factor;

  // The maximum scale that that node's |local| transform will have during
  // current animations, considering only scales at keyframes not including the
  // starting keyframe of each animation.
  float local_maximum_animation_target_scale;

  // The maximum scale that this node's |local| transform will have during
  // current animatons, considering only the starting scale of each animation.
  float local_starting_animation_scale;

  // The maximum scale that this node's |to_target| transform will have during
  // current animations, considering only scales at keyframes not incuding the
  // starting keyframe of each animation.
  float combined_maximum_animation_target_scale;

  // The maximum scale that this node's |to_target| transform will have during
  // current animations, considering only the starting scale of each animation.
  float combined_starting_animation_scale;

  gfx::Vector2dF sublayer_scale;

  // TODO(vollick): will be moved when accelerated effects are implemented.
  gfx::ScrollOffset scroll_offset;

  // We scroll snap where possible, but this has an effect on scroll
  // compensation: the snap is yet more scrolling that must be compensated for.
  // This value stores the snapped amount for this purpose.
  gfx::Vector2dF scroll_snap;

  // TODO(vollick): will be moved when accelerated effects are implemented.
  gfx::Vector2dF source_offset;
  gfx::Vector2dF source_to_parent;

  bool operator==(const TransformNodeData& other) const;

  void set_to_parent(const gfx::Transform& transform) {
    to_parent = transform;
    is_invertible = to_parent.IsInvertible();
  }

  void update_pre_local_transform(const gfx::Point3F& transform_origin);

  void update_post_local_transform(const gfx::PointF& position,
                                   const gfx::Point3F& transform_origin);

  void ToProtobuf(proto::TreeNode* proto) const;
  void FromProtobuf(const proto::TreeNode& proto);
};

typedef TreeNode<TransformNodeData> TransformNode;

struct CC_EXPORT ClipNodeData {
  ClipNodeData();

  // The clip rect that this node contributes, expressed in the space of its
  // transform node.
  gfx::RectF clip;

  // Clip nodes are uses for two reasons. First, they are used for determining
  // which parts of each layer are visible. Second, they are used for
  // determining whether a clip needs to be applied when drawing a layer, and if
  // so, the rect that needs to be used. These can be different since not all
  // clips need to be applied directly to each layer. For example, a layer is
  // implicitly clipped by the bounds of its target render surface and by clips
  // applied to this surface. |combined_clip_in_target_space| is used for
  // computing visible rects, and |clip_in_target_space| is used for computing
  // clips applied at draw time. Both rects are expressed in the space of the
  // target transform node, and may include clips contributed by ancestors.
  gfx::RectF combined_clip_in_target_space;
  gfx::RectF clip_in_target_space;

  // The id of the transform node that defines the clip node's local space.
  int transform_id;

  // The id of the transform node that defines the clip node's target space.
  int target_id;

  // Whether this node contributes a new clip (that is, whether |clip| needs to
  // be applied), rather than only inheriting ancestor clips.
  bool applies_local_clip : 1;

  // When true, |clip_in_target_space| does not include clips from ancestor
  // nodes.
  bool layer_clipping_uses_only_local_clip : 1;

  // True if target surface needs to be drawn with a clip applied.
  bool target_is_clipped : 1;

  // True if layers with this clip tree node need to be drawn with a clip
  // applied.
  bool layers_are_clipped : 1;
  bool layers_are_clipped_when_surfaces_disabled : 1;

  // Nodes that correspond to unclipped surfaces disregard ancestor clips.
  bool resets_clip : 1;

  bool operator==(const ClipNodeData& other) const;

  void ToProtobuf(proto::TreeNode* proto) const;
  void FromProtobuf(const proto::TreeNode& proto);
};

typedef TreeNode<ClipNodeData> ClipNode;

struct CC_EXPORT EffectNodeData {
  EffectNodeData();

  float opacity;
  float screen_space_opacity;

  bool has_render_surface;
  int transform_id;
  int clip_id;

  bool operator==(const EffectNodeData& other) const;

  void ToProtobuf(proto::TreeNode* proto) const;
  void FromProtobuf(const proto::TreeNode& proto);
};

typedef TreeNode<EffectNodeData> EffectNode;

template <typename T>
class CC_EXPORT PropertyTree {
 public:
  PropertyTree();
  virtual ~PropertyTree();

  bool operator==(const PropertyTree<T>& other) const;

  int Insert(const T& tree_node, int parent_id);

  T* Node(int i) {
    // TODO(vollick): remove this.
    CHECK(i < static_cast<int>(nodes_.size()));
    return i > -1 ? &nodes_[i] : nullptr;
  }
  const T* Node(int i) const {
    // TODO(vollick): remove this.
    CHECK(i < static_cast<int>(nodes_.size()));
    return i > -1 ? &nodes_[i] : nullptr;
  }

  T* parent(const T* t) { return Node(t->parent_id); }
  const T* parent(const T* t) const { return Node(t->parent_id); }

  T* back() { return size() ? &nodes_[nodes_.size() - 1] : nullptr; }
  const T* back() const {
    return size() ? &nodes_[nodes_.size() - 1] : nullptr;
  }

  virtual void clear();
  size_t size() const { return nodes_.size(); }

  void set_needs_update(bool needs_update) { needs_update_ = needs_update; }
  bool needs_update() const { return needs_update_; }

  const std::vector<T>& nodes() const { return nodes_; }

  int next_available_id() const { return static_cast<int>(size()); }

  void ToProtobuf(proto::PropertyTree* proto) const;
  void FromProtobuf(const proto::PropertyTree& proto);

 private:
  // Copy and assign are permitted. This is how we do tree sync.
  std::vector<T> nodes_;

  bool needs_update_;
};

class CC_EXPORT TransformTree final : public PropertyTree<TransformNode> {
 public:
  TransformTree();
  ~TransformTree() override;

  bool operator==(const TransformTree& other) const;

  void clear() override;

  // Computes the change of basis transform from node |source_id| to |dest_id|.
  // The function returns false iff the inverse of a singular transform was
  // used (and the result should, therefore, not be trusted). Transforms may
  // be computed between any pair of nodes that have an ancestor/descendant
  // relationship. Transforms between other pairs of nodes may only be computed
  // if the following condition holds: let id1 the larger id and let id2 be the
  // other id; then the nearest ancestor of node id1 whose id is smaller than
  // id2 is the lowest common ancestor of the pair of nodes, and the transform
  // from this lowest common ancestor to node id2 is only a 2d translation.
  bool ComputeTransform(int source_id,
                        int dest_id,
                        gfx::Transform* transform) const;

  // Computes the change of basis transform from node |source_id| to |dest_id|,
  // including any sublayer scale at |dest_id|.  The function returns false iff
  // the inverse of a singular transform was used (and the result should,
  // therefore, not be trusted).
  bool ComputeTransformWithDestinationSublayerScale(
      int source_id,
      int dest_id,
      gfx::Transform* transform) const;

  // Computes the change of basis transform from node |source_id| to |dest_id|,
  // including any sublayer scale at |source_id|.  The function returns false
  // iff the inverse of a singular transform was used (and the result should,
  // therefore, not be trusted).
  bool ComputeTransformWithSourceSublayerScale(int source_id,
                                               int dest_id,
                                               gfx::Transform* transform) const;

  // Returns true iff the nodes indexed by |source_id| and |dest_id| are 2D axis
  // aligned with respect to one another.
  bool Are2DAxisAligned(int source_id, int dest_id) const;

  // Updates the parent, target, and screen space transforms and snapping.
  void UpdateTransforms(int id);

  // A TransformNode's source_to_parent value is used to account for the fact
  // that fixed-position layers are positioned by Blink wrt to their layer tree
  // parent (their "source"), but are parented in the transform tree by their
  // fixed-position container. This value needs to be updated on main-thread
  // property trees (for position changes initiated by Blink), but not on the
  // compositor thread (since the offset from a node corresponding to a
  // fixed-position layer to its fixed-position container is unaffected by
  // compositor-driven effects).
  void set_source_to_parent_updates_allowed(bool allowed) {
    source_to_parent_updates_allowed_ = allowed;
  }
  bool source_to_parent_updates_allowed() const {
    return source_to_parent_updates_allowed_;
  }

  // We store the page scale factor on the transform tree so that it can be
  // easily be retrieved and updated in UpdatePageScaleInPropertyTrees.
  void set_page_scale_factor(float page_scale_factor) {
    page_scale_factor_ = page_scale_factor;
  }
  float page_scale_factor() const { return page_scale_factor_; }

  void set_device_scale_factor(float device_scale_factor) {
    device_scale_factor_ = device_scale_factor;
  }
  float device_scale_factor() const { return device_scale_factor_; }

  void SetDeviceTransform(const gfx::Transform& transform,
                          gfx::PointF root_position);
  void SetDeviceTransformScaleFactor(const gfx::Transform& transform);
  float device_transform_scale_factor() const {
    return device_transform_scale_factor_;
  }

  void SetInnerViewportBoundsDelta(gfx::Vector2dF bounds_delta);
  gfx::Vector2dF inner_viewport_bounds_delta() const {
    return inner_viewport_bounds_delta_;
  }

  void SetOuterViewportBoundsDelta(gfx::Vector2dF bounds_delta);
  gfx::Vector2dF outer_viewport_bounds_delta() const {
    return outer_viewport_bounds_delta_;
  }

  void AddNodeAffectedByInnerViewportBoundsDelta(int node_id);
  void AddNodeAffectedByOuterViewportBoundsDelta(int node_id);

  bool HasNodesAffectedByInnerViewportBoundsDelta() const;
  bool HasNodesAffectedByOuterViewportBoundsDelta() const;

  const std::vector<int>& nodes_affected_by_inner_viewport_bounds_delta()
      const {
    return nodes_affected_by_inner_viewport_bounds_delta_;
  }
  const std::vector<int>& nodes_affected_by_outer_viewport_bounds_delta()
      const {
    return nodes_affected_by_outer_viewport_bounds_delta_;
  }

  void ToProtobuf(proto::PropertyTree* proto) const;
  void FromProtobuf(const proto::PropertyTree& proto);

 private:
  // Returns true iff the node at |desc_id| is a descendant of the node at
  // |anc_id|.
  bool IsDescendant(int desc_id, int anc_id) const;

  // Computes the combined transform between |source_id| and |dest_id| and
  // returns false if the inverse of a singular transform was used. These two
  // nodes must be on the same ancestor chain.
  bool CombineTransformsBetween(int source_id,
                                int dest_id,
                                gfx::Transform* transform) const;

  // Computes the combined inverse transform between |source_id| and |dest_id|
  // and returns false if the inverse of a singular transform was used. These
  // two nodes must be on the same ancestor chain.
  bool CombineInversesBetween(int source_id,
                              int dest_id,
                              gfx::Transform* transform) const;

  void UpdateLocalTransform(TransformNode* node);
  void UpdateScreenSpaceTransform(TransformNode* node,
                                  TransformNode* parent_node,
                                  TransformNode* target_node);
  void UpdateSublayerScale(TransformNode* node);
  void UpdateTargetSpaceTransform(TransformNode* node,
                                  TransformNode* target_node);
  void UpdateAnimationProperties(TransformNode* node,
                                 TransformNode* parent_node);
  void UndoSnapping(TransformNode* node);
  void UpdateSnapping(TransformNode* node);
  void UpdateNodeAndAncestorsHaveIntegerTranslations(
      TransformNode* node,
      TransformNode* parent_node);
  bool NeedsSourceToParentUpdate(TransformNode* node);

  bool source_to_parent_updates_allowed_;
  // When to_screen transform has perspective, the transform node's sublayer
  // scale is calculated using page scale factor, device scale factor and the
  // scale factor of device transform. So we need to store them explicitly.
  float page_scale_factor_;
  float device_scale_factor_;
  float device_transform_scale_factor_;
  gfx::Vector2dF inner_viewport_bounds_delta_;
  gfx::Vector2dF outer_viewport_bounds_delta_;
  std::vector<int> nodes_affected_by_inner_viewport_bounds_delta_;
  std::vector<int> nodes_affected_by_outer_viewport_bounds_delta_;
};

class CC_EXPORT ClipTree final : public PropertyTree<ClipNode> {
 public:
  bool operator==(const ClipTree& other) const;

  void SetViewportClip(gfx::RectF viewport_rect);
  gfx::RectF ViewportClip();

  void ToProtobuf(proto::PropertyTree* proto) const;
  void FromProtobuf(const proto::PropertyTree& proto);
};

class CC_EXPORT EffectTree final : public PropertyTree<EffectNode> {
 public:
  bool operator==(const EffectTree& other) const;

  void UpdateOpacities(int id);

  void ToProtobuf(proto::PropertyTree* proto) const;
  void FromProtobuf(const proto::PropertyTree& proto);
};

class CC_EXPORT PropertyTrees final {
 public:
  PropertyTrees();

  bool operator==(const PropertyTrees& other) const;

  void ToProtobuf(proto::PropertyTrees* proto) const;
  void FromProtobuf(const proto::PropertyTrees& proto);

  TransformTree transform_tree;
  EffectTree effect_tree;
  ClipTree clip_tree;
  bool needs_rebuild;
  bool non_root_surfaces_enabled;
  int sequence_number;
};

}  // namespace cc

#endif  // CC_TREES_PROPERTY_TREE_H_
