// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/infobars/translate_message_infobar_gtk.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/translate/translate_infobar_delegate.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/gtk/gtk_signal_registrar.h"

TranslateMessageInfoBar::TranslateMessageInfoBar(
    InfoBarService* owner,
    TranslateInfoBarDelegate* delegate)
    : TranslateInfoBarBase(owner, delegate) {
}

TranslateMessageInfoBar::~TranslateMessageInfoBar() {
}

void TranslateMessageInfoBar::InitWidgets() {
  TranslateInfoBarBase::InitWidgets();

  GtkWidget* new_hbox = gtk_hbox_new(FALSE, ui::kControlSpacing);
  gtk_util::CenterWidgetInHBox(hbox(), new_hbox, false, 0);

  std::string text = UTF16ToUTF8(GetDelegate()->GetMessageInfoBarText());
  gtk_box_pack_start(GTK_BOX(new_hbox), CreateLabel(text.c_str()), FALSE, FALSE,
                     0);
  string16 button_text = GetDelegate()->GetMessageInfoBarButtonText();
  if (!button_text.empty()) {
    GtkWidget* button =
        gtk_button_new_with_label(UTF16ToUTF8(button_text).c_str());
    signals()->Connect(button, "clicked", G_CALLBACK(&OnButtonPressedThunk),
                       this);
    gtk_box_pack_start(GTK_BOX(new_hbox), button, FALSE, FALSE, 0);
  }
}

void TranslateMessageInfoBar::OnButtonPressed(GtkWidget* sender) {
  GetDelegate()->MessageInfoBarButtonPressed();
}
