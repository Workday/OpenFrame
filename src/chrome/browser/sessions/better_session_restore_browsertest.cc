// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/path_service.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/enterprise_extension_observer.h"
#include "chrome/browser/content_settings/cookie_settings.h"
#include "chrome/browser/infobars/confirm_infobar_delegate.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_impl.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/session_backend.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_iterator.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/content_settings.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/browser_test_utils.h"
#include "net/base/net_util.h"
#include "net/base/upload_bytes_element_reader.h"
#include "net/base/upload_data_stream.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_filter.h"
#include "net/url_request/url_request_test_job.h"

namespace {

Browser* FindOneOtherBrowserForProfile(Profile* profile,
                                       Browser* not_this_browser) {
  for (chrome::BrowserIterator it; !it.done(); it.Next()) {
    if (*it != not_this_browser && it->profile() == profile)
      return *it;
  }
  return NULL;
}

// We need to serve the test files so that PRE_Test and Test can access the same
// page using the same URL. In addition, perceived security origin of the page
// needs to stay the same, so e.g., redirecting the URL requests doesn't
// work. (If we used a test server, the PRE_Test and Test would have separate
// instances running on separate ports.)

base::LazyInstance<std::map<std::string, std::string> > g_file_contents =
    LAZY_INSTANCE_INITIALIZER;

net::URLRequestJob* URLRequestFaker(
    net::URLRequest* request,
    net::NetworkDelegate* network_delegate,
    const std::string& scheme) {
  return new net::URLRequestTestJob(
      request, network_delegate, net::URLRequestTestJob::test_headers(),
      g_file_contents.Get()[request->url().path()], true);
}

base::LazyInstance<std::string> g_last_upload_bytes = LAZY_INSTANCE_INITIALIZER;

net::URLRequestJob* URLRequestFakerForPostRequests(
    net::URLRequest* request,
    net::NetworkDelegate* network_delegate,
    const std::string& scheme) {
  // Read the uploaded data and store it to g_last_upload_bytes.
  const net::UploadDataStream* upload_data = request->get_upload();
  g_last_upload_bytes.Get().clear();
  if (upload_data) {
    const ScopedVector<net::UploadElementReader>& readers =
        upload_data->element_readers();
    for (size_t i = 0; i < readers.size(); ++i) {
      const net::UploadBytesElementReader* bytes_reader =
          readers[i]->AsBytesReader();
      if (bytes_reader) {
        g_last_upload_bytes.Get() +=
            std::string(bytes_reader->bytes(), bytes_reader->length());
      }
    }
  }
  return new net::URLRequestTestJob(
      request, network_delegate, net::URLRequestTestJob::test_headers(),
      "<html><head><title>PASS</title></head><body>Data posted</body></html>",
      true);
}

}  // namespace

class BetterSessionRestoreTest : public InProcessBrowserTest {
 public:
  BetterSessionRestoreTest()
      : fake_server_address_("http://www.test.com/"),
        test_path_("session_restore/"),
        title_pass_(ASCIIToUTF16("PASS")),
        title_storing_(ASCIIToUTF16("STORING")),
        title_error_write_failed_(ASCIIToUTF16("ERROR_WRITE_FAILED")),
        title_error_empty_(ASCIIToUTF16("ERROR_EMPTY")) {
    // Set up the URL request filtering.
    std::vector<std::string> test_files;
    test_files.push_back("common.js");
    test_files.push_back("cookies.html");
    test_files.push_back("local_storage.html");
    test_files.push_back("post.html");
    test_files.push_back("post_with_password.html");
    test_files.push_back("session_cookies.html");
    test_files.push_back("session_storage.html");
    base::FilePath test_file_dir;
    CHECK(PathService::Get(base::DIR_SOURCE_ROOT, &test_file_dir));
    test_file_dir =
        test_file_dir.AppendASCII("chrome/test/data").AppendASCII(test_path_);

    for (std::vector<std::string>::const_iterator it = test_files.begin();
         it != test_files.end(); ++it) {
      base::FilePath path = test_file_dir.AppendASCII(*it);
      std::string contents;
      CHECK(file_util::ReadFileToString(path, &contents));
      g_file_contents.Get()["/" + test_path_ + *it] = contents;
      net::URLRequestFilter::GetInstance()->AddUrlHandler(
          GURL(fake_server_address_ + test_path_ + *it),
          &URLRequestFaker);
    }
    net::URLRequestFilter::GetInstance()->AddUrlHandler(
        GURL(fake_server_address_ + test_path_ + "posted.php"),
        &URLRequestFakerForPostRequests);
  }

