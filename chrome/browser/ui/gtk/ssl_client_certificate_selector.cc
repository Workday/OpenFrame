// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ssl/ssl_client_certificate_selector.h"

#include <gtk/gtk.h>

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/certificate_viewer.h"
#include "chrome/browser/ssl/ssl_client_auth_observer.h"
#include "chrome/browser/ui/crypto_module_password_dialog.h"
#include "chrome/browser/ui/gtk/constrained_window_gtk.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/common/net/x509_certificate_model.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "content/public/browser/browser_thread.h"
#include "grit/generated_resources.h"
#include "net/cert/x509_certificate.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "ui/base/gtk/gtk_compat.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/base/gtk/scoped_gobject.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/native_widget_types.h"

using content::BrowserThread;
using content::WebContents;
using web_modal::WebContentsModalDialogManager;

namespace {

enum {
  RESPONSE_SHOW_CERT_INFO = 1,
};

///////////////////////////////////////////////////////////////////////////////
// SSLClientCertificateSelector

class SSLClientCertificateSelector : public SSLClientAuthObserver {
 public:
  explicit SSLClientCertificateSelector(
      WebContents* parent,
      const net::HttpNetworkSession* network_session,
      net::SSLCertRequestInfo* cert_request_info,
      const base::Callback<void(net::X509Certificate*)>& callback);
  virtual ~SSLClientCertificateSelector();

  void Show();

  // SSLClientAuthObserver implementation:
  virtual void OnCertSelectedByNotification() OVERRIDE;

 private:
  void PopulateCerts();

  net::X509Certificate* GetSelectedCert();

  static std::string FormatComboBoxText(
      net::X509Certificate::OSCertHandle cert,
      const std::string& nickname);
  static std::string FormatDetailsText(
      net::X509Certificate::OSCertHandle cert);

  // Callback after unlocking certificate slot.
  void Unlocked();

  CHROMEGTK_CALLBACK_0(SSLClientCertificateSelector, void, OnComboBoxChanged);
  CHROMEGTK_CALLBACK_0(SSLClientCertificateSelector, void, OnViewClicked);
  CHROMEGTK_CALLBACK_0(SSLClientCertificateSelector, void, OnCancelClicked);
  CHROMEGTK_CALLBACK_0(SSLClientCertificateSelector, void, OnOkClicked);
  CHROMEGTK_CALLBACK_1(SSLClientCertificateSelector, void, OnPromptShown,
                       GtkWidget*);
  CHROMEGTK_CALLBACK_0(SSLClientCertificateSelector, void, OnDestroy);

  std::vector<std::string> details_strings_;

  GtkWidget* cert_combo_box_;
  GtkTextBuffer* cert_details_buffer_;

  ui::ScopedGObject<GtkWidget>::Type root_widget_;
  // Hold on to the select button to focus it.
  GtkWidget* select_button_;

  WebContents* web_contents_;
  GtkWidget* window_;

