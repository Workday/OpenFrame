// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_EXO_SURFACE_H_
#define COMPONENTS_EXO_SURFACE_H_

#include <list>
#include <utility>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "ui/compositor/compositor_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace base {
namespace trace_event {
class TracedValue;
}
}

namespace exo {
class Buffer;
class SurfaceDelegate;
class SurfaceObserver;

// This class represents a rectangular area that is displayed on the screen.
// It has a location, size and pixel contents.
class Surface : public views::View, public ui::CompositorObserver {
 public:
  Surface();
  ~Surface() override;

  // Set a buffer as the content of this surface. A buffer can only be attached
  // to one surface at a time.
  void Attach(Buffer* buffer);

  // Describe the regions where the pending buffer is different from the
  // current surface contents, and where the surface therefore needs to be
  // repainted.
  void Damage(const gfx::Rect& rect);

  // Request notification when the next frame is displayed. Useful for
  // throttling redrawing operations, and driving animations.
  using FrameCallback = base::Callback<void(base::TimeTicks frame_time)>;
  void RequestFrameCallback(const FrameCallback& callback);

  // This sets the region of the surface that contains opaque content.
  void SetOpaqueRegion(const SkRegion& region);

  // Functions that control sub-surface state. All sub-surface state is
  // double-buffered and will be applied when Commit() is called.
  void AddSubSurface(Surface* sub_surface);
  void RemoveSubSurface(Surface* sub_surface);
  void SetSubSurfacePosition(Surface* sub_surface, const gfx::Point& position);
  void PlaceSubSurfaceAbove(Surface* sub_surface, Surface* reference);
  void PlaceSubSurfaceBelow(Surface* sub_surface, Surface* sibling);

  // Surface state (damage regions, attached buffers, etc.) is double-buffered.
  // A Commit() call atomically applies all pending state, replacing the
  // current state. Commit() is not guaranteed to be synchronous. See
  // CommitSurfaceHierarchy() below.
  void Commit();

  // This will synchronously commit all pending state of the surface and its
  // descendants by recursively calling CommitSurfaceHierarchy() for each
  // sub-surface with pending state.
  void CommitSurfaceHierarchy();

  // Returns true if surface is in synchronized mode.
  bool IsSynchronized() const;

  // Set the surface delegate.
  void SetSurfaceDelegate(SurfaceDelegate* delegate);

  // Returns true if surface has been assigned a surface delegate.
  bool HasSurfaceDelegate() const;

  // Surface does not own observers. It is the responsibility of the observer
  // to remove itself when it is done observing.
  void AddSurfaceObserver(SurfaceObserver* observer);
  void RemoveSurfaceObserver(SurfaceObserver* observer);
  bool HasSurfaceObserver(const SurfaceObserver* observer) const;

  // Returns a trace value representing the state of the surface.
  scoped_refptr<base::trace_event::TracedValue> AsTracedValue() const;

  bool HasPendingDamageForTesting() const { return !pending_damage_.IsEmpty(); }

  // Overridden from views::View:
  gfx::Size GetPreferredSize() const override;

  // Overridden from ui::CompositorObserver:
  void OnCompositingDidCommit(ui::Compositor* compositor) override;
  void OnCompositingStarted(ui::Compositor* compositor,
                            base::TimeTicks start_time) override;
  void OnCompositingEnded(ui::Compositor* compositor) override;
  void OnCompositingAborted(ui::Compositor* compositor) override;
  void OnCompositingLockStateChanged(ui::Compositor* compositor) override {}
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;

 private:
  bool needs_commit_surface_hierarchy() const {
    return needs_commit_surface_hierarchy_;
  }

  // This returns true when the surface has some contents assigned to it.
  bool has_contents() const { return !!current_buffer_; }

  // This is true when Attach() has been called and new contents should take
  // effect next time Commit() is called.
  bool has_pending_contents_;

  // The buffer that will become the content of surface when Commit() is called.
  base::WeakPtr<Buffer> pending_buffer_;

  // The damage region to schedule paint for when Commit() is called.
  // TODO(reveman): Use SkRegion here after adding a version of
  // ui::Layer::SchedulePaint that takes a SkRegion.
  gfx::Rect pending_damage_;

  // These lists contains the callbacks to notify the client when it is a good
  // time to start producing a new frame. These callbacks move to
  // |frame_callbacks_| when Commit() is called. Later they are moved to
  // |active_frame_callbacks_| when the effect of the Commit() is reflected in
  // the compositor's active layer tree. The callbacks fire once we're notified
  // that the compositor started drawing that active layer tree.
  std::list<FrameCallback> pending_frame_callbacks_;
  std::list<FrameCallback> frame_callbacks_;
  std::list<FrameCallback> active_frame_callbacks_;

  // The opaque region to take effect when Commit() is called.
  SkRegion pending_opaque_region_;

  // The stack of sub-surfaces to take effect when Commit() is called.
  // Bottom-most sub-surface at the front of the list and top-most sub-surface
  // at the back.
  using SubSurfaceEntry = std::pair<Surface*, gfx::Point>;
  using SubSurfaceEntryList = std::list<SubSurfaceEntry>;
  SubSurfaceEntryList pending_sub_surfaces_;

  // The buffer that is currently set as content of surface.
  base::WeakPtr<Buffer> current_buffer_;

  // This is true if a call to Commit() as been made but
  // CommitSurfaceHierarchy() has not yet been called.
  bool needs_commit_surface_hierarchy_;

  // This is true when the contents of the surface should be updated next time
  // the compositor successfully ends compositing.
  bool update_contents_after_successful_compositing_;

  // The compsitor being observer or null if not observing a compositor.
  ui::Compositor* compositor_;

  // This can be set to have some functions delegated. E.g. ShellSurface class
  // can set this to handle Commit() and apply any double buffered state it
  // maintains.
  SurfaceDelegate* delegate_;

  // Surface observer list. Surface does not own the observers.
  base::ObserverList<SurfaceObserver, true> observers_;

  DISALLOW_COPY_AND_ASSIGN(Surface);
};

}  // namespace exo

#endif  // COMPONENTS_EXO_SURFACE_H_