 protected:
  void StoreDataWithPage(const std::string& filename) {
    StoreDataWithPage(browser(), filename);
  }

  void StoreDataWithPage(Browser* browser, const std::string& filename) {
    content::WebContents* web_contents =
        browser->tab_strip_model()->GetActiveWebContents();
    content::TitleWatcher title_watcher(web_contents, title_storing_);
    title_watcher.AlsoWaitForTitle(title_pass_);
    title_watcher.AlsoWaitForTitle(title_error_write_failed_);
    title_watcher.AlsoWaitForTitle(title_error_empty_);
    ui_test_utils::NavigateToURL(
        browser, GURL(fake_server_address_ + test_path_ + filename));
    string16 final_title = title_watcher.WaitAndGetTitle();
    EXPECT_EQ(title_storing_, final_title);
  }

  void NavigateAndCheckStoredData(const std::string& filename) {
    NavigateAndCheckStoredData(browser(), filename);
  }

  void NavigateAndCheckStoredData(Browser* browser,
                                  const std::string& filename) {
    // Navigate to a page which has previously stored data; check that the
    // stored data can be accessed.
    content::WebContents* web_contents =
        browser->tab_strip_model()->GetActiveWebContents();
    content::TitleWatcher title_watcher(web_contents, title_pass_);
    title_watcher.AlsoWaitForTitle(title_storing_);
    title_watcher.AlsoWaitForTitle(title_error_write_failed_);
    title_watcher.AlsoWaitForTitle(title_error_empty_);
    ui_test_utils::NavigateToURL(
        browser, GURL(fake_server_address_ + test_path_ + filename));
    string16 final_title = title_watcher.WaitAndGetTitle();
    EXPECT_EQ(title_pass_, final_title);
  }

  void CheckReloadedPageRestored() {
    CheckTitle(browser(), title_pass_);
  }

  void CheckReloadedPageRestored(Browser* browser) {
    CheckTitle(browser, title_pass_);
  }

  void CheckReloadedPageNotRestored() {
    CheckTitle(browser(), title_storing_);
  }

  void CheckTitle(Browser* browser, const string16& expected_title) {
    content::WebContents* web_contents =
        browser->tab_strip_model()->GetWebContentsAt(0);
    content::TitleWatcher title_watcher(web_contents, expected_title);
    title_watcher.AlsoWaitForTitle(title_pass_);
    title_watcher.AlsoWaitForTitle(title_storing_);
    title_watcher.AlsoWaitForTitle(title_error_write_failed_);
    title_watcher.AlsoWaitForTitle(title_error_empty_);
    // It's possible that the title was already the right one before
    // title_watcher was created.
    string16 first_title = web_contents->GetTitle();
    if (first_title != title_pass_ &&
        first_title != title_storing_ &&
        first_title != title_error_write_failed_ &&
        first_title != title_error_empty_) {
      string16 final_title = title_watcher.WaitAndGetTitle();
      EXPECT_EQ(expected_title, final_title);
    } else {
      EXPECT_EQ(expected_title, first_title);
    }
  }

  void PostFormWithPage(const std::string& filename, bool password_present) {
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    content::TitleWatcher title_watcher(web_contents, title_pass_);
    ui_test_utils::NavigateToURL(
        browser(), GURL(fake_server_address_ + test_path_ + filename));
    string16 final_title = title_watcher.WaitAndGetTitle();
    EXPECT_EQ(title_pass_, final_title);
    EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-text") !=
                std::string::npos);
    EXPECT_TRUE(g_last_upload_bytes.Get().find("text-entered") !=
                std::string::npos);
    if (password_present) {
      EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-password") !=
                  std::string::npos);
      EXPECT_TRUE(g_last_upload_bytes.Get().find("password-entered") !=
                  std::string::npos);
    }
  }

  void CheckFormRestored(bool text_present, bool password_present) {
    CheckReloadedPageRestored();
    if (text_present) {
      EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-text") !=
                  std::string::npos);
      EXPECT_TRUE(g_last_upload_bytes.Get().find("text-entered") !=
                  std::string::npos);
    } else {
      EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-text") ==
                  std::string::npos);
      EXPECT_TRUE(g_last_upload_bytes.Get().find("text-entered") ==
                  std::string::npos);
    }
    if (password_present) {
      EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-password") !=
                  std::string::npos);
      EXPECT_TRUE(g_last_upload_bytes.Get().find("password-entered") !=
                  std::string::npos);
    } else {
      EXPECT_TRUE(g_last_upload_bytes.Get().find("posted-password") ==
                  std::string::npos);
      EXPECT_TRUE(g_last_upload_bytes.Get().find("password-entered") ==
                  std::string::npos);
    }
  }

  std::string fake_server_address() {
    return fake_server_address_;
  }

  std::string test_path() {
    return test_path_;
  }

 private:
  const std::string fake_server_address_;
  const std::string test_path_;
  const string16 title_pass_;
  const string16 title_storing_;
  const string16 title_error_write_failed_;
  const string16 title_error_empty_;

  DISALLOW_COPY_AND_ASSIGN(BetterSessionRestoreTest);
};

