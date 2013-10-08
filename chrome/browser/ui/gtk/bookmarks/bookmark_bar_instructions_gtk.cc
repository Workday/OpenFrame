// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/bookmarks/bookmark_bar_instructions_gtk.h"

#include "base/observer_list.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/bookmarks/bookmark_bar_instructions_delegate.h"
#include "chrome/browser/ui/gtk/gtk_chrome_link_button.h"
#include "chrome/browser/ui/gtk/gtk_chrome_shrinkable_hbox.h"
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "content/public/browser/notification_source.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

BookmarkBarInstructionsGtk::BookmarkBarInstructionsGtk(
    chrome::BookmarkBarInstructionsDelegate* delegate,
    Profile* profile)
    : delegate_(delegate),
      profile_(profile),
      theme_service_(GtkThemeService::GetFrom(profile_)) {
  instructions_hbox_ = gtk_chrome_shrinkable_hbox_new(FALSE, FALSE, 0);
  gtk_widget_set_size_request(instructions_hbox_, 0, -1);

  instructions_label_ = gtk_label_new(
      l10n_util::GetStringUTF8(IDS_BOOKMARKS_NO_ITEMS).c_str());
  gtk_misc_set_alignment(GTK_MISC(instructions_label_), 0, 0.5);
  gtk_util::CenterWidgetInHBox(instructions_hbox_, instructions_label_,
                               false, 1);
  g_signal_connect(instructions_label_, "map",
                   G_CALLBACK(gtk_util::InitLabelSizeRequestAndEllipsizeMode),
                   NULL);

  instructions_link_ = theme_service_->BuildChromeLinkButton(
      l10n_util::GetStringUTF8(IDS_BOOKMARK_BAR_IMPORT_LINK));
  gtk_misc_set_alignment(
      GTK_MISC(GTK_CHROME_LINK_BUTTON(instructions_link_)->label), 0, 0.5);
  g_signal_connect(instructions_link_, "clicked",
                   G_CALLBACK(OnButtonClickThunk), this);
  gtk_util::SetButtonTriggersNavigation(instructions_link_);
  // Until we switch to vector graphics, force the font size.
  // 13.4px == 10pt @ 96dpi
  gtk_util::ForceFontSizePixels(
      GTK_CHROME_LINK_BUTTON(instructions_link_)->label, 13.4);
  gtk_util::CenterWidgetInHBox(instructions_hbox_, instructions_link_,
                               false, 6);
  g_signal_connect(GTK_CHROME_LINK_BUTTON(instructions_link_)->label, "map",
                   G_CALLBACK(gtk_util::InitLabelSizeRequestAndEllipsizeMode),
                   NULL);

  registrar_.Add(this, chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
                 content::Source<ThemeService>(theme_service_));
  theme_service_->InitThemesFor(this);
}

void BookmarkBarInstructionsGtk::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_BROWSER_THEME_CHANGED)
    UpdateColors();
}

void BookmarkBarInstructionsGtk::OnButtonClick(GtkWidget* button) {
  delegate_->ShowImportDialog();
}

void BookmarkBarInstructionsGtk::UpdateColors() {
  GdkColor bookmark_color = theme_service_->GetGdkColor(
      ThemeProperties::COLOR_BOOKMARK_TEXT);
  if (theme_service_->UsingNativeTheme()) {
    gtk_util::SetLabelColor(instructions_label_, NULL);
    gtk_chrome_link_button_set_normal_color(
        GTK_CHROME_LINK_BUTTON(instructions_link_), NULL);
  } else {
    gtk_util::SetLabelColor(instructions_label_, &bookmark_color);

    // When using a non-standard, non-gtk theme, we make the link color match
    // the bookmark text color. Otherwise, standard link blue can look very
    // bad for some dark themes.
    if (theme_service_->GetColor(ThemeProperties::COLOR_BOOKMARK_TEXT) ==
        ThemeProperties::GetDefaultColor(
            ThemeProperties::COLOR_BOOKMARK_TEXT)) {
      gtk_chrome_link_button_set_normal_color(
          GTK_CHROME_LINK_BUTTON(instructions_link_), NULL);
    } else {
      gtk_chrome_link_button_set_normal_color(
          GTK_CHROME_LINK_BUTTON(instructions_link_), &bookmark_color);
    }
  }
}
