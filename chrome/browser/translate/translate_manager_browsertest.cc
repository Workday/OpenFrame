// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <set>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/prefs/pref_change_registrar.h"
#include "base/prefs/pref_service.h"
#include "base/strings/stringprintf.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/infobars/infobar.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/tab_contents/render_view_context_menu.h"
#include "chrome/browser/translate/translate_infobar_delegate.h"
#include "chrome/browser/translate/translate_language_list.h"
#include "chrome/browser/translate/translate_manager.h"
#include "chrome/browser/translate/translate_prefs.h"
#include "chrome/browser/translate/translate_script.h"
#include "chrome/browser/translate/translate_tab_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/translate/language_detection_details.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_renderer_host.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/WebKit/public/web/WebContextMenuData.h"

using content::RenderViewHostTester;

// An observer that keeps track of whether a navigation entry was committed.
class NavEntryCommittedObserver : public content::NotificationObserver {
 public:
  explicit NavEntryCommittedObserver(content::WebContents* web_contents) {
    registrar_.Add(this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
                   content::Source<content::NavigationController>(
                       &web_contents->GetController()));
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    DCHECK(type == content::NOTIFICATION_NAV_ENTRY_COMMITTED);
    details_ =
        *(content::Details<content::LoadCommittedDetails>(details).ptr());
  }

  const content::LoadCommittedDetails& load_committed_details() const {
    return details_;
  }

 private:
  content::LoadCommittedDetails details_;
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(NavEntryCommittedObserver);
};

class TranslateManagerBrowserTest : public ChromeRenderViewHostTestHarness,
                                    public content::NotificationObserver {
 public:
  TranslateManagerBrowserTest()
      : pref_callback_(
            base::Bind(&TranslateManagerBrowserTest::OnPreferenceChanged,
                       base::Unretained(this))) {
  }

  // Simulates navigating to a page and getting the page contents and language
  // for that navigation.
  void SimulateNavigation(const GURL& url,
                          const std::string& lang,
                          bool page_translatable) {
    NavigateAndCommit(url);
    SimulateOnTranslateLanguageDetermined(lang, page_translatable);
  }

  void SimulateOnTranslateLanguageDetermined(const std::string& lang,
                                             bool page_translatable) {
    LanguageDetectionDetails details;
    details.adopted_language = lang;
    RenderViewHostTester::TestOnMessageReceived(
        rvh(),
        ChromeViewHostMsg_TranslateLanguageDetermined(
            0, details, page_translatable));
  }

  void SimulateOnPageTranslated(int routing_id,
                                const std::string& source_lang,
                                const std::string& target_lang,
                                TranslateErrors::Type error) {
    RenderViewHostTester::TestOnMessageReceived(
        rvh(),
        ChromeViewHostMsg_PageTranslated(
            routing_id, 0, source_lang, target_lang, error));
  }

  void SimulateOnPageTranslated(const std::string& source_lang,
                                const std::string& target_lang) {
    SimulateOnPageTranslated(0, source_lang, target_lang,
                             TranslateErrors::NONE);
  }

  bool GetTranslateMessage(int* page_id,
                           std::string* original_lang,
                           std::string* target_lang) {
    const IPC::Message* message =
        process()->sink().GetFirstMessageMatching(
            ChromeViewMsg_TranslatePage::ID);
    if (!message)
      return false;
    Tuple4<int, std::string, std::string, std::string> translate_param;
    ChromeViewMsg_TranslatePage::Read(message, &translate_param);
    if (page_id)
      *page_id = translate_param.a;
    // Ignore translate_param.b which is the script injected in the page.
    if (original_lang)
      *original_lang = translate_param.c;
    if (target_lang)
      *target_lang = translate_param.d;
    return true;
  }

  InfoBarService* infobar_service() {
    return InfoBarService::FromWebContents(web_contents());
  }

  // Returns the translate infobar if there is 1 infobar and it is a translate
  // infobar.
  TranslateInfoBarDelegate* GetTranslateInfoBar() {
    return (infobar_service()->infobar_count() == 1) ?
        infobar_service()->infobar_at(0)->AsTranslateInfoBarDelegate() : NULL;
  }

  // If there is 1 infobar and it is a translate infobar, closes it and returns
  // true.  Returns false otherwise.
  bool CloseTranslateInfoBar() {
    InfoBarDelegate* infobar = GetTranslateInfoBar();
    if (!infobar)
      return false;
    infobar->InfoBarDismissed();  // Simulates closing the infobar.
    infobar_service()->RemoveInfoBar(infobar);
    return true;
  }

  // Checks whether |infobar| has been removed and clears the removed infobar
  // list.
  bool CheckInfoBarRemovedAndReset(InfoBarDelegate* delegate) {
    bool found = removed_infobars_.count(delegate) != 0;
    removed_infobars_.clear();
    return found;
  }

  void ExpireTranslateScriptImmediately() {
    TranslateManager::GetInstance()->SetTranslateScriptExpirationDelay(0);
  }

  // If there is 1 infobar and it is a translate infobar, deny translation and
  // returns true.  Returns false otherwise.
  bool DenyTranslation() {
    TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
    if (!infobar)
      return false;
    infobar->TranslationDeclined();
    infobar_service()->RemoveInfoBar(infobar);
    return true;
  }

  void ReloadAndWait(bool successful_reload) {
    NavEntryCommittedObserver nav_observer(web_contents());
    if (successful_reload)
      Reload();
    else
      FailedReload();

    // Ensures it is really handled a reload.
    const content::LoadCommittedDetails& nav_details =
        nav_observer.load_committed_details();
    EXPECT_TRUE(nav_details.entry != NULL);  // There was a navigation.
    EXPECT_EQ(content::NAVIGATION_TYPE_EXISTING_PAGE, nav_details.type);

    // The TranslateManager class processes the navigation entry committed
    // notification in a posted task; process that task.
    base::MessageLoop::current()->RunUntilIdle();
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) {
    DCHECK_EQ(chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_REMOVED, type);
    removed_infobars_.insert(
        content::Details<InfoBarRemovedDetails>(details)->first);
  }

  MOCK_METHOD1(OnPreferenceChanged, void(const std::string&));

 protected:
  virtual void SetUp() {
    // Access the TranslateManager singleton so it is created before we call
    // ChromeRenderViewHostTestHarness::SetUp() to match what's done in Chrome,
    // where the TranslateManager is created before the WebContents.  This
    // matters as they both register for similar events and we want the
    // notifications to happen in the same sequence (TranslateManager first,
    // WebContents second).  Also clears the translate script so it is fetched
    // everytime and sets the expiration delay to a large value by default (in
    // case it was zeroed in a previous test).
    TranslateManager::GetInstance()->ClearTranslateScript();
    TranslateManager::GetInstance()->
        SetTranslateScriptExpirationDelay(60 * 60 * 1000);
    TranslateManager::GetInstance()->set_translate_max_reload_attemps(0);

    ChromeRenderViewHostTestHarness::SetUp();
    InfoBarService::CreateForWebContents(web_contents());
    TranslateTabHelper::CreateForWebContents(web_contents());

    notification_registrar_.Add(this,
        chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_REMOVED,
        content::Source<InfoBarService>(infobar_service()));
  }

  virtual void TearDown() {
    process()->sink().ClearMessages();

    notification_registrar_.Remove(this,
        chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_REMOVED,
        content::Source<InfoBarService>(infobar_service()));

    ChromeRenderViewHostTestHarness::TearDown();
  }

  void SimulateTranslateScriptURLFetch(bool success) {
    net::TestURLFetcher* fetcher =
        url_fetcher_factory_.GetFetcherByID(TranslateScript::kFetcherId);
    ASSERT_TRUE(fetcher);
    net::URLRequestStatus status;
    status.set_status(success ? net::URLRequestStatus::SUCCESS :
                                net::URLRequestStatus::FAILED);
    fetcher->set_url(fetcher->GetOriginalURL());
    fetcher->set_status(status);
    fetcher->set_response_code(success ? 200 : 500);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
  }

  void SimulateSupportedLanguagesURLFetch(
      bool success,
      const std::vector<std::string>& languages,
      bool use_alpha_languages,
      const std::vector<std::string>& alpha_languages) {
    net::URLRequestStatus status;
    status.set_status(success ? net::URLRequestStatus::SUCCESS :
                                net::URLRequestStatus::FAILED);

    std::string data;
    if (success) {
      data = base::StringPrintf(
          "%s{\"sl\": {\"bla\": \"bla\"}, \"%s\": {",
          TranslateLanguageList::kLanguageListCallbackName,
          TranslateLanguageList::kTargetLanguagesKey);
      const char* comma = "";
      for (size_t i = 0; i < languages.size(); ++i) {
        data += base::StringPrintf(
            "%s\"%s\": \"UnusedFullName\"", comma, languages[i].c_str());
        if (i == 0)
          comma = ",";
      }

      if (use_alpha_languages) {
        data += base::StringPrintf("},\"%s\": {",
                                   TranslateLanguageList::kAlphaLanguagesKey);
        comma = "";
        for (size_t i = 0; i < alpha_languages.size(); ++i) {
          data += base::StringPrintf("%s\"%s\": 1", comma,
                                     alpha_languages[i].c_str());
          if (i == 0)
            comma = ",";
        }
      }

      data += "}})";
    }
    net::TestURLFetcher* fetcher =
        url_fetcher_factory_.GetFetcherByID(TranslateLanguageList::kFetcherId);
    ASSERT_TRUE(fetcher != NULL);
    fetcher->set_url(fetcher->GetOriginalURL());
    fetcher->set_status(status);
    fetcher->set_response_code(success ? 200 : 500);
    fetcher->SetResponseString(data);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
  }

  void SetPrefObserverExpectation(const char* path) {
    EXPECT_CALL(*this, OnPreferenceChanged(std::string(path)));
  }

  PrefChangeRegistrar::NamedChangeCallback pref_callback_;

 private:
  content::NotificationRegistrar notification_registrar_;
  net::TestURLFetcherFactory url_fetcher_factory_;

  // The infobars that have been removed.
  // WARNING: the pointers point to deleted objects, use only for comparison.
  std::set<InfoBarDelegate*> removed_infobars_;

  DISALLOW_COPY_AND_ASSIGN(TranslateManagerBrowserTest);
};

