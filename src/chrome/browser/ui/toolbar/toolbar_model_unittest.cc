// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/toolbar/toolbar_model.h"

#include <vector>

#include "base/command_line.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_classifier_factory.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/search_terms_data.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/ssl_status.h"
#include "content/public/common/url_constants.h"
#include "net/base/escape.h"


// Test data ------------------------------------------------------------------

namespace {

struct TestItem {
  GURL url;
  string16 expected_text;
  // The expected text to display when query extraction is inactive.
  string16 expected_replace_text_inactive;
  // The expected text to display when query extraction is active.
  string16 expected_replace_text_active;
  bool would_replace;
  bool should_display;
} test_items[] = {
  {
    GURL("view-source:http://www.google.com"),
    ASCIIToUTF16("view-source:www.google.com"),
    ASCIIToUTF16("view-source:www.google.com"),
    ASCIIToUTF16("view-source:www.google.com"),
    false,
    true
  },
  {
    GURL("view-source:chrome://newtab/"),
    ASCIIToUTF16("view-source:chrome://newtab"),
    ASCIIToUTF16("view-source:chrome://newtab"),
    ASCIIToUTF16("view-source:chrome://newtab"),
    false,
    true
  },
  {
    GURL("chrome-extension://monkey/balls.html"),
    ASCIIToUTF16("chrome-extension://monkey/balls.html"),
    ASCIIToUTF16("chrome-extension://monkey/balls.html"),
    ASCIIToUTF16("chrome-extension://monkey/balls.html"),
    false,
    true
  },
  {
    GURL("chrome://newtab/"),
    string16(),
    string16(),
    string16(),
    false,
    false
  },
  {
    GURL(content::kAboutBlankURL),
    ASCIIToUTF16(content::kAboutBlankURL),
    ASCIIToUTF16(content::kAboutBlankURL),
    ASCIIToUTF16(content::kAboutBlankURL),
    false,
    true
  },
  {
    GURL("http://searchurl/?q=tractor+supply"),
    ASCIIToUTF16("searchurl/?q=tractor+supply"),
    ASCIIToUTF16("searchurl/?q=tractor+supply"),
    ASCIIToUTF16("searchurl/?q=tractor+supply"),
    false,
    true
  },
  {
    GURL("http://google.com/search?q=tractor+supply&espv=1"),
    ASCIIToUTF16("google.com/search?q=tractor+supply&espv=1"),
    ASCIIToUTF16("google.com/search?q=tractor+supply&espv=1"),
    ASCIIToUTF16("google.com/search?q=tractor+supply&espv=1"),
    false,
    true
  },
  {
    GURL("https://google.ca/search?q=tractor+supply"),
    ASCIIToUTF16("https://google.ca/search?q=tractor+supply"),
    ASCIIToUTF16("https://google.ca/search?q=tractor+supply"),
    ASCIIToUTF16("https://google.ca/search?q=tractor+supply"),
    false,
    true
  },
  {
    GURL("https://google.com/search?q=tractor+supply"),
    ASCIIToUTF16("https://google.com/search?q=tractor+supply"),
    ASCIIToUTF16("https://google.com/search?q=tractor+supply"),
    ASCIIToUTF16("https://google.com/search?q=tractor+supply"),
    false,
    true
  },
  {
    GURL("https://google.com/search?q=tractor+supply&espv=1"),
    ASCIIToUTF16("https://google.com/search?q=tractor+supply&espv=1"),
    ASCIIToUTF16("https://google.com/search?q=tractor+supply&espv=1"),
    ASCIIToUTF16("tractor supply"),
    true,
    true
  },
  {
    GURL("https://google.com/search?q=tractorsupply.com&espv=1"),
    ASCIIToUTF16("https://google.com/search?q=tractorsupply.com&espv=1"),
    ASCIIToUTF16("https://google.com/search?q=tractorsupply.com&espv=1"),
    ASCIIToUTF16("tractorsupply.com"),
    true,
    true
  },
  {
    GURL("https://google.com/search?q=ftp://tractorsupply.ie&espv=1"),
    ASCIIToUTF16("https://google.com/search?q=ftp://tractorsupply.ie&espv=1"),
    ASCIIToUTF16("https://google.com/search?q=ftp://tractorsupply.ie&espv=1"),
    ASCIIToUTF16("ftp://tractorsupply.ie"),
    true,
    true
  },
};

}  // namespace


// ToolbarModelTest -----------------------------------------------------------

