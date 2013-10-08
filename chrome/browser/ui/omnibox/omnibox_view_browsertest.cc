// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "base/message_loop/message_loop.h"
#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autocomplete/autocomplete_input.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/history_quick_provider.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/history/history_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/omnibox/location_bar.h"
#include "chrome/browser/ui/omnibox/omnibox_popup_model.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"
#include "net/dns/mock_host_resolver.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/events/event_constants.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/gfx/point.h"

#if defined(TOOLKIT_GTK)
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#endif

#if defined(TOOLKIT_VIEWS)
#include "chrome/browser/ui/views/frame/browser_view.h"
#endif

using base::Time;
using base::TimeDelta;

namespace {

const char kSearchKeyword[] = "foo";
const char kSearchKeyword2[] = "footest.com";
const wchar_t kSearchKeywordKeys[] = { ui::VKEY_F, ui::VKEY_O, ui::VKEY_O, 0 };
const char kSearchURL[] = "http://www.foo.com/search?q={searchTerms}";
const char kSearchShortName[] = "foo";
const char kSearchText[] = "abc";
const wchar_t kSearchTextKeys[] = { ui::VKEY_A, ui::VKEY_B, ui::VKEY_C, 0 };
const char kSearchTextURL[] = "http://www.foo.com/search?q=abc";
const char kSearchSingleChar[] = "z";
const wchar_t kSearchSingleCharKeys[] = { ui::VKEY_Z, 0 };
const char kSearchSingleCharURL[] = "http://www.foo.com/search?q=z";

const char kHistoryPageURL[] = "chrome://history/#q=abc";

const char kDesiredTLDHostname[] = "www.bar.com";
const wchar_t kDesiredTLDKeys[] = { ui::VKEY_B, ui::VKEY_A, ui::VKEY_R, 0 };

const char kInlineAutocompleteText[] = "def";
const wchar_t kInlineAutocompleteTextKeys[] = {
  ui::VKEY_D, ui::VKEY_E, ui::VKEY_F, 0
};

// Hostnames that shall be blocked by host resolver.
const char *kBlockedHostnames[] = {
  "foo",
  "*.foo.com",
  "bar",
  "*.bar.com",
  "abc",
  "*.abc.com",
  "def",
  "*.def.com",
  "*.site.com",
  "history",
  "z"
};

const struct TestHistoryEntry {
  const char* url;
  const char* title;
  const char* body;
  int visit_count;
  int typed_count;
  bool starred;
} kHistoryEntries[] = {
  {"http://www.bar.com/1", "Page 1", kSearchText, 10, 10, false },
  {"http://www.bar.com/2", "Page 2", kSearchText, 9, 9, false },
  {"http://www.bar.com/3", "Page 3", kSearchText, 8, 8, false },
  {"http://www.bar.com/4", "Page 4", kSearchText, 7, 7, false },
  {"http://www.bar.com/5", "Page 5", kSearchText, 6, 6, false },
  {"http://www.bar.com/6", "Page 6", kSearchText, 5, 5, false },
  {"http://www.bar.com/7", "Page 7", kSearchText, 4, 4, false },
  {"http://www.bar.com/8", "Page 8", kSearchText, 3, 3, false },
  {"http://www.bar.com/9", "Page 9", kSearchText, 2, 2, false },
  {"http://www.site.com/path/1", "Site 1", kSearchText, 4, 4, false },
  {"http://www.site.com/path/2", "Site 2", kSearchText, 3, 3, false },
  {"http://www.site.com/path/3", "Site 3", kSearchText, 2, 2, false },

  // To trigger inline autocomplete.
  {"http://www.def.com", "Page def", kSearchText, 10000, 10000, true },

  // Used in particular for the desired TLD test.  This makes it test
  // the interesting case when there's an intranet host with the same
  // name as the .com.
  {"http://bar/", "Bar", kSearchText, 1, 0, false },
};

#if defined(TOOLKIT_GTK)
// Returns the text stored in the PRIMARY clipboard.
std::string GetPrimarySelectionText() {
  GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
  DCHECK(clipboard);

  gchar* selection_text = gtk_clipboard_wait_for_text(clipboard);
  std::string result(selection_text ? selection_text : "");
  g_free(selection_text);
  return result;
}
#endif

// Stores the given text to clipboard.
void SetClipboardText(const string16& text) {
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  ui::ScopedClipboardWriter writer(clipboard, ui::Clipboard::BUFFER_STANDARD);
  writer.WriteText(text);
}

#if defined(OS_MACOSX)
const int kCtrlOrCmdMask = ui::EF_COMMAND_DOWN;
#else
const int kCtrlOrCmdMask = ui::EF_CONTROL_DOWN;
#endif

}  // namespace

