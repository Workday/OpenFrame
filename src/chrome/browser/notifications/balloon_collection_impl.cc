// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/balloon_collection_impl.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/notifications/balloon.h"
#include "chrome/browser/notifications/balloon_host.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/panels/docked_panel_collection.h"
#include "chrome/browser/ui/panels/panel.h"
#include "chrome/browser/ui/panels/panel_manager.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/screen.h"
#include "ui/gfx/size.h"

namespace {

// Portion of the screen allotted for notifications. When notification balloons
// extend over this, no new notifications are shown until some are closed.
const double kPercentBalloonFillFactor = 0.7;

// Allow at least this number of balloons on the screen.
const int kMinAllowedBalloonCount = 2;

// Delay from the mouse leaving the balloon collection before
// there is a relayout, in milliseconds.
const int kRepositionDelayMs = 300;

// The spacing between the balloon and the panel.
const int kVerticalSpacingBetweenBalloonAndPanel = 5;

}  // namespace

BalloonCollectionImpl::BalloonCollectionImpl()
#if USE_OFFSETS
    : reposition_factory_(this),
      added_as_message_loop_observer_(false)
#endif
{
  registrar_.Add(this, chrome::NOTIFICATION_PANEL_COLLECTION_UPDATED,
                 content::NotificationService::AllSources());
  registrar_.Add(this, chrome::NOTIFICATION_PANEL_CHANGED_EXPANSION_STATE,
                 content::NotificationService::AllSources());

  SetPositionPreference(BalloonCollection::DEFAULT_POSITION);
}

BalloonCollectionImpl::~BalloonCollectionImpl() {
#if USE_OFFSETS
  RemoveMessageLoopObserver();
#endif
}

void BalloonCollectionImpl::AddImpl(const Notification& notification,
                                    Profile* profile,
                                    bool add_to_front) {
  Balloon* new_balloon = MakeBalloon(notification, profile);
  // The +1 on width is necessary because width is fixed on notifications,
  // so since we always have the max size, we would always hit the scrollbar
  // condition.  We are only interested in comparing height to maximum.
  new_balloon->set_min_scrollbar_size(gfx::Size(1 + layout_.max_balloon_width(),
                                                layout_.max_balloon_height()));
  new_balloon->SetPosition(layout_.OffScreenLocation(), false);
  new_balloon->Show();
#if USE_OFFSETS
  int count = base_.count();
  if (count > 0 && layout_.RequiresOffsets())
    new_balloon->set_offset(base_.balloons()[count - 1]->offset());
#endif
  base_.Add(new_balloon, add_to_front);
  PositionBalloons(false);

  // There may be no listener in a unit test.
  if (space_change_listener_)
    space_change_listener_->OnBalloonSpaceChanged();

  // This is used only for testing.
  if (!on_collection_changed_callback_.is_null())
    on_collection_changed_callback_.Run();
}

void BalloonCollectionImpl::Add(const Notification& notification,
                                Profile* profile) {
  AddImpl(notification, profile, false);
}

const Notification* BalloonCollectionImpl::FindById(
    const std::string& id) const {
  return base_.FindById(id);
}

bool BalloonCollectionImpl::RemoveById(const std::string& id) {
  return base_.CloseById(id);
}

bool BalloonCollectionImpl::RemoveBySourceOrigin(const GURL& origin) {
  return base_.CloseAllBySourceOrigin(origin);
}

bool BalloonCollectionImpl::RemoveByProfile(Profile* profile) {
  return base_.CloseAllByProfile(profile);
}

void BalloonCollectionImpl::RemoveAll() {
  base_.CloseAll();
}

bool BalloonCollectionImpl::HasSpace() const {
  int count = base_.count();
  if (count < kMinAllowedBalloonCount)
    return true;

  int max_balloon_size = 0;
  int total_size = 0;
  layout_.GetMaxLinearSize(&max_balloon_size, &total_size);

  int current_max_size = max_balloon_size * count;
  int max_allowed_size = static_cast<int>(total_size *
                                          kPercentBalloonFillFactor);
  return current_max_size < max_allowed_size - max_balloon_size;
}