class ContinueWhereILeftOffTest : public BetterSessionRestoreTest {
 public:
  ContinueWhereILeftOffTest() { }

  virtual void SetUpOnMainThread() OVERRIDE {
    BetterSessionRestoreTest::SetUpOnMainThread();
    SessionStartupPref::SetStartupPref(
        browser()->profile(), SessionStartupPref(SessionStartupPref::LAST));
  }

  DISALLOW_COPY_AND_ASSIGN(ContinueWhereILeftOffTest);
};

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PRE_SessionCookies) {
  // Set the startup preference to "continue where I left off" and visit a page
  // which stores a session cookie.
  StoreDataWithPage("session_cookies.html");
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, SessionCookies) {
  // The browsing session will be continued; just wait for the page to reload
  // and check the stored data.
  CheckReloadedPageRestored();
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PRE_SessionStorage) {
  StoreDataWithPage("session_storage.html");
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, SessionStorage) {
  CheckReloadedPageRestored();
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest,
                       PRE_PRE_LocalStorageClearedOnExit) {
  StoreDataWithPage("local_storage.html");
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest,
                       PRE_LocalStorageClearedOnExit) {
  // Normally localStorage is restored.
  CheckReloadedPageRestored();
  // ... but not if it's set to clear on exit.
  CookieSettings::Factory::GetForProfile(browser()->profile())->
      SetDefaultCookieSetting(CONTENT_SETTING_SESSION_ONLY);
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, LocalStorageClearedOnExit) {
  CheckReloadedPageNotRestored();
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest,
                       PRE_PRE_CookiesClearedOnExit) {
  StoreDataWithPage("cookies.html");
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PRE_CookiesClearedOnExit) {
  // Normally cookies are restored.
  CheckReloadedPageRestored();
  // ... but not if the content setting is set to clear on exit.
  CookieSettings::Factory::GetForProfile(browser()->profile())->
      SetDefaultCookieSetting(CONTENT_SETTING_SESSION_ONLY);
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, CookiesClearedOnExit) {
  CheckReloadedPageNotRestored();
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PRE_Post) {
  PostFormWithPage("post.html", false);
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, Post) {
  CheckFormRestored(true, false);
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PRE_PostWithPassword) {
  PostFormWithPage("post_with_password.html", true);
}

IN_PROC_BROWSER_TEST_F(ContinueWhereILeftOffTest, PostWithPassword) {
  CheckReloadedPageRestored();
  // The form data contained passwords, so it's removed completely.
  CheckFormRestored(false, false);
}

class RestartTest : public BetterSessionRestoreTest {
 public:
  RestartTest() { }
  virtual ~RestartTest() { }
 protected:
  void Restart() {
    // Simluate restarting the browser, but let the test exit peacefully.
    for (chrome::BrowserIterator it; !it.done(); it.Next())
      content::BrowserContext::SaveSessionState(it->profile());
    PrefService* pref_service = g_browser_process->local_state();
    pref_service->SetBoolean(prefs::kWasRestarted, true);
#if defined(OS_WIN)
    if (pref_service->HasPrefPath(prefs::kRestartSwitchMode))
      pref_service->SetBoolean(prefs::kRestartSwitchMode, false);
#endif
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(RestartTest);
};

IN_PROC_BROWSER_TEST_F(RestartTest, PRE_SessionCookies) {
  StoreDataWithPage("session_cookies.html");
  Restart();
}

IN_PROC_BROWSER_TEST_F(RestartTest, SessionCookies) {
  CheckReloadedPageRestored();
}

IN_PROC_BROWSER_TEST_F(RestartTest, PRE_SessionStorage) {
  StoreDataWithPage("session_storage.html");
  Restart();
}

IN_PROC_BROWSER_TEST_F(RestartTest, SessionStorage) {
  CheckReloadedPageRestored();
}

IN_PROC_BROWSER_TEST_F(RestartTest, PRE_LocalStorageClearedOnExit) {
  StoreDataWithPage("local_storage.html");
  CookieSettings::Factory::GetForProfile(browser()->profile())->
      SetDefaultCookieSetting(CONTENT_SETTING_SESSION_ONLY);
  Restart();
}

IN_PROC_BROWSER_TEST_F(RestartTest, LocalStorageClearedOnExit) {
  CheckReloadedPageRestored();
}

IN_PROC_BROWSER_TEST_F(RestartTest, PRE_CookiesClearedOnExit) {
  StoreDataWithPage("cookies.html");
  CookieSettings::Factory::GetForProfile(browser()->profile())->
      SetDefaultCookieSetting(CONTENT_SETTING_SESSION_ONLY);
  Restart();
}

IN_PROC_BROWSER_TEST_F(RestartTest, CookiesClearedOnExit) {
  CheckReloadedPageRestored();
}

IN_PROC_BROWSER_TEST_F(RestartTest, PRE_Post) {
  PostFormWithPage("post.html", false);
  Restart();
}

IN_PROC_BROWSER_TEST_F(RestartTest, Post) {
  CheckFormRestored(true, false);
}

IN_PROC_BROWSER_TEST_F(RestartTest, PRE_PostWithPassword) {
  PostFormWithPage("post_with_password.html", true);
  Restart();
}

IN_PROC_BROWSER_TEST_F(RestartTest, PostWithPassword) {
  // The form data contained passwords, so it's removed completely.
  CheckFormRestored(false, false);
}

// These tests ensure that the Better Session Restore features are not triggered
// when they shouldn't be.
class NoSessionRestoreTest : public BetterSessionRestoreTest {
 public:
  NoSessionRestoreTest() { }