namespace {

class TestRenderViewContextMenu : public RenderViewContextMenu {
 public:
  static TestRenderViewContextMenu* CreateContextMenu(
      content::WebContents* web_contents) {
    content::ContextMenuParams params;
    params.media_type = WebKit::WebContextMenuData::MediaTypeNone;
    params.x = 0;
    params.y = 0;
    params.is_image_blocked = false;
    params.media_flags = 0;
    params.spellcheck_enabled = false;
    params.is_editable = false;
    params.page_url = web_contents->GetController().GetActiveEntry()->GetURL();
#if defined(OS_MACOSX)
    params.writing_direction_default = 0;
    params.writing_direction_left_to_right = 0;
    params.writing_direction_right_to_left = 0;
#endif  // OS_MACOSX
    params.edit_flags = WebKit::WebContextMenuData::CanTranslate;
    return new TestRenderViewContextMenu(web_contents, params);
  }

  bool IsItemPresent(int id) {
    return menu_model_.GetIndexOfCommandId(id) != -1;
  }

  virtual void PlatformInit() OVERRIDE { }
  virtual void PlatformCancel() OVERRIDE { }
  virtual bool GetAcceleratorForCommandId(
      int command_id,
      ui::Accelerator* accelerator) OVERRIDE { return false; }

 private:
  TestRenderViewContextMenu(content::WebContents* web_contents,
                            const content::ContextMenuParams& params)
      : RenderViewContextMenu(web_contents, params) {
  }

  DISALLOW_COPY_AND_ASSIGN(TestRenderViewContextMenu);
};

}  // namespace

TEST_F(TranslateManagerBrowserTest, NormalTranslate) {
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  // We should have an infobar.
  TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::BEFORE_TRANSLATE,
            infobar->infobar_type());

  // Simulate clicking translate.
  process()->sink().ClearMessages();
  infobar->Translate();

  // The "Translating..." infobar should be showing.
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::TRANSLATING, infobar->infobar_type());

  // Simulate the translate script being retrieved (it only needs to be done
  // once in the test as it is cached).
  SimulateTranslateScriptURLFetch(true);

  // Test that we sent the right message to the renderer.
  int page_id = 0;
  std::string original_lang, target_lang;
  EXPECT_TRUE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  EXPECT_EQ("fr", original_lang);
  EXPECT_EQ("en", target_lang);

  // Simulate the render notifying the translation has been done.
  SimulateOnPageTranslated("fr", "en");

  // The after translate infobar should be showing.
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::AFTER_TRANSLATE, infobar->infobar_type());

  // Simulate changing the original language and translating.
  process()->sink().ClearMessages();
  std::string new_original_lang = infobar->language_code_at(0);
  infobar->set_original_language_index(0);
  infobar->Translate();
  EXPECT_TRUE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  EXPECT_EQ(new_original_lang, original_lang);
  EXPECT_EQ("en", target_lang);
  // Simulate the render notifying the translation has been done.
  SimulateOnPageTranslated(new_original_lang, "en");
  // infobar is now invalid.
  TranslateInfoBarDelegate* new_infobar = GetTranslateInfoBar();
  ASSERT_TRUE(new_infobar != NULL);
  infobar = new_infobar;

  // Simulate changing the target language and translating.
  process()->sink().ClearMessages();
  std::string new_target_lang = infobar->language_code_at(1);
  infobar->set_target_language_index(1);
  infobar->Translate();
  EXPECT_TRUE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  EXPECT_EQ(new_original_lang, original_lang);
  EXPECT_EQ(new_target_lang, target_lang);
  // Simulate the render notifying the translation has been done.
  SimulateOnPageTranslated(new_original_lang, new_target_lang);
  // infobar is now invalid.
  new_infobar = GetTranslateInfoBar();
  ASSERT_TRUE(new_infobar != NULL);

  // Verify reload keeps the same settings.
  ReloadAndWait(true);
  new_infobar = GetTranslateInfoBar();
  ASSERT_TRUE(new_infobar != NULL);
  ASSERT_EQ(new_target_lang, infobar->target_language_code());
}