void BalloonCollectionImpl::ResizeBalloon(Balloon* balloon,
                                          const gfx::Size& size) {
  balloon->set_content_size(Layout::ConstrainToSizeLimits(size));
  PositionBalloons(true);
}

void BalloonCollectionImpl::DisplayChanged() {
  layout_.RefreshSystemMetrics();
  PositionBalloons(true);
}

void BalloonCollectionImpl::OnBalloonClosed(Balloon* source) {
#if USE_OFFSETS
  // We want to free the balloon when finished.
  const Balloons& balloons = base_.balloons();

  Balloons::const_iterator it = balloons.begin();
  if (layout_.RequiresOffsets()) {
    gfx::Vector2d offset;
    bool apply_offset = false;
    while (it != balloons.end()) {
      if (*it == source) {
        ++it;
        if (it != balloons.end()) {
          apply_offset = true;
          offset.set_y((source)->offset().y() - (*it)->offset().y() +
              (*it)->content_size().height() - source->content_size().height());
        }
      } else {
        if (apply_offset)
          (*it)->add_offset(offset);
        ++it;
      }
    }
    // Start listening for UI events so we cancel the offset when the mouse
    // leaves the balloon area.
    if (apply_offset)
      AddMessageLoopObserver();
  }
#endif

  base_.Remove(source);
  PositionBalloons(true);

  // There may be no listener in a unit test.
  if (space_change_listener_)
    space_change_listener_->OnBalloonSpaceChanged();

  // This is used only for testing.
  if (!on_collection_changed_callback_.is_null())
    on_collection_changed_callback_.Run();
}

const BalloonCollection::Balloons& BalloonCollectionImpl::GetActiveBalloons() {
  return base_.balloons();
}

void BalloonCollectionImpl::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  gfx::Rect bounds;
  switch (type) {
    case chrome::NOTIFICATION_PANEL_COLLECTION_UPDATED:
    case chrome::NOTIFICATION_PANEL_CHANGED_EXPANSION_STATE:
      layout_.enable_computing_panel_offset();
      if (layout_.ComputeOffsetToMoveAbovePanels())
        PositionBalloons(true);
      break;
    default:
      NOTREACHED();
      break;
  }
}

void BalloonCollectionImpl::PositionBalloonsInternal(bool reposition) {
  const Balloons& balloons = base_.balloons();

  layout_.RefreshSystemMetrics();
  gfx::Point origin = layout_.GetLayoutOrigin();
  for (Balloons::const_iterator it = balloons.begin();
       it != balloons.end();
       ++it) {
    gfx::Point upper_left = layout_.NextPosition((*it)->GetViewSize(), &origin);
    (*it)->SetPosition(upper_left, reposition);
  }
}

gfx::Rect BalloonCollectionImpl::GetBalloonsBoundingBox() const {
  // Start from the layout origin.
  gfx::Rect bounds = gfx::Rect(layout_.GetLayoutOrigin(), gfx::Size(0, 0));

  // For each balloon, extend the rectangle.  This approach is indifferent to
  // the orientation of the balloons.
  const Balloons& balloons = base_.balloons();
  Balloons::const_iterator iter;
  for (iter = balloons.begin(); iter != balloons.end(); ++iter) {
    gfx::Rect balloon_box = gfx::Rect((*iter)->GetPosition(),
                                      (*iter)->GetViewSize());
    bounds.Union(balloon_box);
  }

  return bounds;
}

#if USE_OFFSETS
void BalloonCollectionImpl::AddMessageLoopObserver() {
  if (!added_as_message_loop_observer_) {
    base::MessageLoopForUI::current()->AddObserver(this);
    added_as_message_loop_observer_ = true;
  }
}

void BalloonCollectionImpl::RemoveMessageLoopObserver() {
  if (added_as_message_loop_observer_) {
    base::MessageLoopForUI::current()->RemoveObserver(this);
    added_as_message_loop_observer_ = false;
  }
}

void BalloonCollectionImpl::CancelOffsets() {
  reposition_factory_.InvalidateWeakPtrs();

  // Unhook from listening to all UI events.
  RemoveMessageLoopObserver();

  const Balloons& balloons = base_.balloons();
  for (Balloons::const_iterator it = balloons.begin();
       it != balloons.end();
       ++it)
    (*it)->set_offset(gfx::Vector2d());

  PositionBalloons(true);
}