class OmniboxViewTest : public InProcessBrowserTest,
                        public content::NotificationObserver {
 protected:
  virtual void SetUpOnMainThread() OVERRIDE {
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
    ASSERT_NO_FATAL_FAILURE(SetupComponents());
    chrome::FocusLocationBar(browser());
    ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  }

  static void GetOmniboxViewForBrowser(
      const Browser* browser,
      OmniboxView** omnibox_view) {
    BrowserWindow* window = browser->window();
    ASSERT_TRUE(window);
    LocationBar* loc_bar = window->GetLocationBar();
    ASSERT_TRUE(loc_bar);
    *omnibox_view = loc_bar->GetLocationEntry();
    ASSERT_TRUE(*omnibox_view);
  }

  void GetOmniboxView(OmniboxView** omnibox_view) {
    GetOmniboxViewForBrowser(browser(), omnibox_view);
  }

  static void SendKeyForBrowser(const Browser* browser,
                                ui::KeyboardCode key,
                                int modifiers) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
        browser, key,
        (modifiers & ui::EF_CONTROL_DOWN) != 0,
        (modifiers & ui::EF_SHIFT_DOWN) != 0,
        (modifiers & ui::EF_ALT_DOWN) != 0,
        (modifiers & ui::EF_COMMAND_DOWN) != 0));
  }

  void SendKey(ui::KeyboardCode key, int modifiers) {
    SendKeyForBrowser(browser(), key, modifiers);
  }

  void SendKeySequence(const wchar_t* keys) {
    for (; *keys; ++keys)
      ASSERT_NO_FATAL_FAILURE(SendKey(static_cast<ui::KeyboardCode>(*keys), 0));
  }

  bool SendKeyAndWait(const Browser* browser,
                      ui::KeyboardCode key,
                      int modifiers,
                      int type,
                      const content::NotificationSource& source)
                          WARN_UNUSED_RESULT {
    return ui_test_utils::SendKeyPressAndWait(
        browser, key,
        (modifiers & ui::EF_CONTROL_DOWN) != 0,
        (modifiers & ui::EF_SHIFT_DOWN) != 0,
        (modifiers & ui::EF_ALT_DOWN) != 0,
        (modifiers & ui::EF_COMMAND_DOWN) != 0,
        type, source);
  }

  void WaitForTabOpenOrCloseForBrowser(const Browser* browser,
                                       int expected_tab_count) {
    int tab_count = browser->tab_strip_model()->count();
    if (tab_count == expected_tab_count)
      return;

    content::NotificationRegistrar registrar;
    registrar.Add(this,
        (tab_count < expected_tab_count) ?
            static_cast<int>(chrome::NOTIFICATION_TAB_PARENTED) :
            static_cast<int>(content::NOTIFICATION_WEB_CONTENTS_DESTROYED),
        content::NotificationService::AllSources());

    while (!HasFailure() &&
           browser->tab_strip_model()->count() != expected_tab_count) {
      content::RunMessageLoop();
    }

    ASSERT_EQ(expected_tab_count, browser->tab_strip_model()->count());
  }

  void WaitForTabOpenOrClose(int expected_tab_count) {
    WaitForTabOpenOrCloseForBrowser(browser(), expected_tab_count);
  }

  void WaitForAutocompleteControllerDone() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

    AutocompleteController* controller =
        omnibox_view->model()->autocomplete_controller();
    ASSERT_TRUE(controller);

    if (controller->done())
      return;

    content::NotificationRegistrar registrar;
    registrar.Add(this,
                  chrome::NOTIFICATION_AUTOCOMPLETE_CONTROLLER_RESULT_READY,
                  content::Source<AutocompleteController>(controller));

    while (!HasFailure() && !controller->done())
      content::RunMessageLoop();

    ASSERT_TRUE(controller->done());
  }

  void SetupSearchEngine() {
    Profile* profile = browser()->profile();
    TemplateURLService* model =
        TemplateURLServiceFactory::GetForProfile(profile);
    ASSERT_TRUE(model);

    if (!model->loaded()) {
      content::NotificationRegistrar registrar;
      registrar.Add(this, chrome::NOTIFICATION_TEMPLATE_URL_SERVICE_LOADED,
                    content::Source<TemplateURLService>(model));
      model->Load();
      content::RunMessageLoop();
    }

    ASSERT_TRUE(model->loaded());
    // Remove built-in template urls, like google.com, bing.com etc., as they
    // may appear as autocomplete suggests and interfere with our tests.
    model->SetDefaultSearchProvider(NULL);
    TemplateURLService::TemplateURLVector builtins = model->GetTemplateURLs();
    for (TemplateURLService::TemplateURLVector::const_iterator
         i = builtins.begin(); i != builtins.end(); ++i)
      model->Remove(*i);

    TemplateURLData data;
    data.short_name = ASCIIToUTF16(kSearchShortName);
    data.SetKeyword(ASCIIToUTF16(kSearchKeyword));
    data.SetURL(kSearchURL);
    TemplateURL* template_url = new TemplateURL(profile, data);
    model->Add(template_url);
    model->SetDefaultSearchProvider(template_url);

    data.SetKeyword(ASCIIToUTF16(kSearchKeyword2));
    model->Add(new TemplateURL(profile, data));
  }

  void AddHistoryEntry(const TestHistoryEntry& entry, const Time& time) {
    Profile* profile = browser()->profile();
    HistoryService* history_service = HistoryServiceFactory::GetForProfile(
        profile, Profile::EXPLICIT_ACCESS);
    ASSERT_TRUE(history_service);

    if (!history_service->BackendLoaded()) {
      content::NotificationRegistrar registrar;
      registrar.Add(this, chrome::NOTIFICATION_HISTORY_LOADED,
                    content::Source<Profile>(profile));
      content::RunMessageLoop();
    }

    BookmarkModel* bookmark_model =
        BookmarkModelFactory::GetForProfile(profile);
    ASSERT_TRUE(bookmark_model);
    ui_test_utils::WaitForBookmarkModelToLoad(bookmark_model);

    GURL url(entry.url);
    // Add everything in order of time. We don't want to have a time that
    // is "right now" or it will nondeterministically appear in the results.
    history_service->AddPageWithDetails(url, UTF8ToUTF16(entry.title),
                                        entry.visit_count,
                                        entry.typed_count, time, false,
                                        history::SOURCE_BROWSED);
    if (entry.starred)
      bookmark_utils::AddIfNotBookmarked(bookmark_model, url, string16());
    // Wait at least for the AddPageWithDetails() call to finish.
    {
      content::NotificationRegistrar registrar;
      registrar.Add(this, chrome::NOTIFICATION_HISTORY_URLS_MODIFIED,
                    content::Source<Profile>(profile));
      content::RunMessageLoop();
      // We don't want to return until all observers have processed this
      // notification, because some (e.g. the in-memory history database) may do
      // something important.  Since we don't know where in the observer list we
      // stand, just spin the message loop once more to allow the current
      // callstack to complete.
      content::RunAllPendingInMessageLoop();
    }
  }

  void SetupHistory() {
    // Add enough history pages containing |kSearchText| to trigger
    // open history page url in autocomplete result.
    for (size_t i = 0; i < arraysize(kHistoryEntries); i++) {
      // Add everything in order of time. We don't want to have a time that
      // is "right now" or it will nondeterministically appear in the results.
      Time t = Time::Now() - TimeDelta::FromHours(i + 1);
      ASSERT_NO_FATAL_FAILURE(AddHistoryEntry(kHistoryEntries[i], t));
    }
  }

  void SetupHostResolver() {
    for (size_t i = 0; i < arraysize(kBlockedHostnames); ++i)
      host_resolver()->AddSimulatedFailure(kBlockedHostnames[i]);
  }

  void SetupComponents() {
    ASSERT_NO_FATAL_FAILURE(SetupHostResolver());
    ASSERT_NO_FATAL_FAILURE(SetupSearchEngine());
    ASSERT_NO_FATAL_FAILURE(SetupHistory());
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    switch (type) {
      case content::NOTIFICATION_WEB_CONTENTS_DESTROYED:
      case chrome::NOTIFICATION_TAB_PARENTED:
      case chrome::NOTIFICATION_AUTOCOMPLETE_CONTROLLER_RESULT_READY:
      case chrome::NOTIFICATION_HISTORY_LOADED:
      case chrome::NOTIFICATION_HISTORY_URLS_MODIFIED:
      case chrome::NOTIFICATION_TEMPLATE_URL_SERVICE_LOADED:
        break;
      default:
        FAIL() << "Unexpected notification type";
    }
    base::MessageLoop::current()->Quit();
  }

  void BrowserAcceleratorsTest() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

    int tab_count = browser()->tab_strip_model()->count();

    // Create a new Tab.
    chrome::NewTab(browser());
    ASSERT_NO_FATAL_FAILURE(WaitForTabOpenOrClose(tab_count + 1));

    // Select the first Tab.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_1, kCtrlOrCmdMask));
    ASSERT_EQ(0, browser()->tab_strip_model()->active_index());

    chrome::FocusLocationBar(browser());

    // Select the second Tab.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_2, kCtrlOrCmdMask));
    ASSERT_EQ(1, browser()->tab_strip_model()->active_index());

    chrome::FocusLocationBar(browser());

    // Try ctrl-w to close a Tab.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_W, kCtrlOrCmdMask));
    ASSERT_NO_FATAL_FAILURE(WaitForTabOpenOrClose(tab_count));

    // Try ctrl-l to focus location bar.
    omnibox_view->SetUserText(ASCIIToUTF16("Hello world"));
    EXPECT_FALSE(omnibox_view->IsSelectAll());
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_L, kCtrlOrCmdMask));
    EXPECT_TRUE(omnibox_view->IsSelectAll());

    // Try editing the location bar text.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_RIGHT, 0));
    EXPECT_FALSE(omnibox_view->IsSelectAll());
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_S, 0));
    EXPECT_EQ(ASCIIToUTF16("Hello worlds"), omnibox_view->GetText());

    // Try ctrl-x to cut text.
#if defined(OS_MACOSX)
    // Mac uses alt-left/right to select a word.
    ASSERT_NO_FATAL_FAILURE(
        SendKey(ui::VKEY_LEFT, ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN));
#else
    ASSERT_NO_FATAL_FAILURE(
        SendKey(ui::VKEY_LEFT, ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN));
#endif
    EXPECT_FALSE(omnibox_view->IsSelectAll());
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_X, kCtrlOrCmdMask));
    EXPECT_EQ(ASCIIToUTF16("Hello "), omnibox_view->GetText());

#if !defined(OS_CHROMEOS) && !defined(OS_MACOSX)
    // Try alt-f4 to close the browser.
    ASSERT_TRUE(SendKeyAndWait(
        browser(), ui::VKEY_F4, ui::EF_ALT_DOWN,
        chrome::NOTIFICATION_BROWSER_CLOSED,
        content::Source<Browser>(browser())));
#endif
  }

  void PopupAcceleratorsTest() {
    // Create a popup.
    Browser* popup = CreateBrowserForPopup(browser()->profile());
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(popup));
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(
        GetOmniboxViewForBrowser(popup, &omnibox_view));
    chrome::FocusLocationBar(popup);
    EXPECT_TRUE(omnibox_view->IsSelectAll());

#if !defined(OS_MACOSX)
    // Try ctrl-w to close the popup.
    // This piece of code doesn't work on Mac, because the Browser object won't
    // be destroyed before finishing the current message loop iteration, thus
    // No BROWSER_CLOSED notification will be sent.
    ASSERT_TRUE(SendKeyAndWait(
        popup, ui::VKEY_W, ui::EF_CONTROL_DOWN,
        chrome::NOTIFICATION_BROWSER_CLOSED, content::Source<Browser>(popup)));

    // Create another popup.
    popup = CreateBrowserForPopup(browser()->profile());
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(popup));
    ASSERT_NO_FATAL_FAILURE(
        GetOmniboxViewForBrowser(popup, &omnibox_view));
#endif

    // Set the edit text to "Hello world".
    omnibox_view->SetUserText(ASCIIToUTF16("Hello world"));
    chrome::FocusLocationBar(popup);
    EXPECT_TRUE(omnibox_view->IsSelectAll());

    // Try editing the location bar text -- should be disallowed.
    ASSERT_NO_FATAL_FAILURE(SendKeyForBrowser(popup, ui::VKEY_S, 0));
    EXPECT_EQ(ASCIIToUTF16("Hello world"), omnibox_view->GetText());
    EXPECT_TRUE(omnibox_view->IsSelectAll());

    ASSERT_NO_FATAL_FAILURE(
        SendKeyForBrowser(popup, ui::VKEY_X, kCtrlOrCmdMask));
    EXPECT_EQ(ASCIIToUTF16("Hello world"), omnibox_view->GetText());
    EXPECT_TRUE(omnibox_view->IsSelectAll());