TEST_F(TranslateManagerBrowserTest, TranslateScriptNotAvailable) {
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  // We should have an infobar.
  TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::BEFORE_TRANSLATE,
            infobar->infobar_type());

  // Simulate clicking translate.
  process()->sink().ClearMessages();
  infobar->Translate();
  SimulateTranslateScriptURLFetch(false);

  // We should not have sent any message to translate to the renderer.
  EXPECT_FALSE(GetTranslateMessage(NULL, NULL, NULL));

  // And we should have an error infobar showing.
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::TRANSLATION_ERROR,
            infobar->infobar_type());
}

// Ensures we deal correctly with pages for which the browser does not recognize
// the language (the translate server may or not detect the language).
TEST_F(TranslateManagerBrowserTest, TranslateUnknownLanguage) {
  // Simulate navigating to a page ("und" is the string returned by the CLD for
  // languages it does not recognize).
  SimulateNavigation(GURL("http://www.google.mys"), "und", true);

  // We should not have an infobar as we don't know the language.
  ASSERT_TRUE(GetTranslateInfoBar() == NULL);

  // Translate the page anyway throught the context menu.
  scoped_ptr<TestRenderViewContextMenu> menu(
      TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  menu->ExecuteCommand(IDC_CONTENT_CONTEXT_TRANSLATE, 0);

  // To test that bug #49018 if fixed, make sure we deal correctly with errors.
  // Simulate a failure to fetch the translate script.
  SimulateTranslateScriptURLFetch(false);
  TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::TRANSLATION_ERROR,
            infobar->infobar_type());
  EXPECT_TRUE(infobar->is_error());
  infobar->MessageInfoBarButtonPressed();
  SimulateTranslateScriptURLFetch(true);  // This time succeed.

  // Simulate the render notifying the translation has been done, the server
  // having detected the page was in a known and supported language.
  SimulateOnPageTranslated("fr", "en");

  // The after translate infobar should be showing.
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::AFTER_TRANSLATE, infobar->infobar_type());
  EXPECT_EQ("fr", infobar->original_language_code());
  EXPECT_EQ("en", infobar->target_language_code());

  // Let's run the same steps but this time the server detects the page is
  // already in English.
  SimulateNavigation(GURL("http://www.google.com"), "und", true);
  menu.reset(TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  menu->ExecuteCommand(IDC_CONTENT_CONTEXT_TRANSLATE, 0);
  SimulateOnPageTranslated(1, "en", "en", TranslateErrors::IDENTICAL_LANGUAGES);
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::TRANSLATION_ERROR,
            infobar->infobar_type());
  EXPECT_EQ(TranslateErrors::IDENTICAL_LANGUAGES, infobar->error_type());

  // Let's run the same steps again but this time the server fails to detect the
  // page's language (it returns an empty string).
  SimulateNavigation(GURL("http://www.google.com"), "und", true);
  menu.reset(TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  menu->ExecuteCommand(IDC_CONTENT_CONTEXT_TRANSLATE, 0);
  SimulateOnPageTranslated(2, std::string(), "en",
                           TranslateErrors::UNKNOWN_LANGUAGE);
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::TRANSLATION_ERROR,
            infobar->infobar_type());
  EXPECT_EQ(TranslateErrors::UNKNOWN_LANGUAGE, infobar->error_type());
}

// Tests that we show/don't show an info-bar for the languages.
TEST_F(TranslateManagerBrowserTest, TestLanguages) {
  std::vector<std::string> languages;
  languages.push_back("en");
  languages.push_back("ja");
  languages.push_back("fr");
  languages.push_back("ht");
  languages.push_back("xx");
  languages.push_back("zh");
  languages.push_back("zh-CN");
  languages.push_back("und");

  GURL url("http://www.google.com");
  for (size_t i = 0; i < languages.size(); ++i) {
    std::string lang = languages[i];
    SCOPED_TRACE(::testing::Message() << "Iteration " << i <<
                 " language=" << lang);

    // We should not have a translate infobar.
    TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
    ASSERT_TRUE(infobar == NULL);

    SimulateNavigation(url, lang, true);

    // Verify we have/don't have an info-bar as expected.
    infobar = GetTranslateInfoBar();
    bool expected = TranslateManager::IsSupportedLanguage(lang) &&
        lang != "en";
    EXPECT_EQ(expected, infobar != NULL);

    if (infobar != NULL)
      EXPECT_TRUE(CloseTranslateInfoBar());
  }
}

// Test the fetching of languages from the translate server
TEST_F(TranslateManagerBrowserTest, FetchLanguagesFromTranslateServer) {
  std::vector<std::string> server_languages;
  // A list of languages to fake being returned by the translate server.
  server_languages.push_back("aa");
  server_languages.push_back("ak");
  server_languages.push_back("ab");
  server_languages.push_back("en-CA");
  server_languages.push_back("zh");
  server_languages.push_back("yi");
  server_languages.push_back("fr-FR");
  server_languages.push_back("xx");

  std::vector<std::string> alpha_languages;
  alpha_languages.push_back("aa");
  alpha_languages.push_back("yi");

  // First, get the default languages list. Note that calling
  // GetSupportedLanguages() invokes RequestLanguageList() internally.
  std::vector<std::string> default_supported_languages;
  TranslateManager::GetSupportedLanguages(&default_supported_languages);
  // To make sure we got the defaults and don't confuse them with the mocks.
  ASSERT_NE(default_supported_languages.size(), server_languages.size());

  // Check that we still get the defaults until the URLFetch has completed.
  std::vector<std::string> current_supported_languages;
  TranslateManager::GetSupportedLanguages(&current_supported_languages);
  EXPECT_EQ(default_supported_languages, current_supported_languages);

  // Also check that it didn't change if we failed the URL fetch.
  SimulateSupportedLanguagesURLFetch(false, std::vector<std::string>(),
                                     true, std::vector<std::string>());
  current_supported_languages.clear();
  TranslateManager::GetSupportedLanguages(&current_supported_languages);
  EXPECT_EQ(default_supported_languages, current_supported_languages);

  // Now check that we got the appropriate set of languages from the server.
  SimulateSupportedLanguagesURLFetch(true, server_languages,
                                     true, alpha_languages);
  current_supported_languages.clear();
  TranslateManager::GetSupportedLanguages(&current_supported_languages);
  // "xx" can't be displayed in the Translate inforbar, so this is eliminated.
  EXPECT_EQ(server_languages.size() - 1, current_supported_languages.size());
  // Not sure we need to guarantee the order of languages, so we find them.
  for (size_t i = 0; i < server_languages.size(); ++i) {
    const std::string& lang = server_languages[i];
    if (lang == "xx")
      continue;
    EXPECT_NE(current_supported_languages.end(),
              std::find(current_supported_languages.begin(),
                        current_supported_languages.end(),
                        lang));
    bool is_alpha = std::find(alpha_languages.begin(),
                              alpha_languages.end(),
                              lang) != alpha_languages.end();
    EXPECT_EQ(TranslateManager::IsAlphaLanguage(lang), is_alpha);
  }
}