void BalloonCollectionImpl::HandleMouseMoveEvent() {
  if (!IsCursorInBalloonCollection()) {
    // Mouse has left the region.  Schedule a reposition after
    // a short delay.
    if (!reposition_factory_.HasWeakPtrs()) {
      base::MessageLoop::current()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&BalloonCollectionImpl::CancelOffsets,
                     reposition_factory_.GetWeakPtr()),
          base::TimeDelta::FromMilliseconds(kRepositionDelayMs));
    }
  } else {
    // Mouse moved back into the region.  Cancel the reposition.
    reposition_factory_.InvalidateWeakPtrs();
  }
}
#endif

BalloonCollectionImpl::Layout::Layout()
    : placement_(INVALID),
      need_to_compute_panel_offset_(false),
      offset_to_move_above_panels_(0) {
  RefreshSystemMetrics();
}

void BalloonCollectionImpl::Layout::GetMaxLinearSize(int* max_balloon_size,
                                                     int* total_size) const {
  DCHECK(max_balloon_size && total_size);

  // All placement schemes are vertical, so we only care about height.
  *total_size = work_area_.height();
  *max_balloon_size = max_balloon_height();
}

gfx::Point BalloonCollectionImpl::Layout::GetLayoutOrigin() const {
  // For lower-left and lower-right positioning, we need to add an offset
  // to ensure balloons to stay on top of panels to avoid overlapping.
  int x = 0;
  int y = 0;
  switch (placement_) {
    case VERTICALLY_FROM_TOP_LEFT: {
      x = work_area_.x() + HorizontalEdgeMargin();
      y = work_area_.y() + VerticalEdgeMargin() + offset_to_move_above_panels_;
      break;
    }
    case VERTICALLY_FROM_TOP_RIGHT: {
      x = work_area_.right() - HorizontalEdgeMargin();
      y = work_area_.y() + VerticalEdgeMargin() + offset_to_move_above_panels_;
      break;
    }
    case VERTICALLY_FROM_BOTTOM_LEFT:
      x = work_area_.x() + HorizontalEdgeMargin();
      y = work_area_.bottom() - VerticalEdgeMargin() -
          offset_to_move_above_panels_;
      break;
    case VERTICALLY_FROM_BOTTOM_RIGHT:
      x = work_area_.right() - HorizontalEdgeMargin();
      y = work_area_.bottom() - VerticalEdgeMargin() -
          offset_to_move_above_panels_;
      break;
    default:
      NOTREACHED();
      break;
  }
  return gfx::Point(x, y);
}

gfx::Point BalloonCollectionImpl::Layout::NextPosition(
    const gfx::Size& balloon_size,
    gfx::Point* position_iterator) const {
  DCHECK(position_iterator);

  int x = 0;
  int y = 0;
  switch (placement_) {
    case VERTICALLY_FROM_TOP_LEFT:
      x = position_iterator->x();
      y = position_iterator->y();
      position_iterator->set_y(position_iterator->y() + balloon_size.height() +
                               InterBalloonMargin());
      break;
    case VERTICALLY_FROM_TOP_RIGHT:
      x = position_iterator->x() - balloon_size.width();
      y = position_iterator->y();
      position_iterator->set_y(position_iterator->y() + balloon_size.height() +
                               InterBalloonMargin());
      break;
    case VERTICALLY_FROM_BOTTOM_LEFT:
      position_iterator->set_y(position_iterator->y() - balloon_size.height() -
                               InterBalloonMargin());
      x = position_iterator->x();
      y = position_iterator->y();
      break;
    case VERTICALLY_FROM_BOTTOM_RIGHT:
      position_iterator->set_y(position_iterator->y() - balloon_size.height() -
                               InterBalloonMargin());
      x = position_iterator->x() - balloon_size.width();
      y = position_iterator->y();
      break;
    default:
      NOTREACHED();
      break;
  }
  return gfx::Point(x, y);
}