#if !defined(OS_CHROMEOS) && !defined(OS_MACOSX)
    // Try alt-f4 to close the popup.
    ASSERT_TRUE(SendKeyAndWait(
        popup, ui::VKEY_F4, ui::EF_ALT_DOWN,
        chrome::NOTIFICATION_BROWSER_CLOSED, content::Source<Browser>(popup)));
#endif
  }

  void BackspaceInKeywordModeTest() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

    // Trigger keyword hint mode.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchKeywordKeys));
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(kSearchKeyword, UTF16ToUTF8(omnibox_view->model()->keyword()));

    // Trigger keyword mode.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(kSearchKeyword, UTF16ToUTF8(omnibox_view->model()->keyword()));

    // Backspace without search text should bring back keyword hint mode.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(kSearchKeyword, UTF16ToUTF8(omnibox_view->model()->keyword()));

    // Trigger keyword mode again.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(kSearchKeyword, UTF16ToUTF8(omnibox_view->model()->keyword()));

    // Input something as search text.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchTextKeys));

    // Should stay in keyword mode while deleting search text by pressing
    // backspace.
    for (size_t i = 0; i < arraysize(kSearchText) - 1; ++i) {
      ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
      ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
      ASSERT_EQ(kSearchKeyword, UTF16ToUTF8(omnibox_view->model()->keyword()));
    }

    // Input something as search text.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchTextKeys));

    // Move cursor to the beginning of the search text.
#if defined(OS_MACOSX)
    // Home doesn't work on Mac trybot.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_A, ui::EF_CONTROL_DOWN));