  DISALLOW_COPY_AND_ASSIGN(SSLClientCertificateSelector);
};

SSLClientCertificateSelector::SSLClientCertificateSelector(
    WebContents* web_contents,
    const net::HttpNetworkSession* network_session,
    net::SSLCertRequestInfo* cert_request_info,
    const base::Callback<void(net::X509Certificate*)>& callback)
    : SSLClientAuthObserver(network_session, cert_request_info, callback),
      web_contents_(web_contents),
      window_(NULL) {
  root_widget_.reset(gtk_vbox_new(FALSE, ui::kControlSpacing));
  g_object_ref_sink(root_widget_.get());
  g_signal_connect(root_widget_.get(),
                   "destroy",
                   G_CALLBACK(OnDestroyThunk),
                   this);

  GtkWidget* site_vbox = gtk_vbox_new(FALSE, ui::kControlSpacing);
  gtk_box_pack_start(GTK_BOX(root_widget_.get()), site_vbox,
                     FALSE, FALSE, 0);

  GtkWidget* site_description_label = gtk_util::CreateBoldLabel(
      l10n_util::GetStringUTF8(IDS_CERT_SELECTOR_SITE_DESCRIPTION_LABEL));
  gtk_box_pack_start(GTK_BOX(site_vbox), site_description_label,
                     FALSE, FALSE, 0);

  GtkWidget* site_label = gtk_label_new(
      cert_request_info->host_and_port.c_str());
  gtk_util::LeftAlignMisc(site_label);
  gtk_box_pack_start(GTK_BOX(site_vbox), site_label, FALSE, FALSE, 0);

  GtkWidget* selector_vbox = gtk_vbox_new(FALSE, ui::kControlSpacing);
  gtk_box_pack_start(GTK_BOX(root_widget_.get()), selector_vbox,
                     TRUE, TRUE, 0);

  GtkWidget* choose_description_label = gtk_util::CreateBoldLabel(
      l10n_util::GetStringUTF8(IDS_CERT_SELECTOR_CHOOSE_DESCRIPTION_LABEL));
  gtk_box_pack_start(GTK_BOX(selector_vbox), choose_description_label,
                     FALSE, FALSE, 0);


  cert_combo_box_ = gtk_combo_box_new_text();
  g_signal_connect(cert_combo_box_, "changed",
                   G_CALLBACK(OnComboBoxChangedThunk), this);
  gtk_box_pack_start(GTK_BOX(selector_vbox), cert_combo_box_,
                     FALSE, FALSE, 0);

  GtkWidget* details_label = gtk_label_new(l10n_util::GetStringUTF8(
      IDS_CERT_SELECTOR_DETAILS_DESCRIPTION_LABEL).c_str());
  gtk_util::LeftAlignMisc(details_label);
  gtk_box_pack_start(GTK_BOX(selector_vbox), details_label, FALSE, FALSE, 0);

  // TODO(mattm): fix text view coloring (should have grey background).
  GtkWidget* cert_details_view = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(cert_details_view), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(cert_details_view), GTK_WRAP_WORD);
  cert_details_buffer_ = gtk_text_view_get_buffer(
      GTK_TEXT_VIEW(cert_details_view));
  // We put the details in a frame instead of a scrolled window so that the
  // entirety will be visible without requiring scrolling or expanding the
  // dialog.  This does however mean the dialog will grow itself if you switch
  // to different cert that has longer details text.
  GtkWidget* details_frame = gtk_frame_new(NULL);
  gtk_frame_set_shadow_type(GTK_FRAME(details_frame), GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(details_frame), cert_details_view);
  gtk_box_pack_start(GTK_BOX(selector_vbox), details_frame, TRUE, TRUE, 0);

  // And then create a set of buttons like a GtkDialog would.
  GtkWidget* button_box = gtk_hbutton_box_new();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(button_box), GTK_BUTTONBOX_END);
  gtk_box_set_spacing(GTK_BOX(button_box), ui::kControlSpacing);
  gtk_box_pack_end(GTK_BOX(root_widget_.get()), button_box, FALSE, FALSE, 0);

  GtkWidget* view_button = gtk_button_new_with_mnemonic(
      l10n_util::GetStringUTF8(IDS_PAGEINFO_CERT_INFO_BUTTON).c_str());
  gtk_box_pack_start(GTK_BOX(button_box), view_button, FALSE, FALSE, 0);
  g_signal_connect(view_button, "clicked",
                   G_CALLBACK(OnViewClickedThunk), this);

  GtkWidget* cancel_button = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
  gtk_box_pack_end(GTK_BOX(button_box), cancel_button, FALSE, FALSE, 0);
  g_signal_connect(cancel_button, "clicked",
                   G_CALLBACK(OnCancelClickedThunk), this);

  GtkWidget* select_button = gtk_button_new_from_stock(GTK_STOCK_OK);
  gtk_box_pack_end(GTK_BOX(button_box), select_button, FALSE, FALSE, 0);
  g_signal_connect(select_button, "clicked",
                   G_CALLBACK(OnOkClickedThunk), this);

  // When we are attached to a window, focus the select button.
  select_button_ = select_button;
  g_signal_connect(root_widget_.get(), "hierarchy-changed",
                   G_CALLBACK(OnPromptShownThunk), this);
  PopulateCerts();

  gtk_widget_show_all(root_widget_.get());

  StartObserving();
}