// Test the fetching of languages from the translate server without 'al'
// parameter.
TEST_F(TranslateManagerBrowserTest,
       FetchLanguagesFromTranslateServerWithoutAlpha) {
  std::vector<std::string> server_languages;
  server_languages.push_back("aa");
  server_languages.push_back("ak");
  server_languages.push_back("ab");
  server_languages.push_back("en-CA");
  server_languages.push_back("zh");
  server_languages.push_back("yi");
  server_languages.push_back("fr-FR");
  server_languages.push_back("xx");

  std::vector<std::string> alpha_languages;
  alpha_languages.push_back("aa");
  alpha_languages.push_back("yi");

  // call GetSupportedLanguages to call RequestLanguageList internally.
  std::vector<std::string> default_supported_languages;
  TranslateManager::GetSupportedLanguages(&default_supported_languages);

  SimulateSupportedLanguagesURLFetch(true, server_languages,
                                     false, alpha_languages);

  std::vector<std::string> current_supported_languages;
  TranslateManager::GetSupportedLanguages(&current_supported_languages);

  // "xx" can't be displayed in the Translate inforbar, so this is eliminated.
  EXPECT_EQ(server_languages.size() - 1, current_supported_languages.size());

  for (size_t i = 0; i < server_languages.size(); ++i) {
    const std::string& lang = server_languages[i];
    if (lang == "xx")
      continue;
    EXPECT_NE(current_supported_languages.end(),
              std::find(current_supported_languages.begin(),
                        current_supported_languages.end(),
                        lang));
    EXPECT_FALSE(TranslateManager::IsAlphaLanguage(lang));
  }
}

// Tests auto-translate on page.
TEST_F(TranslateManagerBrowserTest, AutoTranslateOnNavigate) {
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  // Simulate the user translating.
  TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  infobar->Translate();
  // Simulate the translate script being retrieved.
  SimulateTranslateScriptURLFetch(true);
  SimulateOnPageTranslated("fr", "en");

  // Now navigate to a new page in the same language.
  process()->sink().ClearMessages();
  SimulateNavigation(GURL("http://news.google.fr"), "fr", true);

  // This should have automatically triggered a translation.
  int page_id = 0;
  std::string original_lang, target_lang;
  EXPECT_TRUE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  EXPECT_EQ(1, page_id);
  EXPECT_EQ("fr", original_lang);
  EXPECT_EQ("en", target_lang);

  // Now navigate to a page in a different language.
  process()->sink().ClearMessages();
  SimulateNavigation(GURL("http://news.google.es"), "es", true);

  // This should not have triggered a translate.
  EXPECT_FALSE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
}

// Tests that multiple OnPageContents do not cause multiple infobars.
TEST_F(TranslateManagerBrowserTest, MultipleOnPageContents) {
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  // Simulate clicking 'Nope' (don't translate).
  EXPECT_TRUE(DenyTranslation());
  EXPECT_EQ(0U, infobar_service()->infobar_count());

  // Send a new PageContents, we should not show an infobar.
  SimulateOnTranslateLanguageDetermined("fr", true);
  EXPECT_EQ(0U, infobar_service()->infobar_count());

  // Do the same steps but simulate closing the infobar this time.
  SimulateNavigation(GURL("http://www.youtube.fr"), "fr", true);
  EXPECT_TRUE(CloseTranslateInfoBar());
  EXPECT_EQ(0U, infobar_service()->infobar_count());
  SimulateOnTranslateLanguageDetermined("fr", true);
  EXPECT_EQ(0U, infobar_service()->infobar_count());
}

// Test that reloading the page brings back the infobar if the
// reload succeeded and does not bring it back the reload fails.
TEST_F(TranslateManagerBrowserTest, Reload) {
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  EXPECT_TRUE(CloseTranslateInfoBar());

  // Reload should bring back the infobar if the page succeds
  ReloadAndWait(true);
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);

  EXPECT_TRUE(CloseTranslateInfoBar());

  // And not show it if the reload fails
  ReloadAndWait(false);
  EXPECT_EQ(NULL, GetTranslateInfoBar());

  // Set reload attempts to a high value, we will not see the infobar
  // immediatly.
  TranslateManager::GetInstance()->set_translate_max_reload_attemps(100);
  ReloadAndWait(true);
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);
}

// Test that reloading the page by way of typing again the URL in the
// location bar brings back the infobar.
TEST_F(TranslateManagerBrowserTest, ReloadFromLocationBar) {
  GURL url("http://www.google.fr");
  SimulateNavigation(url, "fr", true);

  EXPECT_TRUE(CloseTranslateInfoBar());

  // Create a pending navigation and simulate a page load.  That should be the
  // equivalent of typing the URL again in the location bar.
  NavEntryCommittedObserver nav_observer(web_contents());
  web_contents()->GetController().LoadURL(url, content::Referrer(),
                                          content::PAGE_TRANSITION_TYPED,
                                          std::string());
  rvh_tester()->SendNavigate(0, url);

  // Test that we are really getting a same page navigation, the test would be
  // useless if it was not the case.
  const content::LoadCommittedDetails& nav_details =
      nav_observer.load_committed_details();
  EXPECT_TRUE(nav_details.entry != NULL);  // There was a navigation.
  EXPECT_EQ(content::NAVIGATION_TYPE_SAME_PAGE, nav_details.type);

  // The TranslateManager class processes the navigation entry committed
  // notification in a posted task; process that task.
  base::MessageLoop::current()->RunUntilIdle();
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);
}

