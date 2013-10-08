// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/download/download_shelf_gtk.h"

#include <string>

#include "base/bind.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/download/download_stats.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/gtk/browser_window_gtk.h"
#include "chrome/browser/ui/gtk/custom_button.h"
#include "chrome/browser/ui/gtk/download/download_item_gtk.h"
#include "chrome/browser/ui/gtk/gtk_chrome_link_button.h"
#include "chrome/browser/ui/gtk/gtk_chrome_shrinkable_hbox.h"
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/page_navigator.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/gtk/gtk_screen_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/gtk_util.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/insets.h"
#include "ui/gfx/point.h"
#include "ui/gfx/rect.h"

namespace {

// The height of the download items.
const int kDownloadItemHeight = DownloadShelf::kSmallProgressIconSize;

// Padding between the download widgets.
const int kDownloadItemPadding = 10;

// Padding between the top/bottom of the download widgets and the edge of the
// shelf.
const int kTopBottomPadding = 4;

// Padding between the left side of the shelf and the first download item.
const int kLeftPadding = 2;

// Padding between the right side of the shelf and the close button.
const int kRightPadding = 10;

// Speed of the shelf show/hide animation.
const int kShelfAnimationDurationMs = 120;

// The time between when the user mouses out of the download shelf zone and
// when the shelf closes (when auto-close is enabled).
const int kAutoCloseDelayMs = 300;

// The area to the top of the shelf that is considered part of its "zone".
const int kShelfAuraSize = 40;

}  // namespace

using content::DownloadItem;

DownloadShelfGtk::DownloadShelfGtk(Browser* browser, GtkWidget* parent)
    : browser_(browser),
      is_showing_(false),
      theme_service_(GtkThemeService::GetFrom(browser->profile())),
      close_on_mouse_out_(false),
      mouse_in_shelf_(false),
      weak_factory_(this) {
  // Logically, the shelf is a vbox that contains two children: a one pixel
  // tall event box, which serves as the top border, and an hbox, which holds
  // the download items and other shelf widgets (close button, show-all-
  // downloads link).
  // To make things pretty, we have to add a few more widgets. To get padding
  // right, we stick the hbox in an alignment. We put that alignment in an
  // event box so we can color the background.

  // Create the top border.
  top_border_ = gtk_event_box_new();
  gtk_widget_set_size_request(GTK_WIDGET(top_border_), 0, 1);

  // Create |items_hbox_|. We use GtkChromeShrinkableHBox, so that download
  // items can be hid automatically when there is no enough space to show them.
  items_hbox_.Own(gtk_chrome_shrinkable_hbox_new(
      TRUE, FALSE, kDownloadItemPadding));
  // We want the download shelf to be horizontally shrinkable, so that the
  // Chrome window can be resized freely even with many download items.
  gtk_widget_set_size_request(items_hbox_.get(), 0, kDownloadItemHeight);

  // Create a hbox that holds |items_hbox_| and other shelf widgets.
  GtkWidget* outer_hbox = gtk_hbox_new(FALSE, kDownloadItemPadding);

  // Pack the |items_hbox_| in the outer hbox.
  gtk_box_pack_start(GTK_BOX(outer_hbox), items_hbox_.get(), TRUE, TRUE, 0);

  // Get the padding and background color for |outer_hbox| right.
  GtkWidget* padding = gtk_alignment_new(0, 0, 1, 1);
  // Subtract 1 from top spacing to account for top border.
  gtk_alignment_set_padding(GTK_ALIGNMENT(padding),
      kTopBottomPadding - 1, kTopBottomPadding, kLeftPadding, kRightPadding);
  padding_bg_ = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(padding_bg_), padding);
  gtk_container_add(GTK_CONTAINER(padding), outer_hbox);

  GtkWidget* vbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), top_border_, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), padding_bg_, FALSE, FALSE, 0);

  // Put the shelf in an event box so it gets its own window, which makes it
  // easier to get z-ordering right.
  shelf_.Own(gtk_event_box_new());
  gtk_container_add(GTK_CONTAINER(shelf_.get()), vbox);

  // Create and pack the close button.
  close_button_.reset(CustomDrawButton::CloseButtonBar(theme_service_));
  gtk_util::CenterWidgetInHBox(outer_hbox, close_button_->widget(), true, 0);
  g_signal_connect(close_button_->widget(), "clicked",
                   G_CALLBACK(OnButtonClickThunk), this);

  // Create the "Show all downloads..." link and connect to the click event.
  link_button_ = theme_service_->BuildChromeLinkButton(
      l10n_util::GetStringUTF8(IDS_SHOW_ALL_DOWNLOADS));
  g_signal_connect(link_button_, "clicked",
                   G_CALLBACK(OnButtonClickThunk), this);
  gtk_util::SetButtonTriggersNavigation(link_button_);
  // Until we switch to vector graphics, force the font size.
  // 13.4px == 10pt @ 96dpi
  gtk_util::ForceFontSizePixels(GTK_CHROME_LINK_BUTTON(link_button_)->label,
                                13.4);

  // Make the download arrow icon.
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  GtkWidget* download_image = gtk_image_new_from_pixbuf(
      rb.GetNativeImageNamed(IDR_DOWNLOADS_FAVICON).ToGdkPixbuf());

  // Pack the link and the icon in outer hbox.
  gtk_util::CenterWidgetInHBox(outer_hbox, link_button_, true, 0);
  gtk_util::CenterWidgetInHBox(outer_hbox, download_image, true, 0);

  slide_widget_.reset(new SlideAnimatorGtk(shelf_.get(),
                                           SlideAnimatorGtk::UP,
                                           kShelfAnimationDurationMs,
                                           false, true, this));

  theme_service_->InitThemesFor(this);
  registrar_.Add(this, chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
                 content::Source<ThemeService>(theme_service_));

  gtk_widget_show_all(shelf_.get());

  // Stick ourselves at the bottom of the parent browser.
  gtk_box_pack_end(GTK_BOX(parent), slide_widget_->widget(),
                   FALSE, FALSE, 0);
  // Make sure we are at the very end.
  gtk_box_reorder_child(GTK_BOX(parent), slide_widget_->widget(), 0);
}

