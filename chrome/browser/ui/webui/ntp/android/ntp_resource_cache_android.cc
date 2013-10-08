// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ntp/ntp_resource_cache.h"

#include "base/command_line.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/string16.h"
#include "base/strings/string_piece.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/webui/jstemplate_builder.h"
#include "ui/webui/web_ui_util.h"

using content::BrowserThread;

namespace {

const char kLearnMoreIncognitoUrl[] =
    "https://www.google.com/support/chrome/bin/answer.py?answer=95464";

}  // namespace

NTPResourceCache::NTPResourceCache(Profile* profile) : profile_(profile) {}

NTPResourceCache::~NTPResourceCache() {}

base::RefCountedMemory* NTPResourceCache::GetNewTabHTML(bool is_incognito) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // Android uses same html/css for incognito NTP and normal NTP
  if (!new_tab_html_.get())
    CreateNewTabHTML();
  return new_tab_html_.get();
}

base::RefCountedMemory* NTPResourceCache::GetNewTabCSS(bool is_incognito) {
  // This is used for themes, which are not currently supported on Android.
  NOTIMPLEMENTED();
  return NULL;
}

void NTPResourceCache::Observe(int type,
                               const content::NotificationSource& source,
                               const content::NotificationDetails& details) {
  // No notifications necessary in Android.
}

void NTPResourceCache::OnPreferenceChanged() {
  // No notifications necessary in Android.
}

void NTPResourceCache::CreateNewTabHTML() {
  // TODO(estade): these strings should be defined in their relevant handlers
  // (in GetLocalizedValues) and should have more legible names.
  // Show the profile name in the title and most visited labels if the current
  // profile is not the default.
  DictionaryValue localized_strings;
  localized_strings.SetBoolean("hasattribution", false);
  localized_strings.SetString("title",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_TITLE));
  localized_strings.SetString("mostvisited",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_MOST_VISITED));
  localized_strings.SetString("recentlyclosed",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_RECENTLY_CLOSED));
  localized_strings.SetString("opentabslastsynced",
      l10n_util::GetStringUTF16(IDS_SYNC_NTP_OPEN_TABS_LAST_SYNCED));
  localized_strings.SetString("elementopeninnewtab",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_CONTEXT_MENU_OPEN_IN_NEW_TAB));
  localized_strings.SetString("elementopeninincognitotab",
      l10n_util::GetStringUTF16(
          IDS_NEW_TAB_CONTEXT_MENU_OPEN_IN_INCOGNITO_TAB));
  localized_strings.SetString("elementremove",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_CONTEXT_MENU_REMOVE));
  localized_strings.SetString("removeall",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_CONTEXT_MENU_REMOVE_ALL));
  localized_strings.SetString("bookmarkedit",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_CONTEXT_MENU_EDIT_BOOKMARK));
  localized_strings.SetString("bookmarkdelete",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_CONTEXT_MENU_DELETE_BOOKMARK));
  localized_strings.SetString("bookmarkshortcut",
        l10n_util::GetStringUTF16(IDS_NEW_TAB_CONTEXT_MENU_BOOKMARK_SHORTCUT));
  localized_strings.SetString("editfolder",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_EDIT_FOLDER));
  localized_strings.SetString("deletefolder",
        l10n_util::GetStringUTF16(IDS_BOOKMARK_REMOVE_FOLDER));
  localized_strings.SetString("receivedDocuments",
      l10n_util::GetStringUTF16(IDS_RECEIVED_DOCUMENTS));
  localized_strings.SetString("syncPromo",
      l10n_util::GetStringUTF16(IDS_SYNC_PROMO_DESKTOP_INSTRUCTIONS));
  localized_strings.SetString("syncEnableSync",
      l10n_util::GetStringUTF16(IDS_SYNC_ENABLE_SYNC));
  localized_strings.SetString("bookmarkstitle",
      l10n_util::GetStringUTF16(IDS_ACCNAME_BOOKMARKS));
  localized_strings.SetString("incognito_document_title",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_INCOGNITO_TITLE));
  localized_strings.SetString("most_visited_document_title",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_MOST_VISITED_TITLE));
  localized_strings.SetString("bookmarks_document_title",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_BOOKMARKS_TITLE));
  localized_strings.SetString("open_tabs_document_title",
      l10n_util::GetStringUTF16(IDS_NEW_TAB_OTHER_SESSIONS_TITLE));

  webui::SetFontAndTextDirection(&localized_strings);

  base::StringPiece new_tab_html(ResourceBundle::GetSharedInstance().
                                 GetRawDataResource(IDR_NEW_TAB_ANDROID_HTML));
  localized_strings.SetString(
      "device",
      CommandLine::ForCurrentProcess()->HasSwitch(switches::kTabletUI) ?
          "tablet" : "phone");
  const char* new_tab_link = kLearnMoreIncognitoUrl;
  string16 learnMoreLink = ASCIIToUTF16(
      google_util::AppendGoogleLocaleParam(GURL(new_tab_link)).spec());
  localized_strings.SetString("content",
      l10n_util::GetStringFUTF16(
          IDS_NEW_TAB_OTR_MESSAGE_MOBILE, learnMoreLink));

  // Load the new tab page appropriate for this build.
  std::string full_html;

  // Inject the template data into the HTML so that it is available before any
  // layout is needed.
  std::string json_html;
  webui::AppendJsonHtml(&localized_strings, &json_html);

  static const base::StringPiece template_data_placeholder(
      "<!-- template data placeholder -->");
  size_t pos = new_tab_html.find(template_data_placeholder);

  if (pos != base::StringPiece::npos) {
    full_html.assign(new_tab_html.data(), pos);
    full_html.append(json_html);
    size_t after_offset = pos + template_data_placeholder.size();
    full_html.append(new_tab_html.data() + after_offset,
                     new_tab_html.size() - after_offset);
  } else {
    NOTREACHED();
    full_html.assign(new_tab_html.data(), new_tab_html.size());
  }

  new_tab_html_ = base::RefCountedString::TakeString(&full_html);
}
