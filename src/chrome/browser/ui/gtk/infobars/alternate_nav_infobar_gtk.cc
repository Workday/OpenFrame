// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/infobars/alternate_nav_infobar_gtk.h"

#include "chrome/browser/ui/gtk/event_utils.h"
#include "chrome/browser/ui/omnibox/alternate_nav_infobar_delegate.h"


// AlternateNavInfoBarDelegate -------------------------------------------------

InfoBar* AlternateNavInfoBarDelegate::CreateInfoBar(InfoBarService* owner) {
  return new AlternateNavInfoBarGtk(owner, this);
}


// AlternateNavInfoBarGtk ------------------------------------------------------

AlternateNavInfoBarGtk::AlternateNavInfoBarGtk(
    InfoBarService* owner,
    AlternateNavInfoBarDelegate* delegate)
    : InfoBarGtk(owner, delegate) {
}

AlternateNavInfoBarGtk::~AlternateNavInfoBarGtk() {
}

void AlternateNavInfoBarGtk::InitWidgets() {
  InfoBarGtk::InitWidgets();

  size_t link_offset;
  string16 display_text = GetDelegate()->GetMessageTextWithOffset(&link_offset);
  string16 link_text = GetDelegate()->GetLinkText();
  AddLabelWithInlineLink(display_text, link_text, link_offset,
                         G_CALLBACK(OnLinkClickedThunk));
}

AlternateNavInfoBarDelegate* AlternateNavInfoBarGtk::GetDelegate() {
  return static_cast<AlternateNavInfoBarDelegate*>(delegate());
}

void AlternateNavInfoBarGtk::OnLinkClicked(GtkWidget* button) {
  if (GetDelegate()->LinkClicked(
        event_utils::DispositionForCurrentButtonPressEvent()))
    RemoveSelf();
}
