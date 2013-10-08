// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/balloon_collection_impl.h"

#include "chrome/browser/notifications/balloon.h"
#include "chrome/browser/ui/gtk/notifications/balloon_view_gtk.h"
#include "ui/gfx/size.h"

Balloon* BalloonCollectionImpl::MakeBalloon(const Notification& notification,
                                            Profile* profile) {
  Balloon* balloon = new Balloon(notification, profile, this);

  balloon->set_view(new BalloonViewImpl(this));
  gfx::Size size(layout_.min_balloon_width(), layout_.min_balloon_height());
  balloon->set_content_size(size);
  return balloon;
}

int BalloonCollectionImpl::Layout::InterBalloonMargin() const {
  return 5;
}

int BalloonCollectionImpl::Layout::HorizontalEdgeMargin() const {
  return 5;
}

int BalloonCollectionImpl::Layout::VerticalEdgeMargin() const {
  return 5;
}

bool BalloonCollectionImpl::Layout::NeedToMoveAboveLeftSidePanels() const {
  return placement_ == VERTICALLY_FROM_BOTTOM_LEFT;
}

bool BalloonCollectionImpl::Layout::NeedToMoveAboveRightSidePanels() const {
  return placement_ == VERTICALLY_FROM_BOTTOM_RIGHT;
}

void BalloonCollectionImpl::PositionBalloons(bool reposition) {
  PositionBalloonsInternal(reposition);
}

void BalloonCollectionImpl::WillProcessEvent(GdkEvent* event) {
}

void BalloonCollectionImpl::DidProcessEvent(GdkEvent* event) {
  switch (event->type) {
    case GDK_MOTION_NOTIFY:
    case GDK_LEAVE_NOTIFY:
      HandleMouseMoveEvent();
      break;
    default:
      break;
  }
}

bool BalloonCollectionImpl::IsCursorInBalloonCollection() const {
  GdkScreen* screen = gdk_screen_get_default();
  GdkDisplay* display = gdk_screen_get_display(screen);
  gint x, y;
  gdk_display_get_pointer(display, NULL, &x, &y, NULL);

  return GetBalloonsBoundingBox().Contains(gfx::Point(x, y));
}

void BalloonCollectionImpl::SetPositionPreference(
    PositionPreference position) {
  if (position == DEFAULT_POSITION)
    position = LOWER_RIGHT;

  // All positioning schemes are vertical, and linux
  // uses the normal screen orientation.
  if (position == UPPER_RIGHT)
    layout_.set_placement(Layout::VERTICALLY_FROM_TOP_RIGHT);
  else if (position == UPPER_LEFT)
    layout_.set_placement(Layout::VERTICALLY_FROM_TOP_LEFT);
  else if (position == LOWER_LEFT)
    layout_.set_placement(Layout::VERTICALLY_FROM_BOTTOM_LEFT);
  else if (position == LOWER_RIGHT)
    layout_.set_placement(Layout::VERTICALLY_FROM_BOTTOM_RIGHT);
  else
    NOTREACHED();

  layout_.ComputeOffsetToMoveAbovePanels();
  PositionBalloons(true);
}

// static
BalloonCollection* BalloonCollection::Create() {
  return new BalloonCollectionImpl();
}