#else
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_HOME, 0));
#endif
    // Backspace at the beginning of the search text shall turn off
    // the keyword mode.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(string16(), omnibox_view->model()->keyword());
    ASSERT_EQ(std::string(kSearchKeyword) + kSearchText,
              UTF16ToUTF8(omnibox_view->GetText()));
  }

  void EscapeTest() {
    ui_test_utils::NavigateToURL(browser(), GURL(chrome::kChromeUIHistoryURL));
    chrome::FocusLocationBar(browser());

    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

    string16 old_text = omnibox_view->GetText();
    EXPECT_FALSE(old_text.empty());
    EXPECT_TRUE(omnibox_view->IsSelectAll());

    // Delete all text in omnibox.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
    EXPECT_TRUE(omnibox_view->GetText().empty());

    // Escape shall revert the text in omnibox.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_ESCAPE, 0));
    EXPECT_EQ(old_text, omnibox_view->GetText());
    EXPECT_TRUE(omnibox_view->IsSelectAll());
  }

  void DesiredTLDTest() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
    OmniboxPopupModel* popup_model = omnibox_view->model()->popup_model();
    ASSERT_TRUE(popup_model);

    // Test ctrl-Enter.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kDesiredTLDKeys));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(popup_model->IsOpen());
    // ctrl-Enter triggers desired_tld feature, thus www.bar.com shall be
    // opened.
    ASSERT_TRUE(SendKeyAndWait(browser(), ui::VKEY_RETURN, ui::EF_CONTROL_DOWN,
        content::NOTIFICATION_NAV_ENTRY_COMMITTED,
        content::Source<content::NavigationController>(
            &browser()->tab_strip_model()->GetActiveWebContents()->
                GetController())));

    GURL url = browser()->tab_strip_model()->GetActiveWebContents()->GetURL();
    EXPECT_EQ(kDesiredTLDHostname, url.host());
    EXPECT_EQ("/", url.path());
  }

  void AltEnterTest() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

    omnibox_view->SetUserText(ASCIIToUTF16(chrome::kChromeUIHistoryURL));
    int tab_count = browser()->tab_strip_model()->count();
    // alt-Enter opens a new tab.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_RETURN, ui::EF_ALT_DOWN));
    ASSERT_NO_FATAL_FAILURE(WaitForTabOpenOrClose(tab_count + 1));
  }

  void EnterToSearchTest() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
    OmniboxPopupModel* popup_model = omnibox_view->model()->popup_model();
    ASSERT_TRUE(popup_model);

    // Test Enter to search.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchTextKeys));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(popup_model->IsOpen());

    // Check if the default match result is Search Primary Provider.
    ASSERT_EQ(AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED,
              popup_model->result().default_match()->type);

    // Open the default match.
    ASSERT_TRUE(SendKeyAndWait(browser(), ui::VKEY_RETURN, 0,
        content::NOTIFICATION_NAV_ENTRY_COMMITTED,
        content::Source<content::NavigationController>(
            &browser()->tab_strip_model()->GetActiveWebContents()->
                GetController())));
    GURL url = browser()->tab_strip_model()->GetActiveWebContents()->GetURL();
    EXPECT_EQ(kSearchTextURL, url.spec());

    // Test that entering a single character then Enter performs a search.
    chrome::FocusLocationBar(browser());
    EXPECT_TRUE(omnibox_view->IsSelectAll());
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchSingleCharKeys));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(popup_model->IsOpen());
    EXPECT_EQ(kSearchSingleChar, UTF16ToUTF8(omnibox_view->GetText()));

    // Check if the default match result is Search Primary Provider.
    ASSERT_EQ(AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED,
              popup_model->result().default_match()->type);

    // Open the default match.
    ASSERT_TRUE(SendKeyAndWait(browser(), ui::VKEY_RETURN, 0,
        content::NOTIFICATION_NAV_ENTRY_COMMITTED,
        content::Source<content::NavigationController>(
            &browser()->tab_strip_model()->GetActiveWebContents()->
                GetController())));
    url = browser()->tab_strip_model()->GetActiveWebContents()->GetURL();
    EXPECT_EQ(kSearchSingleCharURL, url.spec());
  }

  void EscapeToDefaultMatchTest() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
    OmniboxPopupModel* popup_model = omnibox_view->model()->popup_model();
    ASSERT_TRUE(popup_model);

    // Input something to trigger inline autocomplete.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kInlineAutocompleteTextKeys));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(popup_model->IsOpen());

    string16 old_text = omnibox_view->GetText();

    // Make sure inline autocomplete is triggerred.
    EXPECT_GT(old_text.length(), arraysize(kInlineAutocompleteText) - 1);

    size_t old_selected_line = popup_model->selected_line();
    EXPECT_EQ(0U, old_selected_line);

    // Move to another line with different text.
    size_t size = popup_model->result().size();
    while (popup_model->selected_line() < size - 1) {
      ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_DOWN, 0));
      ASSERT_NE(old_selected_line, popup_model->selected_line());
      if (old_text != omnibox_view->GetText())
        break;
    }

    EXPECT_NE(old_text, omnibox_view->GetText());

    // Escape shall revert back to the default match item.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_ESCAPE, 0));
    EXPECT_EQ(old_text, omnibox_view->GetText());
    EXPECT_EQ(old_selected_line, popup_model->selected_line());
  }

  void BasicTextOperationsTest() {
    ui_test_utils::NavigateToURL(browser(), GURL(content::kAboutBlankURL));
    chrome::FocusLocationBar(browser());

    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

    string16 old_text = omnibox_view->GetText();
    EXPECT_EQ(UTF8ToUTF16(content::kAboutBlankURL), old_text);
    EXPECT_TRUE(omnibox_view->IsSelectAll());

    size_t start, end;
    omnibox_view->GetSelectionBounds(&start, &end);
    EXPECT_EQ(0U, start);
    EXPECT_EQ(old_text.size(), end);

    // Move the cursor to the end.
#if defined(OS_MACOSX)
    // End doesn't work on Mac trybot.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_E, ui::EF_CONTROL_DOWN));
#else
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_END, 0));
#endif
    EXPECT_FALSE(omnibox_view->IsSelectAll());

    // Make sure the cursor is placed correctly.
    omnibox_view->GetSelectionBounds(&start, &end);
    EXPECT_EQ(old_text.size(), start);
    EXPECT_EQ(old_text.size(), end);

    // Insert one character at the end. Make sure we won't insert
    // anything after the special ZWS mark used in gtk implementation.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_A, 0));
    EXPECT_EQ(old_text + char16('a'), omnibox_view->GetText());

    // Delete one character from the end. Make sure we won't delete the special
    // ZWS mark used in gtk implementation.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
    EXPECT_EQ(old_text, omnibox_view->GetText());

    omnibox_view->SelectAll(true);
    EXPECT_TRUE(omnibox_view->IsSelectAll());
    omnibox_view->GetSelectionBounds(&start, &end);
    EXPECT_EQ(0U, start);
    EXPECT_EQ(old_text.size(), end);

    // Delete the content
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_DELETE, 0));
    EXPECT_TRUE(omnibox_view->IsSelectAll());
    omnibox_view->GetSelectionBounds(&start, &end);
    EXPECT_EQ(0U, start);
    EXPECT_EQ(0U, end);
    EXPECT_TRUE(omnibox_view->GetText().empty());

    // Check if RevertAll() can set text and cursor correctly.
    omnibox_view->RevertAll();
    EXPECT_FALSE(omnibox_view->IsSelectAll());
    EXPECT_EQ(old_text, omnibox_view->GetText());
    omnibox_view->GetSelectionBounds(&start, &end);
    EXPECT_EQ(old_text.size(), start);
    EXPECT_EQ(old_text.size(), end);
  }

  void AcceptKeywordBySpaceTest() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

    string16 search_keyword(ASCIIToUTF16(kSearchKeyword));

    // Trigger keyword hint mode.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchKeywordKeys));
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_EQ(search_keyword, omnibox_view->GetText());

    // Trigger keyword mode by space.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_SPACE, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_TRUE(omnibox_view->GetText().empty());

    // Revert to keyword hint mode.
    omnibox_view->model()->ClearKeyword(string16());
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_EQ(search_keyword, omnibox_view->GetText());

    // Keyword should also be accepted by typing an ideographic space.
    omnibox_view->OnBeforePossibleChange();
    omnibox_view->SetWindowTextAndCaretPos(search_keyword +
        WideToUTF16(L"\x3000"), search_keyword.length() + 1, false, false);
    omnibox_view->OnAfterPossibleChange();
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_TRUE(omnibox_view->GetText().empty());

    // Revert to keyword hint mode.
    omnibox_view->model()->ClearKeyword(string16());
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_EQ(search_keyword, omnibox_view->GetText());

    // Keyword shouldn't be accepted by pressing space with a trailing
    // whitespace.
    omnibox_view->SetWindowTextAndCaretPos(search_keyword + char16(' '),
        search_keyword.length() + 1, false, false);
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_SPACE, 0));
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_EQ(search_keyword + ASCIIToUTF16("  "), omnibox_view->GetText());

    // Keyword shouldn't be accepted by deleting the trailing space.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_EQ(search_keyword + char16(' '), omnibox_view->GetText());

    // Keyword shouldn't be accepted by pressing space before a trailing space.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_SPACE, 0));
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_EQ(search_keyword + ASCIIToUTF16("  "), omnibox_view->GetText());

    // Keyword should be accepted by pressing space in the middle of context and
    // just after the keyword.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_A, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_SPACE, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_EQ(ASCIIToUTF16("a "), omnibox_view->GetText());
    size_t start, end;
    omnibox_view->GetSelectionBounds(&start, &end);
    EXPECT_EQ(0U, start);
    EXPECT_EQ(0U, end);

    // Keyword shouldn't be accepted by pasting "foo bar".
    omnibox_view->SetUserText(string16());
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_TRUE(omnibox_view->model()->keyword().empty());

    omnibox_view->OnBeforePossibleChange();
    omnibox_view->model()->on_paste();
    omnibox_view->SetWindowTextAndCaretPos(search_keyword +
        ASCIIToUTF16(" bar"), search_keyword.length() + 4, false, false);
    omnibox_view->OnAfterPossibleChange();
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_TRUE(omnibox_view->model()->keyword().empty());
    ASSERT_EQ(search_keyword + ASCIIToUTF16(" bar"), omnibox_view->GetText());

    // Keyword shouldn't be accepted for case like: "foo b|ar" -> "foo b |ar".
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_SPACE, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_TRUE(omnibox_view->model()->keyword().empty());
    ASSERT_EQ(search_keyword + ASCIIToUTF16(" b ar"), omnibox_view->GetText());

    // Keyword could be accepted by pressing space with a selected range at the
    // end of text.
    omnibox_view->OnBeforePossibleChange();
    omnibox_view->OnInlineAutocompleteTextMaybeChanged(
        search_keyword + ASCIIToUTF16("  "), search_keyword.length());
    omnibox_view->OnAfterPossibleChange();
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_EQ(search_keyword + ASCIIToUTF16("  "), omnibox_view->GetText());

    omnibox_view->GetSelectionBounds(&start, &end);
    ASSERT_NE(start, end);
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_SPACE, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_EQ(string16(), omnibox_view->GetText());

    // Space should accept keyword even when inline autocomplete is available.
    omnibox_view->SetUserText(string16());
    const TestHistoryEntry kHistoryFoobar = {
      "http://www.foobar.com", "Page foobar", kSearchText, 100, 100, true
    };

    // Add a history entry to trigger inline autocomplete when typing "foo".
    ASSERT_NO_FATAL_FAILURE(
        AddHistoryEntry(kHistoryFoobar, Time::Now() - TimeDelta::FromHours(1)));

    // Type "foo" to trigger inline autocomplete.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchKeywordKeys));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(omnibox_view->model()->popup_model()->IsOpen());
    ASSERT_NE(search_keyword, omnibox_view->GetText());

    // Keyword hint shouldn't be visible.
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_TRUE(omnibox_view->model()->keyword().empty());

    // Trigger keyword mode by space.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_SPACE, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_TRUE(omnibox_view->GetText().empty());

    // Space in the middle of a temporary text, which separates the text into
    // keyword and replacement portions, should trigger keyword mode.
    omnibox_view->SetUserText(string16());
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchKeywordKeys));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    OmniboxPopupModel* popup_model = omnibox_view->model()->popup_model();
    ASSERT_TRUE(popup_model->IsOpen());
    ASSERT_EQ(ASCIIToUTF16("foobar.com"), omnibox_view->GetText());
    omnibox_view->model()->OnUpOrDownKeyPressed(1);
    omnibox_view->model()->OnUpOrDownKeyPressed(-1);
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_SPACE, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword, omnibox_view->model()->keyword());
    ASSERT_EQ(ASCIIToUTF16("bar.com"), omnibox_view->GetText());

    // Space after temporary text that looks like a keyword, when the original
    // input does not look like a keyword, should trigger keyword mode.
    omnibox_view->SetUserText(string16());
    const TestHistoryEntry kHistoryFoo = {
      "http://footest.com", "Page footest", kSearchText, 1000, 1000, true
    };

    // Add a history entry to trigger HQP matching with text == keyword when
    // typing "fo te".
    ASSERT_NO_FATAL_FAILURE(
        AddHistoryEntry(kHistoryFoo, Time::Now() - TimeDelta::FromMinutes(10)));

    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_F, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_O, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_SPACE, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_T, 0));
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_E, 0));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(popup_model->IsOpen());
    string16 search_keyword2(ASCIIToUTF16(kSearchKeyword2));
    while ((omnibox_view->GetText() != search_keyword2) &&
           (popup_model->selected_line() < popup_model->result().size() - 1))
      omnibox_view->model()->OnUpOrDownKeyPressed(1);
    ASSERT_EQ(search_keyword2, omnibox_view->GetText());
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_SPACE, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(search_keyword2, omnibox_view->model()->keyword());
    ASSERT_TRUE(omnibox_view->GetText().empty());
  }

  void NonSubstitutingKeywordTest() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
    OmniboxPopupModel* popup_model = omnibox_view->model()->popup_model();
    ASSERT_TRUE(popup_model);

    Profile* profile = browser()->profile();
    TemplateURLService* template_url_service =
        TemplateURLServiceFactory::GetForProfile(profile);

    // Add a non-default substituting keyword.
    TemplateURLData data;
    data.short_name = ASCIIToUTF16("Search abc");
    data.SetKeyword(ASCIIToUTF16(kSearchText));
    data.SetURL("http://abc.com/{searchTerms}");
    TemplateURL* template_url = new TemplateURL(profile, data);
    template_url_service->Add(template_url);

    omnibox_view->SetUserText(string16());

    // Non-default substituting keyword shouldn't be matched by default.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchTextKeys));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(popup_model->IsOpen());

    // Check if the default match result is Search Primary Provider.
    ASSERT_EQ(AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED,
              popup_model->result().default_match()->type);
    ASSERT_EQ(kSearchTextURL,
              popup_model->result().default_match()->destination_url.spec());

    omnibox_view->SetUserText(string16());
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_FALSE(popup_model->IsOpen());

    // Try a non-substituting keyword.
    template_url_service->Remove(template_url);
    data.short_name = ASCIIToUTF16("abc");
    data.SetURL("http://abc.com/");
    template_url_service->Add(new TemplateURL(profile, data));

    // We always allow exact matches for non-substituting keywords.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchTextKeys));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(popup_model->IsOpen());
    ASSERT_EQ(AutocompleteMatchType::HISTORY_KEYWORD,
              popup_model->result().default_match()->type);
    ASSERT_EQ("http://abc.com/",
              popup_model->result().default_match()->destination_url.spec());
  }

  void DeleteItemTest() {
    // Disable the search provider, to make sure the popup contains only history
    // items.
    TemplateURLService* model =
        TemplateURLServiceFactory::GetForProfile(browser()->profile());
    model->SetDefaultSearchProvider(NULL);

    ui_test_utils::NavigateToURL(browser(), GURL(content::kAboutBlankURL));
    chrome::FocusLocationBar(browser());

    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

    OmniboxPopupModel* popup_model = omnibox_view->model()->popup_model();
    ASSERT_TRUE(popup_model);

    string16 old_text = omnibox_view->GetText();

    // Input something that can match history items.
    omnibox_view->SetUserText(ASCIIToUTF16("site.com/p"));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(popup_model->IsOpen());

    // Delete the inline autocomplete part.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_DELETE, 0));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(popup_model->IsOpen());
    ASSERT_GE(popup_model->result().size(), 3U);

    string16 user_text = omnibox_view->GetText();
    ASSERT_EQ(ASCIIToUTF16("site.com/p"), user_text);
    omnibox_view->SelectAll(true);
    ASSERT_TRUE(omnibox_view->IsSelectAll());

    // The first item should be the default match.
    size_t default_line = popup_model->selected_line();
    std::string default_url =
        popup_model->result().match_at(default_line).destination_url.spec();

    // Move down.
    omnibox_view->model()->OnUpOrDownKeyPressed(1);
    ASSERT_EQ(default_line + 1, popup_model->selected_line());
    string16 selected_text =
        popup_model->result().match_at(default_line + 1).fill_into_edit;
    // Temporary text is shown.
    ASSERT_EQ(selected_text, omnibox_view->GetText());
    ASSERT_FALSE(omnibox_view->IsSelectAll());

    // Delete the item.
    popup_model->TryDeletingCurrentItem();
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    // The selected line shouldn't be changed, because we have more than two
    // items.
    ASSERT_EQ(default_line + 1, popup_model->selected_line());
    // Make sure the item is really deleted.
    ASSERT_NE(selected_text,
              popup_model->result().match_at(default_line + 1).fill_into_edit);
    selected_text =
        popup_model->result().match_at(default_line + 1).fill_into_edit;
    // New temporary text is shown.
    ASSERT_EQ(selected_text, omnibox_view->GetText());

    // Revert to the default match.
    ASSERT_TRUE(omnibox_view->model()->OnEscapeKeyPressed());
    ASSERT_EQ(default_line, popup_model->selected_line());
    ASSERT_EQ(user_text, omnibox_view->GetText());
    ASSERT_TRUE(omnibox_view->IsSelectAll());

    // Move down and up to select the default match as temporary text.
    omnibox_view->model()->OnUpOrDownKeyPressed(1);
    ASSERT_EQ(default_line + 1, popup_model->selected_line());
    omnibox_view->model()->OnUpOrDownKeyPressed(-1);
    ASSERT_EQ(default_line, popup_model->selected_line());

    selected_text = popup_model->result().match_at(default_line).fill_into_edit;
    // New temporary text is shown.
    ASSERT_EQ(selected_text, omnibox_view->GetText());
    ASSERT_FALSE(omnibox_view->IsSelectAll());

#if 0
    // TODO(mrossetti): http://crbug.com/82335
    // Delete the default item.
    popup_model->TryDeletingCurrentItem();
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    // The selected line shouldn't be changed, but the default item should have
    // been changed.
    ASSERT_EQ(default_line, popup_model->selected_line());
    // Make sure the item is really deleted.
    EXPECT_NE(selected_text,
              popup_model->result().match_at(default_line).fill_into_edit);
    selected_text =
        popup_model->result().match_at(default_line).fill_into_edit;
    // New temporary text is shown.
    ASSERT_EQ(selected_text, omnibox_view->GetText());
#endif

    // As the current selected item is the new default item, pressing Escape key
    // should revert all directly.
    ASSERT_TRUE(omnibox_view->model()->OnEscapeKeyPressed());
    ASSERT_EQ(old_text, omnibox_view->GetText());
    ASSERT_TRUE(omnibox_view->IsSelectAll());
  }

  void TabAcceptKeyword() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

    string16 text = ASCIIToUTF16(kSearchKeyword);

    // Trigger keyword hint mode.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchKeywordKeys));
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(text, omnibox_view->model()->keyword());
    ASSERT_EQ(text, omnibox_view->GetText());

    // Trigger keyword mode by tab.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(text, omnibox_view->model()->keyword());
    ASSERT_TRUE(omnibox_view->GetText().empty());

    // Revert to keyword hint mode.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(text, omnibox_view->model()->keyword());
    ASSERT_EQ(text, omnibox_view->GetText());

    // The location bar should still have focus.
    ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

    // Trigger keyword mode by tab.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(text, omnibox_view->model()->keyword());
    ASSERT_TRUE(omnibox_view->GetText().empty());

    // Revert to keyword hint mode with SHIFT+TAB.