// Tests that a closed translate infobar does not reappear when navigating
// in-page.
TEST_F(TranslateManagerBrowserTest, CloseInfoBarInPageNavigation) {
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  EXPECT_TRUE(CloseTranslateInfoBar());

  // Navigate in page, no infobar should be shown.
  SimulateNavigation(GURL("http://www.google.fr/#ref1"), "fr", true);
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);

  // Navigate out of page, a new infobar should show.
  SimulateNavigation(GURL("http://www.google.fr/foot"), "fr", true);
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);
}

// Tests that a closed translate infobar does not reappear when navigating
// in a subframe. (http://crbug.com/48215)
TEST_F(TranslateManagerBrowserTest, CloseInfoBarInSubframeNavigation) {
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  EXPECT_TRUE(CloseTranslateInfoBar());

  // Simulate a sub-frame auto-navigating.
  rvh_tester()->SendNavigateWithTransition(
      1, GURL("http://pub.com"), content::PAGE_TRANSITION_AUTO_SUBFRAME);
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);

  // Simulate the user navigating in a sub-frame.
  rvh_tester()->SendNavigateWithTransition(
      2, GURL("http://pub.com"), content::PAGE_TRANSITION_MANUAL_SUBFRAME);
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);

  // Navigate out of page, a new infobar should show.
  SimulateNavigation(GURL("http://www.google.fr/foot"), "fr", true);
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);
}

// Tests that denying translation is sticky when navigating in page.
TEST_F(TranslateManagerBrowserTest, DenyTranslateInPageNavigation) {
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  // Simulate clicking 'Nope' (don't translate).
  EXPECT_TRUE(DenyTranslation());

  // Navigate in page, no infobar should be shown.
  SimulateNavigation(GURL("http://www.google.fr/#ref1"), "fr", true);
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);

  // Navigate out of page, a new infobar should show.
  SimulateNavigation(GURL("http://www.google.fr/foot"), "fr", true);
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);
}

// Tests that after translating and closing the infobar, the infobar does not
// return when navigating in page.
TEST_F(TranslateManagerBrowserTest, TranslateCloseInfoBarInPageNavigation) {
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  // Simulate the user translating.
  TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  infobar->Translate();
  // Simulate the translate script being retrieved.
  SimulateTranslateScriptURLFetch(true);
  SimulateOnPageTranslated("fr", "en");

  EXPECT_TRUE(CloseTranslateInfoBar());

  // Navigate in page, no infobar should be shown.
  SimulateNavigation(GURL("http://www.google.fr/#ref1"), "fr", true);
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);

  // Navigate out of page, a new infobar should show.
  // Note that we navigate to a page in a different language so we don't trigger
  // the auto-translate feature (it would translate the page automatically and
  // the before translate inforbar would not be shown).
  SimulateNavigation(GURL("http://www.google.de"), "de", true);
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);
}

// Tests that the after translate the infobar still shows when navigating
// in-page.
TEST_F(TranslateManagerBrowserTest, TranslateInPageNavigation) {
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  // Simulate the user translating.
  TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  infobar->Translate();
  SimulateTranslateScriptURLFetch(true);
  SimulateOnPageTranslated("fr", "en");
  // The after translate infobar is showing.
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);

  // Navigate out of page, a new infobar should show.
  // See note in TranslateCloseInfoBarInPageNavigation test on why it is
  // important to navigate to a page in a different language for this test.
  SimulateNavigation(GURL("http://www.google.de"), "de", true);
  // The old infobar is gone.
  EXPECT_TRUE(CheckInfoBarRemovedAndReset(infobar));
  // And there is a new one.
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);
}

// Tests that no translate infobar is shown when navigating to a page in an
// unsupported language.
TEST_F(TranslateManagerBrowserTest, CLDReportsUnsupportedPageLanguage) {
  // Simulate navigating to a page and getting an unsupported language.
  SimulateNavigation(GURL("http://www.google.com"), "qbz", true);

  // No info-bar should be shown.
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);
}

// Tests that we deal correctly with unsupported languages returned by the
// server.
// The translation server might return a language we don't support.
TEST_F(TranslateManagerBrowserTest, ServerReportsUnsupportedLanguage) {
  SimulateNavigation(GURL("http://mail.google.fr"), "fr", true);
  TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  process()->sink().ClearMessages();
  infobar->Translate();
  SimulateTranslateScriptURLFetch(true);
  // Simulate the render notifying the translation has been done, but it
  // reports a language we don't support.
  SimulateOnPageTranslated("qbz", "en");

  // An error infobar should be showing to report that we don't support this
  // language.
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::TRANSLATION_ERROR,
            infobar->infobar_type());

  // This infobar should have a button (so the string should not be empty).
  ASSERT_FALSE(infobar->GetMessageInfoBarButtonText().empty());

  // Pressing the button on that infobar should revert to the original language.
  process()->sink().ClearMessages();
  infobar->MessageInfoBarButtonPressed();
  const IPC::Message* message =
      process()->sink().GetFirstMessageMatching(
          ChromeViewMsg_RevertTranslation::ID);
  EXPECT_TRUE(message != NULL);
  // And it should have removed the infobar.
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);
}

// Tests that no translate infobar is shown and context menu is disabled, when
// Chrome is in a language that the translate server does not support.
TEST_F(TranslateManagerBrowserTest, UnsupportedUILanguage) {
  std::string original_lang = g_browser_process->GetApplicationLocale();
  g_browser_process->SetApplicationLocale("qbz");

  // Make sure that the accept language list only contains unsupported languages
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  PrefService* prefs = profile->GetPrefs();
  prefs->SetString(prefs::kAcceptLanguages, "qbz");

  // Simulate navigating to a page in a language supported by the translate
  // server.
  SimulateNavigation(GURL("http://www.google.com"), "en", true);

  // No info-bar should be shown.
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);

  // And the context menu option should be disabled too.
  scoped_ptr<TestRenderViewContextMenu> menu(
      TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  EXPECT_TRUE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_TRANSLATE));
  EXPECT_FALSE(menu->IsCommandIdEnabled(IDC_CONTENT_CONTEXT_TRANSLATE));

  g_browser_process->SetApplicationLocale(original_lang);
}

// Tests that the first supported accept language is selected
TEST_F(TranslateManagerBrowserTest, TranslateAcceptLanguage) {
  // Set locate to non-existant language
  std::string original_lang = g_browser_process->GetApplicationLocale();
  g_browser_process->SetApplicationLocale("qbz");

  // Set Qbz and French as the only accepted languages
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  PrefService* prefs = profile->GetPrefs();
  prefs->SetString(prefs::kAcceptLanguages, "qbz,fr");

  // Go to a German page
  SimulateNavigation(GURL("http://google.de"), "de", true);

  // Expect the infobar to pop up
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);

  // Set Qbz and English-US as the only accepted languages to test the country
  // code removal code which was causing a crash as filed in Issue 90106,
  // a crash caused by a language with a country code that wasn't recognized.
  prefs->SetString(prefs::kAcceptLanguages, "qbz,en-us");

  // Go to a German page
  SimulateNavigation(GURL("http://google.de"), "de", true);

  // Expect the infobar to pop up
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);
}