DownloadShelfGtk::~DownloadShelfGtk() {
  for (std::vector<DownloadItemGtk*>::iterator iter = download_items_.begin();
       iter != download_items_.end(); ++iter) {
    delete *iter;
  }

  shelf_.Destroy();
  items_hbox_.Destroy();

  // Make sure we're no longer an observer of the message loop.
  SetCloseOnMouseOut(false);
}

content::PageNavigator* DownloadShelfGtk::GetNavigator() {
  return browser_;
}

void DownloadShelfGtk::DoAddDownload(DownloadItem* download) {
  download_items_.push_back(new DownloadItemGtk(this, download));
}

bool DownloadShelfGtk::IsShowing() const {
  return slide_widget_->IsShowing();
}

bool DownloadShelfGtk::IsClosing() const {
  return slide_widget_->IsClosing();
}

void DownloadShelfGtk::DoShow() {
  slide_widget_->Open();
  browser_->UpdateDownloadShelfVisibility(true);
  CancelAutoClose();
}

void DownloadShelfGtk::DoClose(CloseReason reason) {
  // When we are closing, we can vertically overlap the render view. Make sure
  // we are on top.
  gdk_window_raise(gtk_widget_get_window(shelf_.get()));
  slide_widget_->Close();
  browser_->UpdateDownloadShelfVisibility(false);
  int num_in_progress = 0;
  for (size_t i = 0; i < download_items_.size(); ++i) {
    if (download_items_[i]->download()->GetState() == DownloadItem::IN_PROGRESS)
      ++num_in_progress;
  }
  RecordDownloadShelfClose(
      download_items_.size(), num_in_progress, reason == AUTOMATIC);
  SetCloseOnMouseOut(false);
}

Browser* DownloadShelfGtk::browser() const {
  return browser_;
}

void DownloadShelfGtk::Closed() {
  // Don't remove completed downloads if the shelf is just being auto-hidden
  // rather than explicitly closed by the user.
  if (is_hidden())
    return;
  // When the close animation is complete, remove all completed downloads.
  size_t i = 0;
  while (i < download_items_.size()) {
    DownloadItem* download = download_items_[i]->download();
    DownloadItem::DownloadState state = download->GetState();
    bool is_transfer_done = state == DownloadItem::COMPLETE ||
                            state == DownloadItem::CANCELLED ||
                            state == DownloadItem::INTERRUPTED;
    if (is_transfer_done && !download->IsDangerous()) {
      RemoveDownloadItem(download_items_[i]);
    } else {
      // We set all remaining items as "opened", so that the shelf will auto-
      // close in the future without the user clicking on them.
      download->SetOpened(true);
      ++i;
    }
  }
}

void DownloadShelfGtk::Observe(int type,
                               const content::NotificationSource& source,
                               const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_BROWSER_THEME_CHANGED) {
    GdkColor color = theme_service_->GetGdkColor(
        ThemeProperties::COLOR_TOOLBAR);
    gtk_widget_modify_bg(padding_bg_, GTK_STATE_NORMAL, &color);

    color = theme_service_->GetBorderColor();
    gtk_widget_modify_bg(top_border_, GTK_STATE_NORMAL, &color);

    // When using a non-standard, non-gtk theme, we make the link color match
    // the bookmark text color. Otherwise, standard link blue can look very
    // bad for some dark themes.
    bool use_default_color = theme_service_->GetColor(
        ThemeProperties::COLOR_BOOKMARK_TEXT) ==
        ThemeProperties::GetDefaultColor(
            ThemeProperties::COLOR_BOOKMARK_TEXT);
    GdkColor bookmark_color = theme_service_->GetGdkColor(
        ThemeProperties::COLOR_BOOKMARK_TEXT);
    gtk_chrome_link_button_set_normal_color(
        GTK_CHROME_LINK_BUTTON(link_button_),
        use_default_color ? NULL : &bookmark_color);

    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    close_button_->SetBackground(
        theme_service_->GetColor(ThemeProperties::COLOR_TAB_TEXT),
        rb.GetImageNamed(IDR_CLOSE_1).AsBitmap(),
        rb.GetImageNamed(IDR_CLOSE_1_MASK).AsBitmap());
  }
}