#if defined(OS_MACOSX)
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACKTAB, 0));
#else
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN));
#endif
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(text, omnibox_view->model()->keyword());
    ASSERT_EQ(text, omnibox_view->GetText());
    ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  }

  void TabTraverseResultsTest() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
    OmniboxPopupModel* popup_model = omnibox_view->model()->popup_model();
    ASSERT_TRUE(popup_model);

    // Input something to trigger results.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kDesiredTLDKeys));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(popup_model->IsOpen());

    size_t old_selected_line = popup_model->selected_line();
    EXPECT_EQ(0U, old_selected_line);

    // Move down the results.
    for (size_t size = popup_model->result().size();
         popup_model->selected_line() < size - 1;
         old_selected_line = popup_model->selected_line()) {
      ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, 0));
      ASSERT_LT(old_selected_line, popup_model->selected_line());
    }

    // Don't move past the end.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, 0));
    ASSERT_EQ(old_selected_line, popup_model->selected_line());
    ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

    // Move back up the results.
    for (; popup_model->selected_line() > 0U;
         old_selected_line = popup_model->selected_line()) {
      ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN));
      ASSERT_GT(old_selected_line, popup_model->selected_line());
    }

    // Don't move past the beginning.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN));
    ASSERT_EQ(0U, popup_model->selected_line());
    ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

    const TestHistoryEntry kHistoryFoo = {
      "http://foo/", "Page foo", kSearchText, 1, 1, false
    };

    // Add a history entry so "foo" gets multiple matches.
    ASSERT_NO_FATAL_FAILURE(
        AddHistoryEntry(kHistoryFoo, Time::Now() - TimeDelta::FromHours(1)));

    // Load results.
    ASSERT_NO_FATAL_FAILURE(omnibox_view->SelectAll(false));
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchKeywordKeys));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());

    // Trigger keyword mode by tab.
    string16 text = ASCIIToUTF16(kSearchKeyword);
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(text, omnibox_view->model()->keyword());
    ASSERT_TRUE(omnibox_view->GetText().empty());

    // The location bar should still have focus.
    ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

    // Pressing tab again should move to the next result and clear keyword
    // mode.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, 0));
    ASSERT_EQ(1U, omnibox_view->model()->popup_model()->selected_line());
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_NE(text, omnibox_view->model()->keyword());

    // The location bar should still have focus.
    ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

    // Moving back up should not show keyword mode.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, ui::EF_SHIFT_DOWN));
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(text, omnibox_view->model()->keyword());

    ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  }

  void PersistKeywordModeOnTabSwitch() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

    // Trigger keyword hint mode.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchKeywordKeys));
    ASSERT_TRUE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(kSearchKeyword, UTF16ToUTF8(omnibox_view->model()->keyword()));

    // Trigger keyword mode.
    ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_TAB, 0));
    ASSERT_FALSE(omnibox_view->model()->is_keyword_hint());
    ASSERT_EQ(kSearchKeyword, UTF16ToUTF8(omnibox_view->model()->keyword()));

    // Input something as search text.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kSearchTextKeys));

    // Create a new tab.
    chrome::NewTab(browser());

    // Switch back to the first tab.
    browser()->tab_strip_model()->ActivateTabAt(0, true);

    // Make sure we're still in keyword mode.
    ASSERT_EQ(kSearchKeyword, UTF16ToUTF8(omnibox_view->model()->keyword()));
  }

  void CtrlKeyPressedWithInlineAutocompleteTest() {
    OmniboxView* omnibox_view = NULL;
    ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
    OmniboxPopupModel* popup_model = omnibox_view->model()->popup_model();
    ASSERT_TRUE(popup_model);

    // Input something to trigger inline autocomplete.
    ASSERT_NO_FATAL_FAILURE(SendKeySequence(kInlineAutocompleteTextKeys));
    ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
    ASSERT_TRUE(popup_model->IsOpen());

    string16 old_text = omnibox_view->GetText();

    // Make sure inline autocomplete is triggerred.
    EXPECT_GT(old_text.length(), arraysize(kInlineAutocompleteText) - 1);

    // Press ctrl key.
    omnibox_view->model()->OnControlKeyChanged(true);

    // Inline autocomplete should still be there.
    EXPECT_EQ(old_text, omnibox_view->GetText());
  }

