// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/crypto_module_password_dialog.h"

#include <gtk/gtk.h>

#include "base/basictypes.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "crypto/crypto_module_blocking_password_delegate.h"
#include "grit/generated_resources.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

namespace {

// TODO(mattm): change into a constrained dialog.
class CryptoModulePasswordDialog {
 public:
  CryptoModulePasswordDialog(
      const std::string& slot_name,
      bool retry,
      chrome::CryptoModulePasswordReason reason,
      const std::string& server,
      const chrome::CryptoModulePasswordCallback& callback);

  ~CryptoModulePasswordDialog() {}

  void Show();

 private:
  CHROMEGTK_CALLBACK_1(CryptoModulePasswordDialog, void, OnResponse, int);
  CHROMEGTK_CALLBACK_0(CryptoModulePasswordDialog, void, OnWindowDestroy);

  chrome::CryptoModulePasswordCallback callback_;

  GtkWidget* dialog_;
  GtkWidget* password_entry_;

  DISALLOW_COPY_AND_ASSIGN(CryptoModulePasswordDialog);
};

CryptoModulePasswordDialog::CryptoModulePasswordDialog(
    const std::string& slot_name,
    bool retry,
    chrome::CryptoModulePasswordReason reason,
    const std::string& server,
    const chrome::CryptoModulePasswordCallback& callback)
    : callback_(callback) {
  dialog_ = gtk_dialog_new_with_buttons(
      l10n_util::GetStringUTF8(IDS_CRYPTO_MODULE_AUTH_DIALOG_TITLE).c_str(),
      NULL,
      GTK_DIALOG_NO_SEPARATOR,
      NULL);  // Populate the buttons later, for control over the OK button.
  gtk_dialog_add_button(GTK_DIALOG(dialog_),
                        GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT);
  GtkWidget* ok_button = gtk_util::AddButtonToDialog(
      dialog_,
      l10n_util::GetStringUTF8(
          IDS_CRYPTO_MODULE_AUTH_DIALOG_OK_BUTTON_LABEL).c_str(),
      GTK_STOCK_OK,
      GTK_RESPONSE_ACCEPT);
  gtk_widget_set_can_default(ok_button, TRUE);
  gtk_dialog_set_default_response(GTK_DIALOG(dialog_), GTK_RESPONSE_ACCEPT);

  // Select an appropriate text for the reason.
  std::string text;
  const string16& server16 = UTF8ToUTF16(server);
  const string16& slot16 = UTF8ToUTF16(slot_name);
  switch (reason) {
    case chrome::kCryptoModulePasswordKeygen:
      text = l10n_util::GetStringFUTF8(
          IDS_CRYPTO_MODULE_AUTH_DIALOG_TEXT_KEYGEN, slot16, server16);
      break;
    case chrome::kCryptoModulePasswordCertEnrollment:
      text = l10n_util::GetStringFUTF8(
          IDS_CRYPTO_MODULE_AUTH_DIALOG_TEXT_CERT_ENROLLMENT, slot16, server16);
      break;
    case chrome::kCryptoModulePasswordClientAuth:
      text = l10n_util::GetStringFUTF8(
          IDS_CRYPTO_MODULE_AUTH_DIALOG_TEXT_CLIENT_AUTH, slot16, server16);
      break;
    case chrome::kCryptoModulePasswordListCerts:
      text = l10n_util::GetStringFUTF8(
          IDS_CRYPTO_MODULE_AUTH_DIALOG_TEXT_LIST_CERTS, slot16);
      break;
    case chrome::kCryptoModulePasswordCertImport:
      text = l10n_util::GetStringFUTF8(
          IDS_CRYPTO_MODULE_AUTH_DIALOG_TEXT_CERT_IMPORT, slot16);
      break;
    case chrome::kCryptoModulePasswordCertExport:
      text = l10n_util::GetStringFUTF8(
          IDS_CRYPTO_MODULE_AUTH_DIALOG_TEXT_CERT_EXPORT, slot16);
      break;
    default:
      NOTREACHED();
  }
  GtkWidget* label = gtk_label_new(text.c_str());
  gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
  gtk_util::LeftAlignMisc(label);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog_)->vbox), label,
                     FALSE, FALSE, 0);

  password_entry_ = gtk_entry_new();
  gtk_entry_set_activates_default(GTK_ENTRY(password_entry_), TRUE);
  gtk_entry_set_visibility(GTK_ENTRY(password_entry_), FALSE);

  GtkWidget* password_box = gtk_hbox_new(FALSE, ui::kLabelSpacing);
  gtk_box_pack_start(GTK_BOX(password_box),
                     gtk_label_new(l10n_util::GetStringUTF8(
                         IDS_CRYPTO_MODULE_AUTH_DIALOG_PASSWORD_FIELD).c_str()),
                     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(password_box), password_entry_,
                     TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog_)->vbox), password_box,
                     FALSE, FALSE, 0);

  g_signal_connect(dialog_, "response",
                   G_CALLBACK(OnResponseThunk), this);
  g_signal_connect(dialog_, "destroy",
                   G_CALLBACK(OnWindowDestroyThunk), this);
}

void CryptoModulePasswordDialog::Show() {
  gtk_util::ShowDialog(dialog_);
}

void CryptoModulePasswordDialog::OnResponse(GtkWidget* dialog,
                                            int response_id) {
  if (response_id == GTK_RESPONSE_ACCEPT)
    callback_.Run(gtk_entry_get_text(GTK_ENTRY(password_entry_)));
  else
    callback_.Run(static_cast<const char*>(NULL));

  // This will cause gtk to zero out the buffer.  (see
  // gtk_entry_buffer_normal_delete_text:
  // http://git.gnome.org/browse/gtk+/tree/gtk/gtkentrybuffer.c#n187)
  gtk_editable_delete_text(GTK_EDITABLE(password_entry_), 0, -1);
  gtk_widget_destroy(dialog_);
}

void CryptoModulePasswordDialog::OnWindowDestroy(GtkWidget* widget) {
  delete this;
}

}  // namespace

namespace chrome {

void ShowCryptoModulePasswordDialog(
    const std::string& slot_name,
    bool retry,
    CryptoModulePasswordReason reason,
    const std::string& server,
    const CryptoModulePasswordCallback& callback) {
  (new CryptoModulePasswordDialog(slot_name, retry, reason, server,
                                  callback))->Show();
}

}  // namespace chrome
