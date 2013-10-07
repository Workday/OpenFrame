// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/first_run_dialog.h"

#include <string>
#include <vector>

#include "base/i18n/rtl.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/breakpad_linux.h"
#include "chrome/browser/first_run/first_run_dialog.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/process_singleton.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/gtk/gtk_chrome_link_button.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/installer/util/google_update_settings.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "ui/base/gtk/gtk_floating_container.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

#if defined(GOOGLE_CHROME_BUILD)
#include "base/prefs/pref_service.h"
#include "chrome/browser/browser_process.h"
#endif

namespace {

// Set the (x, y) coordinates of the welcome message (which floats on top of
// the omnibox image at the top of the first run dialog).
void SetWelcomePosition(GtkFloatingContainer* container,
                        GtkAllocation* allocation,
                        GtkWidget* label) {
  GValue value = { 0, };
  g_value_init(&value, G_TYPE_INT);

  GtkRequisition req;
  gtk_widget_size_request(label, &req);

  int x = base::i18n::IsRTL() ?
      allocation->width - req.width - ui::kContentAreaSpacing :
      ui::kContentAreaSpacing;
  g_value_set_int(&value, x);
  gtk_container_child_set_property(GTK_CONTAINER(container),
                                   label, "x", &value);

  int y = allocation->height / 2 - req.height / 2;
  g_value_set_int(&value, y);
  gtk_container_child_set_property(GTK_CONTAINER(container),
                                   label, "y", &value);
  g_value_unset(&value);
}

}  // namespace

namespace first_run {

bool ShowFirstRunDialog(Profile* profile) {
  return FirstRunDialog::Show();
}

}  // namespace first_run

// static
bool FirstRunDialog::Show() {
  bool dialog_shown = false;
#if defined(GOOGLE_CHROME_BUILD)
  // If the metrics reporting is managed, we won't ask.
  const PrefService::Preference* metrics_reporting_pref =
      g_browser_process->local_state()->FindPreference(
          prefs::kMetricsReportingEnabled);
  bool show_reporting_dialog = !metrics_reporting_pref ||
      !metrics_reporting_pref->IsManaged();

  if (show_reporting_dialog) {
    // Object deletes itself.
    new FirstRunDialog();
    dialog_shown = true;

    // TODO(port): it should be sufficient to just run the dialog:
    // int response = gtk_dialog_run(GTK_DIALOG(dialog));
    // but that spins a nested message loop and hoses us.  :(
    // http://code.google.com/p/chromium/issues/detail?id=12552
    // Instead, run a loop directly here.
    base::MessageLoop::current()->Run();
  }
#endif  // defined(GOOGLE_CHROME_BUILD)
  return dialog_shown;
}

FirstRunDialog::FirstRunDialog()
    : dialog_(NULL),
      report_crashes_(NULL),
      make_default_(NULL) {
  ShowReportingDialog();
}

FirstRunDialog::~FirstRunDialog() {
}

void FirstRunDialog::ShowReportingDialog() {
  dialog_ = gtk_dialog_new_with_buttons(
      l10n_util::GetStringUTF8(IDS_FIRSTRUN_DLG_TITLE).c_str(),
      NULL,  // No parent
      (GtkDialogFlags) (GTK_DIALOG_MODAL | GTK_DIALOG_NO_SEPARATOR),
      NULL);
  gtk_util::AddButtonToDialog(dialog_,
      l10n_util::GetStringUTF8(IDS_FIRSTRUN_DLG_OK).c_str(),
      GTK_STOCK_APPLY, GTK_RESPONSE_ACCEPT);
  gtk_window_set_deletable(GTK_WINDOW(dialog_), FALSE);

  gtk_window_set_resizable(GTK_WINDOW(dialog_), FALSE);

  g_signal_connect(dialog_, "delete-event",
                   G_CALLBACK(gtk_widget_hide_on_delete), NULL);

  GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog_));

  make_default_ = gtk_check_button_new_with_label(
      l10n_util::GetStringUTF8(IDS_FR_CUSTOMIZE_DEFAULT_BROWSER).c_str());
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(make_default_), TRUE);
  gtk_box_pack_start(GTK_BOX(content_area), make_default_, FALSE, FALSE, 0);

  report_crashes_ = gtk_check_button_new();
  GtkWidget* check_label = gtk_label_new(
      l10n_util::GetStringUTF8(IDS_OPTIONS_ENABLE_LOGGING).c_str());
  gtk_label_set_line_wrap(GTK_LABEL(check_label), TRUE);
  gtk_container_add(GTK_CONTAINER(report_crashes_), check_label);
  GtkWidget* learn_more_vbox = gtk_vbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(learn_more_vbox), report_crashes_,
                     FALSE, FALSE, 0);

  GtkWidget* learn_more_link = gtk_chrome_link_button_new(
      l10n_util::GetStringUTF8(IDS_LEARN_MORE).c_str());
  gtk_button_set_alignment(GTK_BUTTON(learn_more_link), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(learn_more_vbox),
                     gtk_util::IndentWidget(learn_more_link),
                     FALSE, FALSE, 0);
  g_signal_connect(learn_more_link, "clicked",
                   G_CALLBACK(OnLearnMoreLinkClickedThunk), this);

  gtk_box_pack_start(GTK_BOX(content_area), learn_more_vbox, FALSE, FALSE, 0);

  g_signal_connect(dialog_, "response",
                   G_CALLBACK(OnResponseDialogThunk), this);
  gtk_widget_show_all(dialog_);
}

void FirstRunDialog::OnResponseDialog(GtkWidget* widget, int response) {
  if (dialog_)
    gtk_widget_hide_all(dialog_);

  // Check if user has opted into reporting.
  if (report_crashes_ &&
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(report_crashes_))) {
    if (GoogleUpdateSettings::SetCollectStatsConsent(true))
      InitCrashReporter();
  } else {
    GoogleUpdateSettings::SetCollectStatsConsent(false);
  }

  // If selected set as default browser.
  if (make_default_ &&
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(make_default_))) {
    ShellIntegration::SetAsDefaultBrowser();
  }

  FirstRunDone();
}

void FirstRunDialog::OnLearnMoreLinkClicked(GtkButton* button) {
  platform_util::OpenExternal(GURL(chrome::kLearnMoreReportingURL));
}

void FirstRunDialog::FirstRunDone() {
  first_run::SetShouldShowWelcomePage();

  if (dialog_)
    gtk_widget_destroy(dialog_);
  base::MessageLoop::current()->Quit();
  delete this;
}