  virtual void SetUpOnMainThread() OVERRIDE {
    BetterSessionRestoreTest::SetUpOnMainThread();
    SessionStartupPref::SetStartupPref(
        browser()->profile(), SessionStartupPref(SessionStartupPref::DEFAULT));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(NoSessionRestoreTest);
};

IN_PROC_BROWSER_TEST_F(NoSessionRestoreTest, PRE_SessionCookies) {
  StoreDataWithPage("session_cookies.html");
}

IN_PROC_BROWSER_TEST_F(NoSessionRestoreTest, SessionCookies) {
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(std::string(content::kAboutBlankURL),
            web_contents->GetURL().spec());
  // When we navigate to the page again, it doens't see the data previously
  // stored.
  StoreDataWithPage("session_cookies.html");
}

IN_PROC_BROWSER_TEST_F(NoSessionRestoreTest, PRE_SessionStorage) {
  StoreDataWithPage("session_storage.html");
}

IN_PROC_BROWSER_TEST_F(NoSessionRestoreTest, SessionStorage) {
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(std::string(content::kAboutBlankURL),
            web_contents->GetURL().spec());
  StoreDataWithPage("session_storage.html");
}

IN_PROC_BROWSER_TEST_F(NoSessionRestoreTest,
                       PRE_PRE_LocalStorageClearedOnExit) {
  StoreDataWithPage("local_storage.html");
}

IN_PROC_BROWSER_TEST_F(NoSessionRestoreTest, PRE_LocalStorageClearedOnExit) {
  // Normally localStorage is persisted.
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(std::string(content::kAboutBlankURL),
            web_contents->GetURL().spec());
  NavigateAndCheckStoredData("local_storage.html");
  // ... but not if it's set to clear on exit.
  CookieSettings::Factory::GetForProfile(browser()->profile())->
      SetDefaultCookieSetting(CONTENT_SETTING_SESSION_ONLY);
}

IN_PROC_BROWSER_TEST_F(NoSessionRestoreTest, LocalStorageClearedOnExit) {
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(std::string(content::kAboutBlankURL),
            web_contents->GetURL().spec());
  StoreDataWithPage("local_storage.html");
}

IN_PROC_BROWSER_TEST_F(NoSessionRestoreTest, PRE_PRE_CookiesClearedOnExit) {
  StoreDataWithPage("cookies.html");
}

IN_PROC_BROWSER_TEST_F(NoSessionRestoreTest, PRE_CookiesClearedOnExit) {
  // Normally cookies are restored.
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(std::string(content::kAboutBlankURL),
            web_contents->GetURL().spec());
  NavigateAndCheckStoredData("cookies.html");
  // ... but not if the content setting is set to clear on exit.
  CookieSettings::Factory::GetForProfile(browser()->profile())->
      SetDefaultCookieSetting(CONTENT_SETTING_SESSION_ONLY);
}

IN_PROC_BROWSER_TEST_F(NoSessionRestoreTest, CookiesClearedOnExit) {
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(std::string(content::kAboutBlankURL),
            web_contents->GetURL().spec());
  StoreDataWithPage("local_storage.html");
}