// Tests that the translate enabled preference is honored.
TEST_F(TranslateManagerBrowserTest, TranslateEnabledPref) {
  // Make sure the pref allows translate.
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  PrefService* prefs = profile->GetPrefs();
  prefs->SetBoolean(prefs::kEnableTranslate, true);

  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  // An infobar should be shown.
  TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
  EXPECT_TRUE(infobar != NULL);

  // Disable translate.
  prefs->SetBoolean(prefs::kEnableTranslate, false);

  // Navigate to a new page, that should close the previous infobar.
  GURL url("http://www.youtube.fr");
  NavigateAndCommit(url);
  infobar = GetTranslateInfoBar();
  EXPECT_TRUE(infobar == NULL);

  // Simulate getting the page contents and language, that should not trigger
  // a translate infobar.
  SimulateOnTranslateLanguageDetermined("fr", true);
  infobar = GetTranslateInfoBar();
  EXPECT_TRUE(infobar == NULL);
}

// Tests the "Never translate <language>" pref.
TEST_F(TranslateManagerBrowserTest, NeverTranslateLanguagePref) {
  GURL url("http://www.google.fr");
  SimulateNavigation(url, "fr", true);

  // An infobar should be shown.
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);

  // Select never translate this language.
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  PrefService* prefs = profile->GetPrefs();
  PrefChangeRegistrar registrar;
  registrar.Init(prefs);
  registrar.Add(TranslatePrefs::kPrefTranslateLanguageBlacklist,
                pref_callback_);
  TranslatePrefs translate_prefs(prefs);
  EXPECT_FALSE(translate_prefs.IsBlockedLanguage("fr"));
  EXPECT_TRUE(translate_prefs.CanTranslateLanguage(profile, "fr"));
  SetPrefObserverExpectation(TranslatePrefs::kPrefTranslateLanguageBlacklist);
  translate_prefs.BlockLanguage("fr");
  EXPECT_TRUE(translate_prefs.IsBlockedLanguage("fr"));
  EXPECT_FALSE(translate_prefs.IsSiteBlacklisted(url.host()));
  EXPECT_FALSE(translate_prefs.CanTranslateLanguage(profile, "fr"));

  EXPECT_TRUE(CloseTranslateInfoBar());

  // Navigate to a new page also in French.
  SimulateNavigation(GURL("http://wwww.youtube.fr"), "fr", true);

  // There should not be a translate infobar.
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);

  // Remove the language from the blacklist.
  SetPrefObserverExpectation(TranslatePrefs::kPrefTranslateLanguageBlacklist);
  translate_prefs.UnblockLanguage("fr");
  EXPECT_FALSE(translate_prefs.IsBlockedLanguage("fr"));
  EXPECT_FALSE(translate_prefs.IsSiteBlacklisted(url.host()));
  EXPECT_TRUE(translate_prefs.CanTranslateLanguage(profile, "fr"));

  // Navigate to a page in French.
  SimulateNavigation(url, "fr", true);

  // There should be a translate infobar.
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);
}

// Tests the "Never translate this site" pref.
TEST_F(TranslateManagerBrowserTest, NeverTranslateSitePref) {
  GURL url("http://www.google.fr");
  std::string host(url.host());
  SimulateNavigation(url, "fr", true);

  // An infobar should be shown.
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);

  // Select never translate this site.
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  PrefService* prefs = profile->GetPrefs();
  PrefChangeRegistrar registrar;
  registrar.Init(prefs);
  registrar.Add(TranslatePrefs::kPrefTranslateSiteBlacklist, pref_callback_);
  TranslatePrefs translate_prefs(prefs);
  EXPECT_FALSE(translate_prefs.IsSiteBlacklisted(host));
  EXPECT_TRUE(translate_prefs.CanTranslateLanguage(profile, "fr"));
  SetPrefObserverExpectation(TranslatePrefs::kPrefTranslateSiteBlacklist);
  translate_prefs.BlacklistSite(host);
  EXPECT_TRUE(translate_prefs.IsSiteBlacklisted(host));
  EXPECT_TRUE(translate_prefs.CanTranslateLanguage(profile, "fr"));

  EXPECT_TRUE(CloseTranslateInfoBar());

  // Navigate to a new page also on the same site.
  SimulateNavigation(GURL("http://www.google.fr/hello"), "fr", true);

  // There should not be a translate infobar.
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);

  // Remove the site from the blacklist.
  SetPrefObserverExpectation(TranslatePrefs::kPrefTranslateSiteBlacklist);
  translate_prefs.RemoveSiteFromBlacklist(host);
  EXPECT_FALSE(translate_prefs.IsSiteBlacklisted(host));
  EXPECT_TRUE(translate_prefs.CanTranslateLanguage(profile, "fr"));

  // Navigate to a page in French.
  SimulateNavigation(url, "fr", true);

  // There should be a translate infobar.
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);
}

// Tests the "Always translate this language" pref.
TEST_F(TranslateManagerBrowserTest, AlwaysTranslateLanguagePref) {
  // Select always translate French to English.
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  PrefService* prefs = profile->GetPrefs();
  PrefChangeRegistrar registrar;
  registrar.Init(prefs);
  registrar.Add(TranslatePrefs::kPrefTranslateWhitelists, pref_callback_);
  TranslatePrefs translate_prefs(prefs);
  SetPrefObserverExpectation(TranslatePrefs::kPrefTranslateWhitelists);
  translate_prefs.WhitelistLanguagePair("fr", "en");

  // Load a page in French.
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);

  // It should have triggered an automatic translation to English.

  // The translating infobar should be showing.
  TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::TRANSLATING, infobar->infobar_type());
  SimulateTranslateScriptURLFetch(true);
  int page_id = 0;
  std::string original_lang, target_lang;
  EXPECT_TRUE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  EXPECT_EQ("fr", original_lang);
  EXPECT_EQ("en", target_lang);
  process()->sink().ClearMessages();

  // Try another language, it should not be autotranslated.
  SimulateNavigation(GURL("http://www.google.es"), "es", true);
  EXPECT_FALSE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);
  EXPECT_TRUE(CloseTranslateInfoBar());

  // Let's switch to incognito mode, it should not be autotranslated in that
  // case either.
  TestingProfile* test_profile =
      static_cast<TestingProfile*>(web_contents()->GetBrowserContext());
  test_profile->set_incognito(true);
  SimulateNavigation(GURL("http://www.youtube.fr"), "fr", true);
  EXPECT_FALSE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  EXPECT_TRUE(GetTranslateInfoBar() != NULL);
  EXPECT_TRUE(CloseTranslateInfoBar());
  test_profile->set_incognito(false);  // Get back to non incognito.

  // Now revert the always translate pref and make sure we go back to expected
  // behavior, which is show a "before translate" infobar.
  SetPrefObserverExpectation(TranslatePrefs::kPrefTranslateWhitelists);
  translate_prefs.RemoveLanguagePairFromWhitelist("fr", "en");
  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);
  EXPECT_FALSE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::BEFORE_TRANSLATE,
            infobar->infobar_type());
}