#if defined(TOOLKIT_VIEWS)
  const BrowserView* GetBrowserView() const {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  // Move the mouse to the center of the browser window and left-click.
  void ClickBrowserWindowCenter() {
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
                    GetBrowserView()->GetBoundsInScreen().CenterPoint()));
    ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
                    ui_controls::LEFT, ui_controls::DOWN));
    ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
                    ui_controls::LEFT, ui_controls::UP));
  }

  // Press and release the mouse in the omnibox at an offset from its origin.
  // If |release_offset| differs from |press_offset|, the mouse will be moved
  // between the press and release.
  void ClickOmnibox(ui_controls::MouseButton button,
                    const gfx::Vector2d& press_offset,
                    const gfx::Vector2d& release_offset) {
    const views::View* omnibox = GetBrowserView()->GetViewByID(VIEW_ID_OMNIBOX);
    gfx::Point omnibox_origin = omnibox->GetBoundsInScreen().origin();
    gfx::Point press_point = omnibox_origin + press_offset;
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(press_point));
    ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(button, ui_controls::DOWN));

    gfx::Point release_point = omnibox_origin + release_offset;
    if (release_point != press_point)
      ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(release_point));
    ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(button, ui_controls::UP));
  }
#endif  // defined(TOOLKIT_VIEWS)
};

// Test if ctrl-* accelerators are workable in omnibox.
// See http://crbug.com/19193: omnibox blocks ctrl-* commands
//
// Flaky on interactive tests (dbg), http://crbug.com/69433
IN_PROC_BROWSER_TEST_F(OmniboxViewTest, DISABLED_BrowserAccelerators) {
  BrowserAcceleratorsTest();
}

// Flakily fails and times out on Win only.  http://crbug.com/69941
#if defined(OS_WIN)
#define MAYBE_PopupAccelerators DISABLED_PopupAccelerators
#else
#define MAYBE_PopupAccelerators PopupAccelerators
#endif

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, MAYBE_PopupAccelerators) {
  PopupAcceleratorsTest();
}

// http://crbug.com/133341
#if defined(OS_LINUX)
#define MAYBE_BackspaceInKeywordMode DISABLED_BackspaceInKeywordMode
#else
#define MAYBE_BackspaceInKeywordMode BackspaceInKeywordMode
#endif

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, MAYBE_BackspaceInKeywordMode) {
  BackspaceInKeywordModeTest();
}

// http://crbug.com/158913
#if defined(OS_CHROMEOS)
#define MAYBE_Escape DISABLED_Escape
#else
#define MAYBE_Escape Escape
#endif
IN_PROC_BROWSER_TEST_F(OmniboxViewTest, MAYBE_Escape) {
  EscapeTest();
}
#undef MAYBE_ESCAPE

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, DesiredTLD) {
  DesiredTLDTest();
}

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, AltEnter) {
  AltEnterTest();
}

// http://crbug.com/133354, http://crbug.com/146953
IN_PROC_BROWSER_TEST_F(OmniboxViewTest, DISABLED_EnterToSearch) {
  EnterToSearchTest();
}

// http://crbug.com/131179
#if defined(OS_LINUX)
#define MAYBE_EscapeToDefaultMatch DISABLED_EscapeToDefaultMatch
#else
#define MAYBE_EscapeToDefaultMatch EscapeToDefaultMatch
#endif
IN_PROC_BROWSER_TEST_F(OmniboxViewTest, MAYBE_EscapeToDefaultMatch) {
  EscapeToDefaultMatchTest();
}

// http://crbug.com/131179, http://crbug.com/146619
#if defined(OS_LINUX) || defined(OS_WIN)
#define MAYBE_BasicTextOperations DISABLED_BasicTextOperations
#else
#define MAYBE_BasicTextOperations BasicTextOperations
#endif
IN_PROC_BROWSER_TEST_F(OmniboxViewTest, MAYBE_BasicTextOperations) {
  BasicTextOperationsTest();
}

// http://crbug.com/131179
#if defined(OS_LINUX)
#define MAYBE_AcceptKeywordBySpace DISABLED_AcceptKeywordBySpace
#else
#define MAYBE_AcceptKeywordBySpace AcceptKeywordBySpace
#endif

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, MAYBE_AcceptKeywordBySpace) {
  AcceptKeywordBySpaceTest();
}

// http://crbug.com/131179
#if defined(OS_LINUX)
#define MAYBE_NonSubstitutingKeywordTest DISABLED_NonSubstitutingKeywordTest
#else
#define MAYBE_NonSubstitutingKeywordTest NonSubstitutingKeywordTest
#endif

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, MAYBE_NonSubstitutingKeywordTest) {
  NonSubstitutingKeywordTest();
}

// http://crbug.com/131179 http://crbug.com/165765
#if defined(OS_LINUX) || defined(OS_WIN) || defined(OS_MACOSX)
#define MAYBE_DeleteItem DISABLED_DeleteItem
#else
#define MAYBE_DeleteItem DeleteItem
#endif
IN_PROC_BROWSER_TEST_F(OmniboxViewTest, MAYBE_DeleteItem) {
  DeleteItemTest();
}

// http://crbug.com/133344
#if defined(OS_LINUX)
#define MAYBE_TabAcceptKeyword DISABLED_TabAcceptKeyword
#else
#define MAYBE_TabAcceptKeyword TabAcceptKeyword
#endif

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, MAYBE_TabAcceptKeyword) {
  TabAcceptKeyword();
}

#if !defined(OS_MACOSX)
// Mac intentionally does not support this behavior.

// http://crbug.com/133360
#if defined(OS_LINUX)
#define MAYBE_TabTraverseResultsTest DISABLED_TabTraverseResultsTest
#else
#define MAYBE_TabTraverseResultsTest TabTraverseResultsTest
#endif

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, MAYBE_TabTraverseResultsTest) {
  TabTraverseResultsTest();
}
#endif


// http://crbug.com/133347
#if defined(OS_LINUX)
#define MAYBE_PersistKeywordModeOnTabSwitch DISABLED_PersistKeywordModeOnTabSwitch
#else
#define MAYBE_PersistKeywordModeOnTabSwitch PersistKeywordModeOnTabSwitch
#endif

IN_PROC_BROWSER_TEST_F(OmniboxViewTest,
                       MAYBE_PersistKeywordModeOnTabSwitch) {
  PersistKeywordModeOnTabSwitch();
}

// http://crbug.com/133355
#if defined(OS_LINUX)
#define MAYBE_CtrlKeyPressedWithInlineAutocompleteTest DISABLED_CtrlKeyPressedWithInlineAutocompleteTest
#else
#define MAYBE_CtrlKeyPressedWithInlineAutocompleteTest CtrlKeyPressedWithInlineAutocompleteTest
#endif

IN_PROC_BROWSER_TEST_F(OmniboxViewTest,
                       MAYBE_CtrlKeyPressedWithInlineAutocompleteTest) {
  CtrlKeyPressedWithInlineAutocompleteTest();
}