class ToolbarModelTest : public BrowserWithTestWindowTest {
 public:
  ToolbarModelTest();
  virtual ~ToolbarModelTest();

  // BrowserWithTestWindowTest:
  virtual void SetUp() OVERRIDE;

 protected:
  void ResetDefaultTemplateURL();
  void NavigateAndCheckText(const GURL& url,
                            const string16& expected_text,
                            const string16& expected_replace_text,
                            bool would_replace,
                            bool should_display);

 private:
  void ResetTemplateURLForInstant(const GURL& instant_url);
  void NavigateAndCheckTextImpl(const GURL& url,
                                bool can_replace,
                                const string16 expected_text,
                                bool would_replace,
                                bool should_display);

  DISALLOW_COPY_AND_ASSIGN(ToolbarModelTest);
};

ToolbarModelTest::ToolbarModelTest() {
}

ToolbarModelTest::~ToolbarModelTest() {
}

void ToolbarModelTest::SetUp() {
  BrowserWithTestWindowTest::SetUp();
  TemplateURLServiceFactory::GetInstance()->SetTestingFactoryAndUse(
      profile(), &TemplateURLServiceFactory::BuildInstanceFor);
  AutocompleteClassifierFactory::GetInstance()->SetTestingFactoryAndUse(
      profile(), &AutocompleteClassifierFactory::BuildInstanceFor);
  UIThreadSearchTermsData::SetGoogleBaseURL("http://google.com/");
}

void ToolbarModelTest::ResetDefaultTemplateURL() {
  ResetTemplateURLForInstant(GURL("http://does/not/exist"));
}

void ToolbarModelTest::NavigateAndCheckText(
    const GURL& url,
    const string16& expected_text,
    const string16& expected_replace_text,
    bool would_replace,
    bool should_display) {
  NavigateAndCheckTextImpl(url, false, expected_text, would_replace,
                           should_display);
  NavigateAndCheckTextImpl(url, true, expected_replace_text, would_replace,
                           should_display);
}

void ToolbarModelTest::ResetTemplateURLForInstant(const GURL& instant_url) {
  TemplateURLData data;
  data.short_name = ASCIIToUTF16("Google");
  data.SetURL("{google:baseURL}search?q={searchTerms}");
  data.instant_url = instant_url.spec();
  data.search_terms_replacement_key = "{google:instantExtendedEnabledKey}";
  TemplateURL* search_template_url = new TemplateURL(profile(), data);
  TemplateURLService* template_url_service =
      TemplateURLServiceFactory::GetForProfile(profile());
  template_url_service->Add(search_template_url);
  template_url_service->SetDefaultSearchProvider(search_template_url);
  ASSERT_NE(0, search_template_url->id());
  template_url_service->Load();
}

void ToolbarModelTest::NavigateAndCheckTextImpl(const GURL& url,
                                                bool can_replace,
                                                const string16 expected_text,
                                                bool would_replace,
                                                bool should_display) {
  // The URL being navigated to should be treated as the Instant URL. Else
  // there will be no search term extraction.
  ResetTemplateURLForInstant(url);

  // Check while loading.
  content::NavigationController* controller =
      &browser()->tab_strip_model()->GetWebContentsAt(0)->GetController();
  controller->LoadURL(url, content::Referrer(), content::PAGE_TRANSITION_LINK,
                      std::string());
  ToolbarModel* toolbar_model = browser()->toolbar_model();
  EXPECT_EQ(should_display, toolbar_model->ShouldDisplayURL());
  EXPECT_EQ(expected_text, toolbar_model->GetText(can_replace));
  EXPECT_EQ(would_replace,
            toolbar_model->WouldReplaceSearchURLWithSearchTerms(false));

  // Check after commit.
  CommitPendingLoad(controller);
  // Fake a secure connection for HTTPS URLs, or the toolbar will refuse to
  // extract search terms.
  if (url.SchemeIsSecure()) {
    controller->GetVisibleEntry()->GetSSL().security_style =
        content::SECURITY_STYLE_AUTHENTICATED;
  }
  EXPECT_EQ(should_display, toolbar_model->ShouldDisplayURL());
  EXPECT_EQ(expected_text, toolbar_model->GetText(can_replace));
  EXPECT_EQ(would_replace,
            toolbar_model->WouldReplaceSearchURLWithSearchTerms(false));

  // Now pretend the user started modifying the omnibox.
  toolbar_model->SetInputInProgress(true);
  EXPECT_FALSE(toolbar_model->WouldReplaceSearchURLWithSearchTerms(false));
  EXPECT_EQ(would_replace,
            toolbar_model->WouldReplaceSearchURLWithSearchTerms(true));

  // Tell the ToolbarModel that the user has stopped editing.  This prevents
  // this function from having side effects.
  toolbar_model->SetInputInProgress(false);
}