// Context menu.
TEST_F(TranslateManagerBrowserTest, ContextMenu) {
  // Blacklist www.google.fr and French for translation.
  GURL url("http://www.google.fr");
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  TranslatePrefs translate_prefs(profile->GetPrefs());
  translate_prefs.BlockLanguage("fr");
  translate_prefs.BlacklistSite(url.host());
  EXPECT_TRUE(translate_prefs.IsBlockedLanguage("fr"));
  EXPECT_TRUE(translate_prefs.IsSiteBlacklisted(url.host()));

  // Simulate navigating to a page in French. The translate menu should show but
  // should only be enabled when the page language has been received.
  NavigateAndCommit(url);
  scoped_ptr<TestRenderViewContextMenu> menu(
      TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  EXPECT_TRUE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_TRANSLATE));
  EXPECT_FALSE(menu->IsCommandIdEnabled(IDC_CONTENT_CONTEXT_TRANSLATE));

  // Simulate receiving the language.
  SimulateOnTranslateLanguageDetermined("fr", true);
  menu.reset(TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  EXPECT_TRUE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_TRANSLATE));
  EXPECT_TRUE(menu->IsCommandIdEnabled(IDC_CONTENT_CONTEXT_TRANSLATE));

  // Use the menu to translate the page.
  menu->ExecuteCommand(IDC_CONTENT_CONTEXT_TRANSLATE, 0);

  // That should have triggered a translation.
  // The "translating..." infobar should be showing.
  TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  EXPECT_EQ(TranslateInfoBarDelegate::TRANSLATING, infobar->infobar_type());
  SimulateTranslateScriptURLFetch(true);
  int page_id = 0;
  std::string original_lang, target_lang;
  EXPECT_TRUE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  EXPECT_EQ("fr", original_lang);
  EXPECT_EQ("en", target_lang);
  process()->sink().ClearMessages();

  // This should also have reverted the blacklisting of this site and language.
  EXPECT_FALSE(translate_prefs.IsBlockedLanguage("fr"));
  EXPECT_FALSE(translate_prefs.IsSiteBlacklisted(url.host()));

  // Let's simulate the page being translated.
  SimulateOnPageTranslated("fr", "en");

  // The translate menu should now be disabled.
  menu.reset(TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  EXPECT_TRUE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_TRANSLATE));
  EXPECT_FALSE(menu->IsCommandIdEnabled(IDC_CONTENT_CONTEXT_TRANSLATE));

  // Test that selecting translate in the context menu WHILE the page is being
  // translated does nothing (this could happen if autotranslate kicks-in and
  // the user selects the menu while the translation is being performed).
  SimulateNavigation(GURL("http://www.google.es"), "es", true);
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  infobar->Translate();
  EXPECT_TRUE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  process()->sink().ClearMessages();
  menu.reset(TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  EXPECT_TRUE(menu->IsCommandIdEnabled(IDC_CONTENT_CONTEXT_TRANSLATE));
  menu->ExecuteCommand(IDC_CONTENT_CONTEXT_TRANSLATE, 0);
  // No message expected since the translation should have been ignored.
  EXPECT_FALSE(GetTranslateMessage(&page_id, &original_lang, &target_lang));

  // Now test that selecting translate in the context menu AFTER the page has
  // been translated does nothing.
  SimulateNavigation(GURL("http://www.google.de"), "de", true);
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  infobar->Translate();
  EXPECT_TRUE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  process()->sink().ClearMessages();
  menu.reset(TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  EXPECT_TRUE(menu->IsCommandIdEnabled(IDC_CONTENT_CONTEXT_TRANSLATE));
  SimulateOnPageTranslated("de", "en");
  menu->ExecuteCommand(IDC_CONTENT_CONTEXT_TRANSLATE, 0);
  // No message expected since the translation should have been ignored.
  EXPECT_FALSE(GetTranslateMessage(&page_id, &original_lang, &target_lang));

  // Test that the translate context menu is enabled when the page is in an
  // unknown language.
  SimulateNavigation(url, "und", true);
  menu.reset(TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  EXPECT_TRUE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_TRANSLATE));
  EXPECT_TRUE(menu->IsCommandIdEnabled(IDC_CONTENT_CONTEXT_TRANSLATE));

  // Test that the translate context menu is enabled even if the page is in an
  // unsupported language.
  SimulateNavigation(url, "qbz", true);
  menu.reset(TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  EXPECT_TRUE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_TRANSLATE));
  EXPECT_TRUE(menu->IsCommandIdEnabled(IDC_CONTENT_CONTEXT_TRANSLATE));
}