#if defined(TOOLKIT_GTK) || defined(TOOLKIT_VIEWS)
IN_PROC_BROWSER_TEST_F(OmniboxViewTest, UndoRedo) {
  ui_test_utils::NavigateToURL(browser(), GURL(content::kAboutBlankURL));
  chrome::FocusLocationBar(browser());

  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

  string16 old_text = omnibox_view->GetText();
  EXPECT_EQ(UTF8ToUTF16(content::kAboutBlankURL), old_text);
  EXPECT_TRUE(omnibox_view->IsSelectAll());

  // Undo should clear the omnibox.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_Z, ui::EF_CONTROL_DOWN));
  EXPECT_TRUE(omnibox_view->GetText().empty());

  // Nothing should happen if undo again.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_Z, ui::EF_CONTROL_DOWN));
  EXPECT_TRUE(omnibox_view->GetText().empty());

  // Redo should restore the original text.
  ASSERT_NO_FATAL_FAILURE(
      SendKey(ui::VKEY_Z, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN));
  EXPECT_EQ(old_text, omnibox_view->GetText());

  // Looks like the undo manager doesn't support restoring selection.
  EXPECT_FALSE(omnibox_view->IsSelectAll());

  // The cursor should be at the end.
  size_t start, end;
  omnibox_view->GetSelectionBounds(&start, &end);
  EXPECT_EQ(old_text.size(), start);
  EXPECT_EQ(old_text.size(), end);

  // Delete two characters.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
  EXPECT_EQ(old_text.substr(0, old_text.size() - 2), omnibox_view->GetText());

  // Undo delete.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_Z, ui::EF_CONTROL_DOWN));
  EXPECT_EQ(old_text, omnibox_view->GetText());

  // Redo delete.
  ASSERT_NO_FATAL_FAILURE(
      SendKey(ui::VKEY_Z, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN));
  EXPECT_EQ(old_text.substr(0, old_text.size() - 2), omnibox_view->GetText());

  // Delete everything.
  omnibox_view->SelectAll(true);
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
  EXPECT_TRUE(omnibox_view->GetText().empty());

  // Undo delete everything.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_Z, ui::EF_CONTROL_DOWN));
  EXPECT_EQ(old_text.substr(0, old_text.size() - 2), omnibox_view->GetText());

  // Undo delete two characters.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_Z, ui::EF_CONTROL_DOWN));
  EXPECT_EQ(old_text, omnibox_view->GetText());

  // Undo again.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_Z, ui::EF_CONTROL_DOWN));
  EXPECT_TRUE(omnibox_view->GetText().empty());
}

// See http://crosbug.com/10306
IN_PROC_BROWSER_TEST_F(OmniboxViewTest,
                       BackspaceDeleteHalfWidthKatakana) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
  // Insert text: ﾀﾞ
  omnibox_view->SetUserText(UTF8ToUTF16("\357\276\200\357\276\236"));

  // Move the cursor to the end.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_END, 0));

  // Backspace should delete one character.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_BACK, 0));
  EXPECT_EQ(UTF8ToUTF16("\357\276\200"), omnibox_view->GetText());
}
#endif  // defined(TOOLKIT_GTK) || defined(TOOLKIT_VIEWS)

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, DoesNotUpdateAutocompleteOnBlur) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
  OmniboxPopupModel* popup_model = omnibox_view->model()->popup_model();
  ASSERT_TRUE(popup_model);

  // Input something to trigger inline autocomplete.
  ASSERT_NO_FATAL_FAILURE(SendKeySequence(kInlineAutocompleteTextKeys));
  ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
  ASSERT_TRUE(popup_model->IsOpen());
  size_t start, end;
  omnibox_view->GetSelectionBounds(&start, &end);
  EXPECT_TRUE(start != end);
  string16 old_autocomplete_text =
      omnibox_view->model()->autocomplete_controller()->input().text();

  // Unfocus the omnibox. This should clear the text field selection and
  // close the popup, but should not run autocomplete.
  // Note: GTK preserves the selection when the omnibox is unfocused.
  ui_test_utils::ClickOnView(browser(), VIEW_ID_TAB_CONTAINER);
  ASSERT_FALSE(popup_model->IsOpen());
  omnibox_view->GetSelectionBounds(&start, &end);
#if !defined(TOOLKIT_GTK)
  EXPECT_TRUE(start == end);
#endif

  EXPECT_EQ(old_autocomplete_text,
      omnibox_view->model()->autocomplete_controller()->input().text());
}

#if defined(TOOLKIT_GTK)
// See http://crbug.com/63860
IN_PROC_BROWSER_TEST_F(OmniboxViewTest, PrimarySelection) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
  omnibox_view->SetUserText(ASCIIToUTF16("Hello world"));
  EXPECT_FALSE(omnibox_view->IsSelectAll());

  // Move the cursor to the end.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_END, 0));

  // Select all text by pressing Shift+Home
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_HOME, ui::EF_SHIFT_DOWN));
  EXPECT_TRUE(omnibox_view->IsSelectAll());

  // The selected content should be saved to the PRIMARY clipboard.
  EXPECT_EQ("Hello world", GetPrimarySelectionText());

  // Move the cursor to the end.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_END, 0));
  EXPECT_FALSE(omnibox_view->IsSelectAll());

  // The content in the PRIMARY clipboard should not be cleared.
  EXPECT_EQ("Hello world", GetPrimarySelectionText());
}
#endif  // defined(TOOLKIT_GTK)

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, Paste) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
  OmniboxPopupModel* popup_model = omnibox_view->model()->popup_model();
  ASSERT_TRUE(popup_model);
  EXPECT_FALSE(popup_model->IsOpen());

  // Paste should yield the expected text and open the popup.
  SetClipboardText(ASCIIToUTF16(kSearchText));
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_V, kCtrlOrCmdMask));
  ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
  EXPECT_EQ(ASCIIToUTF16(kSearchText), omnibox_view->GetText());
  EXPECT_TRUE(popup_model->IsOpen());

  // Close the popup and select all.
  omnibox_view->CloseOmniboxPopup();
  omnibox_view->SelectAll(false);
  EXPECT_FALSE(popup_model->IsOpen());

  // Pasting the same text again over itself should re-open the popup.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_V, kCtrlOrCmdMask));
  ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
  EXPECT_EQ(ASCIIToUTF16(kSearchText), omnibox_view->GetText());
  // This fails on GTK, see http://crbug.com/131179
#if !defined(TOOLKIT_GTK)
  EXPECT_TRUE(popup_model->IsOpen());
#endif
  omnibox_view->CloseOmniboxPopup();
  EXPECT_FALSE(popup_model->IsOpen());

  // Pasting amid text should yield the expected text and re-open the popup.
  omnibox_view->SetWindowTextAndCaretPos(ASCIIToUTF16("abcd"), 2, false, false);
  SetClipboardText(ASCIIToUTF16("123"));
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_V, kCtrlOrCmdMask));
  EXPECT_EQ(ASCIIToUTF16("ab123cd"), omnibox_view->GetText());
  EXPECT_TRUE(popup_model->IsOpen());
}

#if defined(TOOLKIT_VIEWS)
IN_PROC_BROWSER_TEST_F(OmniboxViewTest, SelectAllOnClick) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
  omnibox_view->SetUserText(ASCIIToUTF16("http://www.google.com/"));
  const gfx::Vector2d click(40, 10);

  // Take the focus away from the omnibox.
  ASSERT_NO_FATAL_FAILURE(ClickBrowserWindowCenter());
  EXPECT_FALSE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  EXPECT_FALSE(omnibox_view->IsSelectAll());

  // Clicking in the omnibox should take focus and select all text.
  ASSERT_NO_FATAL_FAILURE(ClickOmnibox(ui_controls::LEFT, click, click));
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  EXPECT_TRUE(omnibox_view->IsSelectAll());

  // Clicking in another view should clear focus and the selection.
  ASSERT_NO_FATAL_FAILURE(ClickBrowserWindowCenter());
  EXPECT_FALSE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  EXPECT_FALSE(omnibox_view->IsSelectAll());

  // Clicking in the omnibox again should take focus and select all text again.
  ASSERT_NO_FATAL_FAILURE(ClickOmnibox(ui_controls::LEFT, click, click));
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  EXPECT_TRUE(omnibox_view->IsSelectAll());

  // Clicking another omnibox spot should keep focus but clear the selection.
  omnibox_view->SelectAll(false);
  const gfx::Vector2d click_2(click.x() + 10, click.y());
  ASSERT_NO_FATAL_FAILURE(ClickOmnibox(ui_controls::LEFT, click_2, click_2));
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  EXPECT_FALSE(omnibox_view->IsSelectAll());

  // Take the focus away and click in the omnibox again, but drag a bit before
  // releasing.  We should focus the omnibox but not select all of its text.
  ASSERT_NO_FATAL_FAILURE(ClickBrowserWindowCenter());
  const gfx::Vector2d release(click.x() + 10, click.y());
  ASSERT_NO_FATAL_FAILURE(ClickOmnibox(ui_controls::LEFT, click, release));
  EXPECT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  EXPECT_FALSE(omnibox_view->IsSelectAll());

  // Middle-clicking should not be handled by the omnibox.
  ASSERT_NO_FATAL_FAILURE(ClickBrowserWindowCenter());
  ASSERT_NO_FATAL_FAILURE(ClickOmnibox(ui_controls::MIDDLE, click, click));
  EXPECT_FALSE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  EXPECT_FALSE(omnibox_view->IsSelectAll());
}
#endif  // defined(TOOLKIT_VIEWS)

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, CopyURLToClipboard) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
  const char* target_url = "http://www.google.com/calendar";
  omnibox_view->SetUserText(ASCIIToUTF16(target_url));

  // Set permanent text thus making sure that omnibox treats 'google.com'
  // as URL (not as ordinary user input).
  OmniboxEditModel* edit_model = omnibox_view->model();
  ASSERT_NE(static_cast<OmniboxEditModel*>(NULL), edit_model);
  edit_model->UpdatePermanentText(ASCIIToUTF16("http://www.google.com/"));

  // Location bar must have focus.
  chrome::FocusLocationBar(browser());
  ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

  // Select full URL and copy it to clipboard. General text and html should
  // be available.
  omnibox_view->SelectAll(true);
  EXPECT_TRUE(omnibox_view->IsSelectAll());
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  clipboard->Clear(ui::Clipboard::BUFFER_STANDARD);
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_COPY));
  EXPECT_EQ(ASCIIToUTF16(target_url), omnibox_view->GetText());
  EXPECT_TRUE(clipboard->IsFormatAvailable(
      ui::Clipboard::GetPlainTextFormatType(), ui::Clipboard::BUFFER_STANDARD));

  // The Mac is the only platform which doesn't write html.