SSLClientCertificateSelector::~SSLClientCertificateSelector() {
}

void SSLClientCertificateSelector::Show() {
  DCHECK(!window_);
  window_ = CreateWebContentsModalDialogGtk(root_widget_.get(), select_button_);

  WebContentsModalDialogManager* web_contents_modal_dialog_manager =
      WebContentsModalDialogManager::FromWebContents(web_contents_);
  web_contents_modal_dialog_manager->ShowDialog(window_);
}

void SSLClientCertificateSelector::OnCertSelectedByNotification() {
  DCHECK(window_);
  gtk_widget_destroy(window_);
}

void SSLClientCertificateSelector::PopulateCerts() {
  std::vector<std::string> nicknames;
  x509_certificate_model::GetNicknameStringsFromCertList(
      cert_request_info()->client_certs,
      l10n_util::GetStringUTF8(IDS_CERT_SELECTOR_CERT_EXPIRED),
      l10n_util::GetStringUTF8(IDS_CERT_SELECTOR_CERT_NOT_YET_VALID),
      &nicknames);

  DCHECK_EQ(nicknames.size(),
            cert_request_info()->client_certs.size());

  for (size_t i = 0; i < cert_request_info()->client_certs.size(); ++i) {
    net::X509Certificate::OSCertHandle cert =
        cert_request_info()->client_certs[i]->os_cert_handle();

    details_strings_.push_back(FormatDetailsText(cert));

    gtk_combo_box_append_text(
        GTK_COMBO_BOX(cert_combo_box_),
        FormatComboBoxText(cert, nicknames[i]).c_str());
  }

  // Auto-select the first cert.
  gtk_combo_box_set_active(GTK_COMBO_BOX(cert_combo_box_), 0);
}

net::X509Certificate* SSLClientCertificateSelector::GetSelectedCert() {
  int selected = gtk_combo_box_get_active(GTK_COMBO_BOX(cert_combo_box_));
  if (selected >= 0 &&
      selected < static_cast<int>(
          cert_request_info()->client_certs.size()))
    return cert_request_info()->client_certs[selected].get();
  return NULL;
}

// static
std::string SSLClientCertificateSelector::FormatComboBoxText(
    net::X509Certificate::OSCertHandle cert, const std::string& nickname) {
  std::string rv(nickname);
  rv += " [";
  rv += x509_certificate_model::GetSerialNumberHexified(cert, std::string());
  rv += ']';
  return rv;
}

// static
std::string SSLClientCertificateSelector::FormatDetailsText(
    net::X509Certificate::OSCertHandle cert) {
  std::string rv;

  rv += l10n_util::GetStringFUTF8(
      IDS_CERT_SUBJECTNAME_FORMAT,
      UTF8ToUTF16(x509_certificate_model::GetSubjectName(cert)));

  rv += "\n  ";
  rv += l10n_util::GetStringFUTF8(
      IDS_CERT_SERIAL_NUMBER_FORMAT,
      UTF8ToUTF16(x509_certificate_model::GetSerialNumberHexified(
          cert, std::string())));

  base::Time issued, expires;
  if (x509_certificate_model::GetTimes(cert, &issued, &expires)) {
    string16 issued_str = base::TimeFormatShortDateAndTime(issued);
    string16 expires_str = base::TimeFormatShortDateAndTime(expires);
    rv += "\n  ";
    rv += l10n_util::GetStringFUTF8(IDS_CERT_VALIDITY_RANGE_FORMAT,
                                    issued_str, expires_str);
  }

  std::vector<std::string> usages;
  x509_certificate_model::GetUsageStrings(cert, &usages);
  if (usages.size()) {
    rv += "\n  ";
    rv += l10n_util::GetStringFUTF8(IDS_CERT_X509_EXTENDED_KEY_USAGE_FORMAT,
                                    UTF8ToUTF16(JoinString(usages, ',')));
  }

  std::string key_usage_str = x509_certificate_model::GetKeyUsageString(cert);
  if (!key_usage_str.empty()) {
    rv += "\n  ";
    rv += l10n_util::GetStringFUTF8(IDS_CERT_X509_KEY_USAGE_FORMAT,
                                    UTF8ToUTF16(key_usage_str));
  }

  std::vector<std::string> email_addresses;
  x509_certificate_model::GetEmailAddresses(cert, &email_addresses);
  if (email_addresses.size()) {
    rv += "\n  ";
    rv += l10n_util::GetStringFUTF8(
        IDS_CERT_EMAIL_ADDRESSES_FORMAT,
        UTF8ToUTF16(JoinString(email_addresses, ',')));
  }

  rv += '\n';
  rv += l10n_util::GetStringFUTF8(
      IDS_CERT_ISSUERNAME_FORMAT,
      UTF8ToUTF16(x509_certificate_model::GetIssuerName(cert)));

  string16 token(UTF8ToUTF16(x509_certificate_model::GetTokenName(cert)));
  if (!token.empty()) {
    rv += '\n';
    rv += l10n_util::GetStringFUTF8(IDS_CERT_TOKEN_FORMAT, token);
  }

  return rv;
}