// Tests that an extra always/never translate button is shown on the "before
// translate" infobar when the translation is accepted/declined 3 times,
// only when not in incognito mode.
TEST_F(TranslateManagerBrowserTest, BeforeTranslateExtraButtons) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  TranslatePrefs translate_prefs(profile->GetPrefs());
  translate_prefs.ResetTranslationAcceptedCount("fr");
  translate_prefs.ResetTranslationDeniedCount("fr");
  translate_prefs.ResetTranslationAcceptedCount("de");
  translate_prefs.ResetTranslationDeniedCount("de");

  // We'll do 4 times in incognito mode first to make sure the button is not
  // shown in that case, then 4 times in normal mode.
  TranslateInfoBarDelegate* infobar;
  TestingProfile* test_profile =
      static_cast<TestingProfile*>(web_contents()->GetBrowserContext());
  static_cast<extensions::TestExtensionSystem*>(
      extensions::ExtensionSystem::Get(test_profile))->
      CreateExtensionProcessManager();
  test_profile->set_incognito(true);
  for (int i = 0; i < 8; ++i) {
    SCOPED_TRACE(::testing::Message() << "Iteration " << i <<
        " incognito mode=" << test_profile->IsOffTheRecord());
    SimulateNavigation(GURL("http://www.google.fr"), "fr", true);
    infobar = GetTranslateInfoBar();
    ASSERT_TRUE(infobar != NULL);
    EXPECT_EQ(TranslateInfoBarDelegate::BEFORE_TRANSLATE,
              infobar->infobar_type());
    if (i < 7) {
      EXPECT_FALSE(infobar->ShouldShowAlwaysTranslateShortcut());
      infobar->Translate();
      process()->sink().ClearMessages();
    } else {
      EXPECT_TRUE(infobar->ShouldShowAlwaysTranslateShortcut());
    }
    if (i == 3)
      test_profile->set_incognito(false);
  }
  // Simulate the user pressing "Always translate French".
  infobar->AlwaysTranslatePageLanguage();
  EXPECT_TRUE(translate_prefs.IsLanguagePairWhitelisted("fr", "en"));
  // Simulate the translate script being retrieved (it only needs to be done
  // once in the test as it is cached).
  SimulateTranslateScriptURLFetch(true);
  // That should have triggered a page translate.
  int page_id = 0;
  std::string original_lang, target_lang;
  EXPECT_TRUE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  process()->sink().ClearMessages();

  // Now test that declining the translation causes a "never translate" button
  // to be shown (in non incognito mode only).
  test_profile->set_incognito(true);
  for (int i = 0; i < 8; ++i) {
    SCOPED_TRACE(::testing::Message() << "Iteration " << i <<
        " incognito mode=" << test_profile->IsOffTheRecord());
    SimulateNavigation(GURL("http://www.google.de"), "de", true);
    infobar = GetTranslateInfoBar();
    ASSERT_TRUE(infobar != NULL);
    EXPECT_EQ(TranslateInfoBarDelegate::BEFORE_TRANSLATE,
              infobar->infobar_type());
    if (i < 7) {
      EXPECT_FALSE(infobar->ShouldShowNeverTranslateShortcut());
      infobar->TranslationDeclined();
    } else {
      EXPECT_TRUE(infobar->ShouldShowNeverTranslateShortcut());
    }
    if (i == 3)
      test_profile->set_incognito(false);
  }
  // Simulate the user pressing "Never translate French".
  infobar->NeverTranslatePageLanguage();
  EXPECT_TRUE(translate_prefs.IsBlockedLanguage("de"));
  // No translation should have occured and the infobar should be gone.
  EXPECT_FALSE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  process()->sink().ClearMessages();
  ASSERT_TRUE(GetTranslateInfoBar() == NULL);
}

// Tests that we don't show a translate infobar when a page instructs that it
// should not be translated.
TEST_F(TranslateManagerBrowserTest, NonTranslatablePage) {
  SimulateNavigation(GURL("http://mail.google.fr"), "fr", false);

  // We should not have an infobar.
  EXPECT_TRUE(GetTranslateInfoBar() == NULL);

  // The context menu is enabled to allow users to force translation.
  scoped_ptr<TestRenderViewContextMenu> menu(
      TestRenderViewContextMenu::CreateContextMenu(web_contents()));
  menu->Init();
  EXPECT_TRUE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_TRANSLATE));
  EXPECT_TRUE(menu->IsCommandIdEnabled(IDC_CONTENT_CONTEXT_TRANSLATE));
}

// Tests that the script is expired and refetched as expected.
TEST_F(TranslateManagerBrowserTest, ScriptExpires) {
  ExpireTranslateScriptImmediately();

  SimulateNavigation(GURL("http://www.google.fr"), "fr", true);
  TranslateInfoBarDelegate* infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  process()->sink().ClearMessages();
  infobar->Translate();
  SimulateTranslateScriptURLFetch(true);
  SimulateOnPageTranslated("fr", "en");

  // A task should have been posted to clear the script, run it.
  base::MessageLoop::current()->RunUntilIdle();

  // Do another navigation and translation.
  SimulateNavigation(GURL("http://www.google.es"), "es", true);
  infobar = GetTranslateInfoBar();
  ASSERT_TRUE(infobar != NULL);
  process()->sink().ClearMessages();
  infobar->Translate();
  // If we don't simulate the URL fetch, the TranslateManager should be waiting
  // for the script and no message should have been sent to the renderer.
  EXPECT_TRUE(
      process()->sink().GetFirstMessageMatching(
          ChromeViewMsg_TranslatePage::ID) == NULL);
  // Now simulate the URL fetch.
  SimulateTranslateScriptURLFetch(true);
  // Now the message should have been sent.
  int page_id = 0;
  std::string original_lang, target_lang;
  EXPECT_TRUE(GetTranslateMessage(&page_id, &original_lang, &target_lang));
  EXPECT_EQ("es", original_lang);
  EXPECT_EQ("en", target_lang);
}

TEST_F(TranslateManagerBrowserTest, DownloadsAndHistoryNotTranslated) {
  ASSERT_FALSE(TranslateManager::IsTranslatableURL(
      GURL(chrome::kChromeUIDownloadsURL)));
  ASSERT_FALSE(TranslateManager::IsTranslatableURL(
      GURL(chrome::kChromeUIHistoryURL)));
}

// Test is flaky on Win http://crbug.com/166334
#if defined(OS_WIN)
#define MAYBE_PRE_TranslateSessionRestore DISABLED_PRE_TranslateSessionRestore
#else
#define MAYBE_PRE_TranslateSessionRestore PRE_TranslateSessionRestore
#endif
// Test that session restore restores the translate infobar and other translate
// settings.
IN_PROC_BROWSER_TEST_F(InProcessBrowserTest,
                       MAYBE_PRE_TranslateSessionRestore) {
  SessionStartupPref pref(SessionStartupPref::LAST);
  SessionStartupPref::SetStartupPref(browser()->profile(), pref);

  content::WebContents* current_web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  TranslateTabHelper* translate_tab_helper =
      TranslateTabHelper::FromWebContents(current_web_contents);
  content::Source<content::WebContents> source(current_web_contents);

  ui_test_utils::WindowedNotificationObserverWithDetails<
    LanguageDetectionDetails>
      fr_language_detected_signal(chrome::NOTIFICATION_TAB_LANGUAGE_DETERMINED,
                                  source);

  GURL french_url = ui_test_utils::GetTestUrl(
      base::FilePath(), base::FilePath(FILE_PATH_LITERAL("french_page.html")));
  ui_test_utils::NavigateToURL(browser(), french_url);
  fr_language_detected_signal.Wait();
  LanguageDetectionDetails details;
  EXPECT_TRUE(fr_language_detected_signal.GetDetailsFor(
        source.map_key(), &details));
  EXPECT_EQ("fr", details.adopted_language);
  EXPECT_EQ("fr", translate_tab_helper->language_state().original_language());
}

#if defined (OS_WIN)
#define MAYBE_TranslateSessionRestore DISABLED_TranslateSessionRestore
#else
#define MAYBE_TranslateSessionRestore TranslateSessionRestore
#endif
IN_PROC_BROWSER_TEST_F(InProcessBrowserTest, MAYBE_TranslateSessionRestore) {
  content::WebContents* current_web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::Source<content::WebContents> source(current_web_contents);

  ui_test_utils::WindowedNotificationObserverWithDetails<
    LanguageDetectionDetails>
      fr_language_detected_signal(chrome::NOTIFICATION_TAB_LANGUAGE_DETERMINED,
                                  source);
  fr_language_detected_signal.Wait();
}