#if !defined(OS_MACOSX)
  EXPECT_TRUE(clipboard->IsFormatAvailable(
      ui::Clipboard::GetHtmlFormatType(), ui::Clipboard::BUFFER_STANDARD));
#endif

  // These platforms should read bookmark format.
#if defined(OS_WIN) || defined(OS_CHROMEOS) || defined(OS_MACOSX)
  string16 title;
  std::string url;
  clipboard->ReadBookmark(&title, &url);
  EXPECT_EQ(target_url, url);
  EXPECT_EQ(ASCIIToUTF16(target_url), title);
#endif
}

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, CutURLToClipboard) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
  const char* target_url = "http://www.google.com/calendar";
  omnibox_view->SetUserText(ASCIIToUTF16(target_url));

  // Set permanent text thus making sure that omnibox treats 'google.com'
  // as URL (not as ordinary user input).
  OmniboxEditModel* edit_model = omnibox_view->model();
  ASSERT_NE(static_cast<OmniboxEditModel*>(NULL), edit_model);
  edit_model->UpdatePermanentText(ASCIIToUTF16("http://www.google.com/"));

  // Location bar must have focus.
  chrome::FocusLocationBar(browser());
  ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

  // Select full URL and cut it. General text and html should be available
  // in the clipboard.
  omnibox_view->SelectAll(true);
  EXPECT_TRUE(omnibox_view->IsSelectAll());
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  clipboard->Clear(ui::Clipboard::BUFFER_STANDARD);
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_CUT));
  EXPECT_EQ(string16(), omnibox_view->GetText());
  EXPECT_TRUE(clipboard->IsFormatAvailable(
      ui::Clipboard::GetPlainTextFormatType(), ui::Clipboard::BUFFER_STANDARD));

  // The Mac is the only platform which doesn't write html.
#if !defined(OS_MACOSX)
  EXPECT_TRUE(clipboard->IsFormatAvailable(
      ui::Clipboard::GetHtmlFormatType(), ui::Clipboard::BUFFER_STANDARD));
#endif

  // These platforms should read bookmark format.
#if defined(OS_WIN) || defined(OS_CHROMEOS) || defined(OS_MACOSX)
  string16 title;
  std::string url;
  clipboard->ReadBookmark(&title, &url);
  EXPECT_EQ(target_url, url);
  EXPECT_EQ(ASCIIToUTF16(target_url), title);
#endif
}

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, CopyTextToClipboard) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
  const char* target_text = "foo";
  omnibox_view->SetUserText(ASCIIToUTF16(target_text));

  // Location bar must have focus.
  chrome::FocusLocationBar(browser());
  ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

  // Select full text and copy it to the clipboard.
  omnibox_view->SelectAll(true);
  EXPECT_TRUE(omnibox_view->IsSelectAll());
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  clipboard->Clear(ui::Clipboard::BUFFER_STANDARD);
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_COPY));
  EXPECT_TRUE(clipboard->IsFormatAvailable(
      ui::Clipboard::GetPlainTextFormatType(), ui::Clipboard::BUFFER_STANDARD));
  EXPECT_FALSE(clipboard->IsFormatAvailable(
      ui::Clipboard::GetHtmlFormatType(), ui::Clipboard::BUFFER_STANDARD));
  EXPECT_EQ(ASCIIToUTF16(target_text), omnibox_view->GetText());
}

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, CutTextToClipboard) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
  const char* target_text = "foo";
  omnibox_view->SetUserText(ASCIIToUTF16(target_text));

  // Location bar must have focus.
  chrome::FocusLocationBar(browser());
  ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

  // Select full text and cut it to the clipboard.
  omnibox_view->SelectAll(true);
  EXPECT_TRUE(omnibox_view->IsSelectAll());
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  clipboard->Clear(ui::Clipboard::BUFFER_STANDARD);
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_CUT));
  EXPECT_TRUE(clipboard->IsFormatAvailable(
      ui::Clipboard::GetPlainTextFormatType(), ui::Clipboard::BUFFER_STANDARD));
  EXPECT_FALSE(clipboard->IsFormatAvailable(
      ui::Clipboard::GetHtmlFormatType(), ui::Clipboard::BUFFER_STANDARD));
  EXPECT_EQ(string16(), omnibox_view->GetText());
}

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, EditSearchEngines) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_EDIT_SEARCH_ENGINES));
  ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
  const std::string target_url =
      std::string(chrome::kChromeUISettingsURL) + chrome::kSearchEnginesSubPage;
  EXPECT_EQ(ASCIIToUTF16(target_url), omnibox_view->GetText());
  EXPECT_FALSE(omnibox_view->model()->popup_model()->IsOpen());
}

#if !defined(TOOLKIT_GTK)
IN_PROC_BROWSER_TEST_F(OmniboxViewTest, BeginningShownAfterBlur) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));

  omnibox_view->OnBeforePossibleChange();
  omnibox_view->SetWindowTextAndCaretPos(ASCIIToUTF16("data:text/plain,test"),
      5U, false, false);
  omnibox_view->OnAfterPossibleChange();
  ASSERT_TRUE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));
  size_t start, end;
  omnibox_view->GetSelectionBounds(&start, &end);
  ASSERT_EQ(5U, start);
  ASSERT_EQ(5U, end);

  ui_test_utils::ClickOnView(browser(), VIEW_ID_TAB_CONTAINER);
  ASSERT_FALSE(ui_test_utils::IsViewFocused(browser(), VIEW_ID_OMNIBOX));

  omnibox_view->GetSelectionBounds(&start, &end);
  ASSERT_EQ(0U, start);
  ASSERT_EQ(0U, end);
}
#endif  // !defined(TOOLKIT_GTK)

IN_PROC_BROWSER_TEST_F(OmniboxViewTest, CtrlArrowAfterArrowSuggestions) {
  OmniboxView* omnibox_view = NULL;
  ASSERT_NO_FATAL_FAILURE(GetOmniboxView(&omnibox_view));
  OmniboxPopupModel* popup_model = omnibox_view->model()->popup_model();
  ASSERT_TRUE(popup_model);

  // Input something to trigger results.
  ASSERT_NO_FATAL_FAILURE(SendKeySequence(kDesiredTLDKeys));
  ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
  ASSERT_TRUE(popup_model->IsOpen());

  ASSERT_EQ(ASCIIToUTF16("bar.com/1"), omnibox_view->GetText());

  // Arrow down on a suggestion, and omnibox text should be the suggestion.
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_DOWN, 0));
  ASSERT_NO_FATAL_FAILURE(WaitForAutocompleteControllerDone());
  ASSERT_EQ(ASCIIToUTF16("www.bar.com/2"), omnibox_view->GetText());

  // Highlight the last 2 words and the omnibox text should not change.
  // Simulating Ctrl-shift-left only once does not seem to highlight anything
  // on Linux.
#if defined(OS_MACOSX)
  // Mac uses alt-left/right to select a word.
  const int modifiers = ui::EF_SHIFT_DOWN | ui::EF_ALT_DOWN;
#else
  const int modifiers = ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN;
#endif
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, modifiers));
  ASSERT_NO_FATAL_FAILURE(SendKey(ui::VKEY_LEFT, modifiers));
  ASSERT_EQ(ASCIIToUTF16("www.bar.com/2"), omnibox_view->GetText());
}