gfx::Point BalloonCollectionImpl::Layout::OffScreenLocation() const {
  gfx::Point location = GetLayoutOrigin();
  switch (placement_) {
    case VERTICALLY_FROM_TOP_LEFT:
    case VERTICALLY_FROM_BOTTOM_LEFT:
      location.Offset(0, kBalloonMaxHeight);
      break;
    case VERTICALLY_FROM_TOP_RIGHT:
    case VERTICALLY_FROM_BOTTOM_RIGHT:
      location.Offset(-kBalloonMaxWidth - BalloonView::GetHorizontalMargin(),
                      kBalloonMaxHeight);
      break;
    default:
      NOTREACHED();
      break;
  }
  return location;
}

bool BalloonCollectionImpl::Layout::RequiresOffsets() const {
  // Layout schemes that grow up from the bottom require offsets;
  // schemes that grow down do not require offsets.
  bool offsets = (placement_ == VERTICALLY_FROM_BOTTOM_LEFT ||
                  placement_ == VERTICALLY_FROM_BOTTOM_RIGHT);

#if defined(OS_MACOSX)
  // These schemes are in screen-coordinates, and top and bottom
  // are inverted on Mac.
  offsets = !offsets;
#endif

  return offsets;
}

// static
gfx::Size BalloonCollectionImpl::Layout::ConstrainToSizeLimits(
    const gfx::Size& size) {
  // restrict to the min & max sizes
  return gfx::Size(
      std::max(min_balloon_width(),
               std::min(max_balloon_width(), size.width())),
      std::max(min_balloon_height(),
               std::min(max_balloon_height(), size.height())));
}

bool BalloonCollectionImpl::Layout::ComputeOffsetToMoveAbovePanels() {
  // If the offset is not enabled due to that we have not received a
  // notification about panel, don't proceed because we don't want to call
  // PanelManager::GetInstance() to create an instance when panel is not
  // present.
  if (!need_to_compute_panel_offset_)
    return false;

  const DockedPanelCollection::Panels& panels =
      PanelManager::GetInstance()->docked_collection()->panels();
  int offset_to_move_above_panels = 0;

  // The offset is the maximum height of panels that could overlap with the
  // balloons.
  if (NeedToMoveAboveLeftSidePanels()) {
    for (DockedPanelCollection::Panels::const_reverse_iterator iter =
             panels.rbegin();
         iter != panels.rend(); ++iter) {
      // No need to check panels beyond the area occupied by the balloons.
      if ((*iter)->GetBounds().x() >= work_area_.x() + max_balloon_width())
        break;

      int current_height = (*iter)->GetBounds().height();
      if (current_height > offset_to_move_above_panels)
        offset_to_move_above_panels = current_height;
    }
  } else if (NeedToMoveAboveRightSidePanels()) {
    for (DockedPanelCollection::Panels::const_iterator iter = panels.begin();
         iter != panels.end(); ++iter) {
      // No need to check panels beyond the area occupied by the balloons.
      if ((*iter)->GetBounds().right() <=
          work_area_.right() - max_balloon_width())
        break;

      int current_height = (*iter)->GetBounds().height();
      if (current_height > offset_to_move_above_panels)
        offset_to_move_above_panels = current_height;
    }
  }

  // Ensure that we have some sort of margin between the 1st balloon and the
  // panel beneath it even the vertical edge margin is 0 as on Mac.
  if (offset_to_move_above_panels && !VerticalEdgeMargin())
    offset_to_move_above_panels += kVerticalSpacingBetweenBalloonAndPanel;

  // If no change is detected, return false to indicate that we do not need to
  // reposition balloons.
  if (offset_to_move_above_panels_ == offset_to_move_above_panels)
    return false;

  offset_to_move_above_panels_ = offset_to_move_above_panels;
  return true;
}

bool BalloonCollectionImpl::Layout::RefreshSystemMetrics() {
  bool changed = false;

#if defined(OS_MACOSX)
  gfx::Rect new_work_area = GetMacWorkArea();
#else
  // TODO(scottmg): NativeScreen is wrong. http://crbug.com/133312
  gfx::Rect new_work_area =
      gfx::Screen::GetNativeScreen()->GetPrimaryDisplay().work_area();
#endif
  if (work_area_ != new_work_area) {
    work_area_.SetRect(new_work_area.x(), new_work_area.y(),
                       new_work_area.width(), new_work_area.height());
    changed = true;
  }

  return changed;
}