// Actual tests ---------------------------------------------------------------

// Test that we don't replace any URLs when the query extraction is disabled.
TEST_F(ToolbarModelTest, ShouldDisplayURLQueryExtractionDisabled) {
  ASSERT_FALSE(chrome::IsQueryExtractionEnabled())
      << "This test expects query extraction to be disabled.";
  AddTab(browser(), GURL(content::kAboutBlankURL));
  for (size_t i = 0; i < arraysize(test_items); ++i) {
    const TestItem& test_item = test_items[i];
    NavigateAndCheckText(test_item.url, test_item.expected_text,
                         test_item.expected_replace_text_inactive, false,
                         test_item.should_display);
  }
}

// Test that we replace URLs when the query extraction API is enabled.
TEST_F(ToolbarModelTest, ShouldDisplayURLQueryExtractionEnabled) {
  chrome::EnableInstantExtendedAPIForTesting();
  AddTab(browser(), GURL(content::kAboutBlankURL));
  for (size_t i = 0; i < arraysize(test_items); ++i) {
    const TestItem& test_item = test_items[i];
    NavigateAndCheckText(test_item.url, test_item.expected_text,
                         test_item.expected_replace_text_active,
                         test_item.would_replace,
                         test_item.should_display);
  }
}

// Verify that search terms are extracted while the page is loading.
TEST_F(ToolbarModelTest, SearchTermsWhileLoading) {
  chrome::EnableInstantExtendedAPIForTesting();
  ResetDefaultTemplateURL();
  AddTab(browser(), GURL(content::kAboutBlankURL));

  // While loading, we should be willing to extract search terms.
  content::NavigationController* controller =
      &browser()->tab_strip_model()->GetWebContentsAt(0)->GetController();
  controller->LoadURL(GURL("https://google.com/search?q=tractor+supply&espv=1"),
                      content::Referrer(), content::PAGE_TRANSITION_LINK,
                      std::string());
  ToolbarModel* toolbar_model = browser()->toolbar_model();
  controller->GetVisibleEntry()->GetSSL().security_style =
      content::SECURITY_STYLE_UNKNOWN;
  EXPECT_TRUE(toolbar_model->WouldReplaceSearchURLWithSearchTerms(false));

  // When done loading, we shouldn't extract search terms if we didn't get an
  // authenticated connection.
  CommitPendingLoad(controller);
  controller->GetVisibleEntry()->GetSSL().security_style =
      content::SECURITY_STYLE_UNKNOWN;
  EXPECT_FALSE(toolbar_model->WouldReplaceSearchURLWithSearchTerms(false));
}

// When the Google base URL is overridden on the command line, we should extract
// search terms from URLs that start with that base URL even when they're not
// secure.
TEST_F(ToolbarModelTest, GoogleBaseURL) {
  chrome::EnableInstantExtendedAPIForTesting();
  AddTab(browser(), GURL(content::kAboutBlankURL));

  // If the Google base URL wasn't specified on the command line, then if it's
  // HTTP, we should not extract search terms.
  UIThreadSearchTermsData::SetGoogleBaseURL("http://www.foo.com/");
  NavigateAndCheckText(
      GURL("http://www.foo.com/search?q=tractor+supply&espv=1"),
      ASCIIToUTF16("www.foo.com/search?q=tractor+supply&espv=1"),
      ASCIIToUTF16("www.foo.com/search?q=tractor+supply&espv=1"), false,
      true);

  // The same URL, when specified on the command line, should allow search term
  // extraction.
  UIThreadSearchTermsData::SetGoogleBaseURL(std::string());
  CommandLine::ForCurrentProcess()->AppendSwitchASCII(switches::kGoogleBaseURL,
                                                      "http://www.foo.com/");
  NavigateAndCheckText(
      GURL("http://www.foo.com/search?q=tractor+supply&espv=1"),
      ASCIIToUTF16("www.foo.com/search?q=tractor+supply&espv=1"),
      ASCIIToUTF16("tractor supply"), true, true);
}