void SSLClientCertificateSelector::Unlocked() {
  // TODO(mattm): refactor so we don't need to call GetSelectedCert again.
  net::X509Certificate* cert = GetSelectedCert();
  CertificateSelected(cert);
  DCHECK(window_);
  gtk_widget_destroy(window_);
}

void SSLClientCertificateSelector::OnComboBoxChanged(GtkWidget* combo_box) {
  int selected = gtk_combo_box_get_active(
      GTK_COMBO_BOX(cert_combo_box_));
  if (selected < 0)
    return;
  gtk_text_buffer_set_text(cert_details_buffer_,
                           details_strings_[selected].c_str(),
                           details_strings_[selected].size());
}

void SSLClientCertificateSelector::OnViewClicked(GtkWidget* button) {
  net::X509Certificate* cert = GetSelectedCert();
  if (cert) {
    GtkWidget* toplevel = gtk_widget_get_toplevel(root_widget_.get());
    ShowCertificateViewer(web_contents_, GTK_WINDOW(toplevel), cert);
  }
}

void SSLClientCertificateSelector::OnCancelClicked(GtkWidget* button) {
  CertificateSelected(NULL);
  DCHECK(window_);
  gtk_widget_destroy(window_);
}

void SSLClientCertificateSelector::OnOkClicked(GtkWidget* button) {
  net::X509Certificate* cert = GetSelectedCert();

  // Remove the observer before we try unlocking, otherwise we might act on a
  // notification while waiting for the unlock dialog, causing us to delete
  // ourself before the Unlocked callback gets called.
  StopObserving();

  chrome::UnlockCertSlotIfNecessary(
      cert,
      chrome::kCryptoModulePasswordClientAuth,
      cert_request_info()->host_and_port,
      base::Bind(&SSLClientCertificateSelector::Unlocked,
                 base::Unretained(this)));
}

void SSLClientCertificateSelector::OnPromptShown(GtkWidget* widget,
                                                 GtkWidget* previous_toplevel) {
  if (!root_widget_.get() ||
      !gtk_widget_is_toplevel(gtk_widget_get_toplevel(root_widget_.get())))
    return;
  gtk_widget_set_can_default(select_button_, TRUE);
  gtk_widget_grab_default(select_button_);
}

void SSLClientCertificateSelector::OnDestroy(GtkWidget* widget) {
  // The dialog was closed by escape key.
  StopObserving();
  CertificateSelected(NULL);
  delete this;
}

}  // namespace

namespace chrome {

void ShowSSLClientCertificateSelector(
    content::WebContents* contents,
    const net::HttpNetworkSession* network_session,
    net::SSLCertRequestInfo* cert_request_info,
    const base::Callback<void(net::X509Certificate*)>& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  (new SSLClientCertificateSelector(
      contents, network_session, cert_request_info, callback))->Show();
}

}  // namespace chrome