int DownloadShelfGtk::GetHeight() const {
  GtkAllocation allocation;
  gtk_widget_get_allocation(slide_widget_->widget(), &allocation);
  return allocation.height;
}

void DownloadShelfGtk::RemoveDownloadItem(DownloadItemGtk* download_item) {
  DCHECK(download_item);
  std::vector<DownloadItemGtk*>::iterator i =
      find(download_items_.begin(), download_items_.end(), download_item);
  DCHECK(i != download_items_.end());
  download_items_.erase(i);
  delete download_item;
  if (download_items_.empty()) {
    slide_widget_->CloseWithoutAnimation();
    browser_->UpdateDownloadShelfVisibility(false);
  } else {
    AutoCloseIfPossible();
  }
}

GtkWidget* DownloadShelfGtk::GetHBox() const {
  return items_hbox_.get();
}

void DownloadShelfGtk::MaybeShowMoreDownloadItems() {
  // Show all existing download items. It'll trigger "size-allocate" signal,
  // which will hide download items that don't have enough space to show.
  gtk_widget_show_all(items_hbox_.get());
}

void DownloadShelfGtk::OnButtonClick(GtkWidget* button) {
  if (button == close_button_->widget()) {
    Close(USER_ACTION);
  } else {
    // The link button was clicked.
    chrome::ShowDownloads(browser_);
  }
}

void DownloadShelfGtk::AutoCloseIfPossible() {
  for (std::vector<DownloadItemGtk*>::iterator iter = download_items_.begin();
       iter != download_items_.end(); ++iter) {
    if (!(*iter)->download()->GetOpened())
      return;
  }

  SetCloseOnMouseOut(true);
}

void DownloadShelfGtk::CancelAutoClose() {
  SetCloseOnMouseOut(false);
  weak_factory_.InvalidateWeakPtrs();
}

void DownloadShelfGtk::ItemOpened() {
  AutoCloseIfPossible();
}

void DownloadShelfGtk::SetCloseOnMouseOut(bool close) {
  if (close_on_mouse_out_ == close)
    return;

  close_on_mouse_out_ = close;
  mouse_in_shelf_ = close;
  if (close)
    base::MessageLoopForUI::current()->AddObserver(this);
  else
    base::MessageLoopForUI::current()->RemoveObserver(this);
}

void DownloadShelfGtk::WillProcessEvent(GdkEvent* event) {
}

void DownloadShelfGtk::DidProcessEvent(GdkEvent* event) {
  gfx::Point cursor_screen_coords;

  switch (event->type) {
    case GDK_MOTION_NOTIFY:
      cursor_screen_coords =
          gfx::Point(event->motion.x_root, event->motion.y_root);
      break;
    case GDK_LEAVE_NOTIFY:
      cursor_screen_coords =
          gfx::Point(event->crossing.x_root, event->crossing.y_root);
      break;
    default:
      return;
  }

  bool mouse_in_shelf = IsCursorInShelfZone(cursor_screen_coords);
  if (mouse_in_shelf == mouse_in_shelf_)
    return;
  mouse_in_shelf_ = mouse_in_shelf;

  if (mouse_in_shelf)
    MouseEnteredShelf();
  else
    MouseLeftShelf();
}

bool DownloadShelfGtk::IsCursorInShelfZone(
    const gfx::Point& cursor_screen_coords) {
  bool realized = (shelf_.get() &&
                   gtk_widget_get_window(shelf_.get()));
  // Do nothing if we've been unrealized in order to avoid a NOTREACHED in
  // GetWidgetScreenPosition.
  if (!realized)
    return false;

  gfx::Rect bounds = ui::GetWidgetScreenBounds(shelf_.get());

  // Negative insets expand the rectangle. We only expand the top.
  bounds.Inset(gfx::Insets(-kShelfAuraSize, 0, 0, 0));

  return bounds.Contains(cursor_screen_coords);
}

void DownloadShelfGtk::MouseLeftShelf() {
  DCHECK(close_on_mouse_out_);

  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(
          &DownloadShelfGtk::Close, weak_factory_.GetWeakPtr(), AUTOMATIC),
      base::TimeDelta::FromMilliseconds(kAutoCloseDelayMs));
}

void DownloadShelfGtk::MouseEnteredShelf() {
  weak_factory_.InvalidateWeakPtrs();
}
