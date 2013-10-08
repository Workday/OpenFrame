// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_started_animation.h"

#include <math.h>

#include <gtk/gtk.h>

#include "base/message_loop/message_loop.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/theme_resources.h"
#include "ui/base/animation/linear_animation.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/rect.h"

using content::WebContents;

namespace {

// How long to spend moving downwards and fading out after waiting.
const int kMoveTimeMs = 600;

// The animation framerate.
const int kFrameRateHz = 60;

// What fraction of the frame height to move downward from the frame center.
// Note that setting this greater than 0.5 will mean moving past the bottom of
// the frame.
const double kMoveFraction = 1.0 / 3.0;

class DownloadStartedAnimationGtk : public ui::LinearAnimation,
                                    public content::NotificationObserver {
 public:
  explicit DownloadStartedAnimationGtk(WebContents* web_contents);

  // DownloadStartedAnimation will delete itself, but this is public so
  // that we can use DeleteSoon().
  virtual ~DownloadStartedAnimationGtk();

 private:
  // Move the arrow to wherever it should currently be.
  void Reposition();

  // Shut down cleanly.
  void Close();

  // Animation implementation.
  virtual void AnimateToState(double state) OVERRIDE;

  // NotificationObserver
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // The top level window that floats over the browser and displays the
  // image.
  GtkWidget* popup_;

  // Dimensions of the image.
  int width_;
  int height_;

  // The content area holding us.
  WebContents* web_contents_;

  // The content area at the start of the animation. We store this so that the
  // download shelf's resizing of the content area doesn't cause the animation
  // to move around. This means that once started, the animation won't move
  // with the parent window, but it's so fast that this shouldn't cause too
  // much heartbreak.
  gfx::Rect web_contents_bounds_;

  // A scoped container for notification registries.
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(DownloadStartedAnimationGtk);
};

DownloadStartedAnimationGtk::DownloadStartedAnimationGtk(
    WebContents* web_contents)
    : ui::LinearAnimation(kMoveTimeMs, kFrameRateHz, NULL),
      popup_(NULL),
      web_contents_(web_contents) {
  static GdkPixbuf* kDownloadImage = NULL;
  if (!kDownloadImage) {
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    kDownloadImage = rb.GetNativeImageNamed(
        IDR_DOWNLOAD_ANIMATION_BEGIN).ToGdkPixbuf();
  }

  width_ = gdk_pixbuf_get_width(kDownloadImage);
  height_ = gdk_pixbuf_get_height(kDownloadImage);

  // If we're too small to show the download image, then don't bother -
  // the shelf will be enough.
  web_contents_->GetView()->GetContainerBounds(&web_contents_bounds_);
  if (web_contents_bounds_.height() < height_)
    return;

  registrar_.Add(
      this,
      content::NOTIFICATION_WEB_CONTENTS_VISIBILITY_CHANGED,
      content::Source<WebContents>(web_contents_));
  registrar_.Add(
      this,
      content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
      content::Source<WebContents>(web_contents_));

  // TODO(estade): don't show up on the wrong virtual desktop.

  popup_ = gtk_window_new(GTK_WINDOW_POPUP);
  GtkWidget* image = gtk_image_new_from_pixbuf(kDownloadImage);
  gtk_container_add(GTK_CONTAINER(popup_), image);

  // Set the shape of the window to that of the arrow. Areas with
  // opacity less than 0xff (i.e. <100% opacity) will be transparent.
  GdkBitmap* mask = gdk_pixmap_new(NULL, width_, height_, 1);
  gdk_pixbuf_render_threshold_alpha(kDownloadImage, mask,
                                    0, 0,
                                    0, 0, -1, -1,
                                    0xff);
  gtk_widget_shape_combine_mask(popup_, mask, 0, 0);
  g_object_unref(mask);

  Reposition();
  gtk_widget_show_all(popup_);
  // Make sure our window has focus, is brought to the top, etc.
  gtk_window_present(GTK_WINDOW(popup_));

  Start();
}

DownloadStartedAnimationGtk::~DownloadStartedAnimationGtk() {
}

void DownloadStartedAnimationGtk::Reposition() {
  if (!web_contents_)
    return;

  DCHECK(popup_);

  // Align the image with the bottom left of the web contents (so that it
  // points to the newly created download).
  gtk_window_move(GTK_WINDOW(popup_),
      web_contents_bounds_.x(),
      static_cast<int>(web_contents_bounds_.bottom() -
          height_ - height_ * (1 - GetCurrentValue())));
}

void DownloadStartedAnimationGtk::Close() {
  if (!web_contents_)
    return;

  DCHECK(popup_);

  registrar_.Remove(
      this,
      content::NOTIFICATION_WEB_CONTENTS_VISIBILITY_CHANGED,
      content::Source<WebContents>(web_contents_));
  registrar_.Remove(
      this,
      content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
      content::Source<WebContents>(web_contents_));

  web_contents_ = NULL;
  gtk_widget_destroy(popup_);
  base::MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}

void DownloadStartedAnimationGtk::AnimateToState(double state) {
  if (!web_contents_)
    return;

  if (state >= 1.0) {
    Close();
  } else {
    Reposition();

    // Start at zero, peak halfway and end at zero.
    double opacity = std::min(1.0 - pow(GetCurrentValue() - 0.5, 2) * 4.0,
                              static_cast<double>(1.0));

    // This only works when there's a compositing manager running. Oh well.
    gtk_window_set_opacity(GTK_WINDOW(popup_), opacity);
  }
}

void DownloadStartedAnimationGtk::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == content::NOTIFICATION_WEB_CONTENTS_VISIBILITY_CHANGED) {
    bool visible = *content::Details<bool>(details).ptr();
    if (visible)
      return;
  }
  Close();
}

}  // namespace

// static
void DownloadStartedAnimation::Show(WebContents* web_contents) {
  // The animation will delete itself.
  new DownloadStartedAnimationGtk(web_contents);
}
