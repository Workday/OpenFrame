// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/history_url_provider.h"

#include <algorithm>

#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_provider.h"
#include "chrome/browser/autocomplete/autocomplete_provider_listener.h"
#include "chrome/browser/autocomplete/history_quick_provider.h"
#include "chrome/browser/history/history_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/common/net/url_fixer_upper.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Time;
using base::TimeDelta;

using content::TestBrowserThreadBundle;

struct TestURLInfo {
  const char* url;
  const char* title;
  int visit_count;
  int typed_count;
} test_db[] = {
  {"http://www.google.com/", "Google", 3, 3},

  // High-quality pages should get a host synthesized as a lower-quality match.
  {"http://slashdot.org/favorite_page.html", "Favorite page", 200, 100},

  // Less popular pages should have hosts synthesized as higher-quality
  // matches.
  {"http://kerneltrap.org/not_very_popular.html", "Less popular", 4, 0},

  // Unpopular pages should not appear in the results at all.
  {"http://freshmeat.net/unpopular.html", "Unpopular", 1, 0},

  // If a host has a match, we should pick it up during host synthesis.
  {"http://news.google.com/?ned=us&topic=n", "Google News - U.S.", 2, 2},
  {"http://news.google.com/", "Google News", 1, 1},

  // Matches that are normally not inline-autocompletable should be
  // autocompleted if they are shorter substitutes for longer matches that would
  // have been inline autocompleted.
  {"http://synthesisatest.com/foo/", "Test A", 1, 1},
  {"http://synthesisbtest.com/foo/", "Test B", 1, 1},
  {"http://synthesisbtest.com/foo/bar.html", "Test B Bar", 2, 2},

  // Suggested short URLs must be "good enough" and must match user input.
  {"http://foo.com/", "Dir", 5, 5},
  {"http://foo.com/dir/", "Dir", 2, 2},
  {"http://foo.com/dir/another/", "Dir", 5, 1},
  {"http://foo.com/dir/another/again/", "Dir", 10, 0},
  {"http://foo.com/dir/another/again/myfile.html", "File", 10, 2},

  // We throw in a lot of extra URLs here to make sure we're testing the
  // history database's query, not just the autocomplete provider.
  {"http://startest.com/y/a", "A", 2, 2},
  {"http://startest.com/y/b", "B", 5, 2},
  {"http://startest.com/x/c", "C", 5, 2},
  {"http://startest.com/x/d", "D", 5, 5},
  {"http://startest.com/y/e", "E", 4, 2},
  {"http://startest.com/y/f", "F", 3, 2},
  {"http://startest.com/y/g", "G", 3, 2},
  {"http://startest.com/y/h", "H", 3, 2},
  {"http://startest.com/y/i", "I", 3, 2},
  {"http://startest.com/y/j", "J", 3, 2},
  {"http://startest.com/y/k", "K", 3, 2},
  {"http://startest.com/y/l", "L", 3, 2},
  {"http://startest.com/y/m", "M", 3, 2},

  // A file: URL is useful for testing that fixup does the right thing w.r.t.
  // the number of trailing slashes on the user's input.
  {"file:///C:/foo.txt", "", 2, 2},

  // Results with absurdly high typed_counts so that very generic queries like
  // "http" will give consistent results even if more data is added above.
  {"http://bogussite.com/a", "Bogus A", 10002, 10000},
  {"http://bogussite.com/b", "Bogus B", 10001, 10000},
  {"http://bogussite.com/c", "Bogus C", 10000, 10000},

  // Domain name with number.
  {"http://www.17173.com/", "Domain with number", 3, 3},

  // URLs to test exact-matching behavior.
  {"http://go/", "Intranet URL", 1, 1},
  {"http://gooey/", "Intranet URL 2", 5, 5},

  // URLs for testing offset adjustment.
  {"http://www.\xEA\xB5\x90\xEC\x9C\xA1.kr/", "Korean", 2, 2},
  {"http://spaces.com/path%20with%20spaces/foo.html", "Spaces", 2, 2},
  {"http://ms/c++%20style%20guide", "Style guide", 2, 2},

  // URLs for testing ctrl-enter behavior.
  {"http://binky/", "Intranet binky", 2, 2},
  {"http://winky/", "Intranet winky", 2, 2},
  {"http://www.winky.com/", "Internet winky", 5, 0},

  // URLs used by EmptyVisits.
  {"http://pandora.com/", "Pandora", 2, 2},
  // This entry is explicitly added more recently than
  // history::kLowQualityMatchAgeLimitInDays.
  // {"http://p/", "p", 0, 0},

  // For intranet based tests.
  {"http://intra/one", "Intranet", 2, 2},
  {"http://intra/two", "Intranet two", 1, 1},
  {"http://intra/three", "Intranet three", 2, 2},
  {"http://moo/bar", "Intranet moo", 1, 1},
  {"http://typedhost/typedpath", "Intranet typed", 1, 1},
  {"http://typedhost/untypedpath", "Intranet untyped", 1, 0},

  {"http://x.com/one", "Internet", 2, 2},
  {"http://x.com/two", "Internet two", 1, 1},
  {"http://x.com/three", "Internet three", 2, 2},
};

class HistoryURLProviderTest : public testing::Test,
                               public AutocompleteProviderListener {
 public:
  struct UrlAndLegalDefault {
    std::string url;
    bool allowed_to_be_default_match;
  };

  HistoryURLProviderTest()
      : sort_matches_(false) {
    HistoryQuickProvider::set_disabled(true);
  }

  virtual ~HistoryURLProviderTest() {
    HistoryQuickProvider::set_disabled(false);
  }

  // AutocompleteProviderListener:
  virtual void OnProviderUpdate(bool updated_matches) OVERRIDE;

 protected:
  static BrowserContextKeyedService* CreateTemplateURLService(
      content::BrowserContext* profile) {
    return new TemplateURLService(static_cast<Profile*>(profile));
  }

  // testing::Test
  virtual void SetUp() {
    ASSERT_TRUE(SetUpImpl(false));
  }
  virtual void TearDown();

  // Does the real setup.
  bool SetUpImpl(bool no_db) WARN_UNUSED_RESULT;

  // Fills test data into the history system.
  void FillData();

  // Runs an autocomplete query on |text| and checks to see that the returned
  // results' destination URLs match those provided.  Also allows checking
  // that the input type was identified correctly.
  void RunTest(const string16 text,
               const string16& desired_tld,
               bool prevent_inline_autocomplete,
               const UrlAndLegalDefault* expected_urls,
               size_t num_results,
               AutocompleteInput::Type* identified_input_type);

  // A version of the above without the final |type| output parameter.
  void RunTest(const string16 text,
               const string16& desired_tld,
               bool prevent_inline_autocomplete,
               const UrlAndLegalDefault* expected_urls,
               size_t num_results) {
    AutocompleteInput::Type type;
    return RunTest(text, desired_tld, prevent_inline_autocomplete,
                   expected_urls, num_results, &type);
  }

  content::TestBrowserThreadBundle thread_bundle_;
  ACMatches matches_;
  scoped_ptr<TestingProfile> profile_;
  HistoryService* history_service_;
  scoped_refptr<HistoryURLProvider> autocomplete_;
  // Should the matches be sorted and duplicates removed?
  bool sort_matches_;
};

class HistoryURLProviderTestNoDB : public HistoryURLProviderTest {
 protected:
  virtual void SetUp() {
    ASSERT_TRUE(SetUpImpl(true));
  }
};

void HistoryURLProviderTest::OnProviderUpdate(bool updated_matches) {
  if (autocomplete_->done())
    base::MessageLoop::current()->Quit();
}

bool HistoryURLProviderTest::SetUpImpl(bool no_db) {
  profile_.reset(new TestingProfile());
  if (!(profile_->CreateHistoryService(true, no_db)))
    return false;
  if (!no_db) {
    profile_->BlockUntilHistoryProcessesPendingRequests();
    profile_->BlockUntilHistoryIndexIsRefreshed();
  }
  history_service_ =
      HistoryServiceFactory::GetForProfile(profile_.get(),
                                           Profile::EXPLICIT_ACCESS);

  autocomplete_ = new HistoryURLProvider(this, profile_.get(), "en-US,en,ko");
  TemplateURLServiceFactory::GetInstance()->SetTestingFactoryAndUse(
      profile_.get(), &HistoryURLProviderTest::CreateTemplateURLService);
  FillData();
  return true;
}

void HistoryURLProviderTest::TearDown() {
  autocomplete_ = NULL;
}

void HistoryURLProviderTest::FillData() {
  // All visits are a long time ago (some tests require this since we do some
  // special logic for things visited very recently). Note that this time must
  // be more recent than the "archived history" threshold for the data to go
  // into the main database.
  //
  // TODO(brettw) It would be nice if we could test this behavior, in which
  // case the time would be specifed in the test_db structure.
  Time visit_time = Time::Now() - TimeDelta::FromDays(80);

  for (size_t i = 0; i < arraysize(test_db); ++i) {
    const TestURLInfo& cur = test_db[i];
    const GURL current_url(cur.url);
    history_service_->AddPageWithDetails(current_url, UTF8ToUTF16(cur.title),
                                         cur.visit_count, cur.typed_count,
                                         visit_time, false,
                                         history::SOURCE_BROWSED);
  }

  history_service_->AddPageWithDetails(
      GURL("http://p/"), UTF8ToUTF16("p"), 0, 0,
      Time::Now() -
      TimeDelta::FromDays(history::kLowQualityMatchAgeLimitInDays - 1),
      false, history::SOURCE_BROWSED);
}

void HistoryURLProviderTest::RunTest(
    const string16 text,
    const string16& desired_tld,
    bool prevent_inline_autocomplete,
    const UrlAndLegalDefault* expected_urls,
    size_t num_results,
    AutocompleteInput::Type* identified_input_type) {
  AutocompleteInput input(text, string16::npos, desired_tld, GURL(),
                          AutocompleteInput::INVALID_SPEC,
                          prevent_inline_autocomplete, false, true,
                          AutocompleteInput::ALL_MATCHES);
  *identified_input_type = input.type();
  autocomplete_->Start(input, false);
  if (!autocomplete_->done())
    base::MessageLoop::current()->Run();

  matches_ = autocomplete_->matches();
  if (sort_matches_) {
    for (ACMatches::iterator i = matches_.begin(); i != matches_.end(); ++i)
      i->ComputeStrippedDestinationURL(profile_.get());
    std::sort(matches_.begin(), matches_.end(),
              &AutocompleteMatch::DestinationSortFunc);
    matches_.erase(std::unique(matches_.begin(), matches_.end(),
                               &AutocompleteMatch::DestinationsEqual),
                   matches_.end());
    std::sort(matches_.begin(), matches_.end(),
              &AutocompleteMatch::MoreRelevant);
  }
  ASSERT_EQ(num_results, matches_.size()) << "Input text: " << text
                                          << "\nTLD: \"" << desired_tld << "\"";
  for (size_t i = 0; i < num_results; ++i) {
    EXPECT_EQ(expected_urls[i].url, matches_[i].destination_url.spec());
    EXPECT_EQ(expected_urls[i].allowed_to_be_default_match,
              matches_[i].allowed_to_be_default_match);
  }
}

TEST_F(HistoryURLProviderTest, PromoteShorterURLs) {
  // Test that hosts get synthesized below popular pages.
  const UrlAndLegalDefault expected_nonsynth[] = {
    { "http://slashdot.org/favorite_page.html", false },
    { "http://slashdot.org/", false }
  };
  RunTest(ASCIIToUTF16("slash"), string16(), true, expected_nonsynth,
          arraysize(expected_nonsynth));

  // Test that hosts get synthesized above less popular pages.
  const UrlAndLegalDefault expected_synth[] = {
    { "http://kerneltrap.org/", false },
    { "http://kerneltrap.org/not_very_popular.html", false }
  };
  RunTest(ASCIIToUTF16("kernel"), string16(), true, expected_synth,
          arraysize(expected_synth));

  // Test that unpopular pages are ignored completely.
  RunTest(ASCIIToUTF16("fresh"), string16(), true, NULL, 0);

  // Test that if we create or promote shorter suggestions that would not
  // normally be inline autocompletable, we make them inline autocompletable if
  // the original suggestion (that we replaced as "top") was inline
  // autocompletable.
  const UrlAndLegalDefault expected_synthesisa[] = {
    { "http://synthesisatest.com/", true },
    { "http://synthesisatest.com/foo/", true }
  };
  RunTest(ASCIIToUTF16("synthesisa"), string16(), false, expected_synthesisa,
          arraysize(expected_synthesisa));
  EXPECT_LT(matches_.front().relevance, 1200);
  const UrlAndLegalDefault expected_synthesisb[] = {
    { "http://synthesisbtest.com/foo/", true },
    { "http://synthesisbtest.com/foo/bar.html", true }
  };
  RunTest(ASCIIToUTF16("synthesisb"), string16(), false, expected_synthesisb,
          arraysize(expected_synthesisb));
  EXPECT_GE(matches_.front().relevance, 1410);

  // Test that if we have a synthesized host that matches a suggestion, they
  // get combined into one.
  const UrlAndLegalDefault expected_combine[] = {
    { "http://news.google.com/", false },
    { "http://news.google.com/?ned=us&topic=n", false },
  };
  ASSERT_NO_FATAL_FAILURE(RunTest(ASCIIToUTF16("news"), string16(), true,
      expected_combine, arraysize(expected_combine)));
  // The title should also have gotten set properly on the host for the
  // synthesized one, since it was also in the results.
  EXPECT_EQ(ASCIIToUTF16("Google News"), matches_.front().description);

  // Test that short URL matching works correctly as the user types more
  // (several tests):
  // The entry for foo.com is the best of all five foo.com* entries.
  const UrlAndLegalDefault short_1[] = {
    { "http://foo.com/", false },
    { "http://foo.com/dir/another/again/myfile.html", false },
    { "http://foo.com/dir/", false }
  };
  RunTest(ASCIIToUTF16("foo"), string16(), true, short_1, arraysize(short_1));

  // When the user types the whole host, make sure we don't get two results for
  // it.
  const UrlAndLegalDefault short_2[] = {
    { "http://foo.com/", true },
    { "http://foo.com/dir/another/again/myfile.html", false },
    { "http://foo.com/dir/", false },
    { "http://foo.com/dir/another/", false }
  };
  RunTest(ASCIIToUTF16("foo.com"), string16(), true, short_2,
          arraysize(short_2));
  RunTest(ASCIIToUTF16("foo.com/"), string16(), true, short_2,
          arraysize(short_2));

  // The filename is the second best of the foo.com* entries, but there is a
  // shorter URL that's "good enough".  The host doesn't match the user input
  // and so should not appear.
  const UrlAndLegalDefault short_3[] = {
    { "http://foo.com/d", true },
    { "http://foo.com/dir/another/", false },
    { "http://foo.com/dir/another/again/myfile.html", false },
    { "http://foo.com/dir/", false }
  };
  RunTest(ASCIIToUTF16("foo.com/d"), string16(), true, short_3,
          arraysize(short_3));

  // We shouldn't promote shorter URLs than the best if they're not good
  // enough.
  const UrlAndLegalDefault short_4[] = {
    { "http://foo.com/dir/another/a", true },
    { "http://foo.com/dir/another/again/myfile.html", false },
    { "http://foo.com/dir/another/again/", false }
  };
  RunTest(ASCIIToUTF16("foo.com/dir/another/a"), string16(), true, short_4,
          arraysize(short_4));

  // Exact matches should always be best no matter how much more another match
  // has been typed.
  const UrlAndLegalDefault short_5a[] = {
    { "http://gooey/", true },
    { "http://www.google.com/", true },
    { "http://go/", true }
  };
  const UrlAndLegalDefault short_5b[] = {
    { "http://go/", true },
    { "http://gooey/", true },
    { "http://www.google.com/", true }
  };
  RunTest(ASCIIToUTF16("g"), string16(), false, short_5a, arraysize(short_5a));
  RunTest(ASCIIToUTF16("go"), string16(), false, short_5b, arraysize(short_5b));
}

TEST_F(HistoryURLProviderTest, CullRedirects) {
  // URLs we will be using, plus the visit counts they will initially get
  // (the redirect set below will also increment the visit counts). We want
  // the results to be in A,B,C order. Note also that our visit counts are
  // all high enough so that domain synthesizing won't get triggered.
  struct TestCase {
    const char* url;
    int count;
  } test_cases[] = {
    {"http://redirects/A", 30},
    {"http://redirects/B", 20},
    {"http://redirects/C", 10}
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(test_cases); ++i) {
    history_service_->AddPageWithDetails(GURL(test_cases[i].url),
        UTF8ToUTF16("Title"), test_cases[i].count, test_cases[i].count,
        Time::Now(), false, history::SOURCE_BROWSED);
  }

  // Create a B->C->A redirect chain, but set the visit counts such that they
  // will appear in A,B,C order in the results. The autocomplete query will
  // search for the most recent visit when looking for redirects, so this will
  // be found even though the previous visits had no redirects.
  history::RedirectList redirects_to_a;
  redirects_to_a.push_back(GURL(test_cases[1].url));
  redirects_to_a.push_back(GURL(test_cases[2].url));
  redirects_to_a.push_back(GURL(test_cases[0].url));
  history_service_->AddPage(GURL(test_cases[0].url), base::Time::Now(),
      NULL, 0, GURL(), redirects_to_a, content::PAGE_TRANSITION_TYPED,
      history::SOURCE_BROWSED, true);

  // Because all the results are part of a redirect chain with other results,
  // all but the first one (A) should be culled. We should get the default
  // "what you typed" result, plus this one.
  const string16 typing(ASCIIToUTF16("http://redirects/"));
  const UrlAndLegalDefault expected_results[] = {
    { UTF16ToUTF8(typing), true },
    { test_cases[0].url, false }
  };
  RunTest(typing, string16(), true, expected_results,
          arraysize(expected_results));
}

TEST_F(HistoryURLProviderTest, WhatYouTyped) {
  // Make sure we suggest a What You Typed match at the right times.
  RunTest(ASCIIToUTF16("wytmatch"), string16(), false, NULL, 0);
  RunTest(ASCIIToUTF16("wytmatch foo bar"), string16(), false, NULL, 0);
  RunTest(ASCIIToUTF16("wytmatch+foo+bar"), string16(), false, NULL, 0);
  RunTest(ASCIIToUTF16("wytmatch+foo+bar.com"), string16(), false, NULL, 0);

  const UrlAndLegalDefault results_1[] = {
    { "http://www.wytmatch.com/", true }
  };
  RunTest(ASCIIToUTF16("wytmatch"), ASCIIToUTF16("com"), false, results_1,
          arraysize(results_1));

  const UrlAndLegalDefault results_2[] = {
    { "http://wytmatch%20foo%20bar/", true }
  };
  RunTest(ASCIIToUTF16("http://wytmatch foo bar"), string16(), false, results_2,
          arraysize(results_2));

  const UrlAndLegalDefault results_3[] = {
    { "https://wytmatch%20foo%20bar/", true }
  };
  RunTest(ASCIIToUTF16("https://wytmatch foo bar"), string16(), false,
          results_3, arraysize(results_3));
}

TEST_F(HistoryURLProviderTest, Fixup) {
  // Test for various past crashes we've had.
  RunTest(ASCIIToUTF16("\\"), string16(), false, NULL, 0);
  RunTest(ASCIIToUTF16("#"), string16(), false, NULL, 0);
  RunTest(ASCIIToUTF16("%20"), string16(), false, NULL, 0);
  const UrlAndLegalDefault fixup_crash[] = {
    { "http://%EF%BD%A5@s/", true }
  };
  RunTest(WideToUTF16(L"\uff65@s"), string16(), false, fixup_crash,
          arraysize(fixup_crash));
  RunTest(WideToUTF16(L"\u2015\u2015@ \uff7c"), string16(), false, NULL, 0);

  // Fixing up "file:" should result in an inline autocomplete offset of just
  // after "file:", not just after "file://".
  const string16 input_1(ASCIIToUTF16("file:"));
  const UrlAndLegalDefault fixup_1[] = {
    { "file:///C:/foo.txt", true }
  };
  ASSERT_NO_FATAL_FAILURE(RunTest(input_1, string16(), false, fixup_1,
                                  arraysize(fixup_1)));
  EXPECT_EQ(ASCIIToUTF16("///C:/foo.txt"),
            matches_.front().inline_autocompletion);

  // Fixing up "http:/" should result in an inline autocomplete offset of just
  // after "http:/", not just after "http:".
  const string16 input_2(ASCIIToUTF16("http:/"));
  const UrlAndLegalDefault fixup_2[] = {
    { "http://bogussite.com/a", true },
    { "http://bogussite.com/b", true },
    { "http://bogussite.com/c", true }
  };
  ASSERT_NO_FATAL_FAILURE(RunTest(input_2, string16(), false, fixup_2,
                                  arraysize(fixup_2)));
  EXPECT_EQ(ASCIIToUTF16("/bogussite.com/a"),
            matches_.front().inline_autocompletion);

  // Adding a TLD to a small number like "56" should result in "www.56.com"
  // rather than "0.0.0.56.com".
  const UrlAndLegalDefault fixup_3[] = {
    { "http://www.56.com/", true }
  };
  RunTest(ASCIIToUTF16("56"), ASCIIToUTF16("com"), true, fixup_3,
          arraysize(fixup_3));

  // An input looks like a IP address like "127.0.0.1" should result in
  // "http://127.0.0.1/".
  const UrlAndLegalDefault fixup_4[] = {
    { "http://127.0.0.1/", true }
  };
  RunTest(ASCIIToUTF16("127.0.0.1"), string16(), false, fixup_4,
          arraysize(fixup_4));

  // An number "17173" should result in "http://www.17173.com/" in db.
  const UrlAndLegalDefault fixup_5[] = {
    { "http://www.17173.com/", true }
  };
  RunTest(ASCIIToUTF16("17173"), string16(), false, fixup_5,
          arraysize(fixup_5));
}

// Make sure the results for the input 'p' don't change between the first and
// second passes.
TEST_F(HistoryURLProviderTest, EmptyVisits) {
  // Wait for history to create the in memory DB.
  profile_->BlockUntilHistoryProcessesPendingRequests();

  AutocompleteInput input(ASCIIToUTF16("p"), string16::npos, string16(), GURL(),
                          AutocompleteInput::INVALID_SPEC, false, false, true,
                          AutocompleteInput::ALL_MATCHES);
  autocomplete_->Start(input, false);
  // HistoryURLProvider shouldn't be done (waiting on async results).
  EXPECT_FALSE(autocomplete_->done());

  // We should get back an entry for pandora.
  matches_ = autocomplete_->matches();
  ASSERT_GT(matches_.size(), 0u);
  EXPECT_EQ(GURL("http://pandora.com/"), matches_[0].destination_url);
  int pandora_relevance = matches_[0].relevance;

  // Run the message loop. When |autocomplete_| finishes the loop is quit.
  base::MessageLoop::current()->Run();
  EXPECT_TRUE(autocomplete_->done());
  matches_ = autocomplete_->matches();
  ASSERT_GT(matches_.size(), 0u);
  EXPECT_EQ(GURL("http://pandora.com/"), matches_[0].destination_url);
  EXPECT_EQ(pandora_relevance, matches_[0].relevance);
}

TEST_F(HistoryURLProviderTestNoDB, NavigateWithoutDB) {
  // Ensure that we will still produce matches for navigation when there is no
  // database.
  UrlAndLegalDefault navigation_1[] = {
    { "http://test.com/", true }
  };
  RunTest(ASCIIToUTF16("test.com"), string16(), false, navigation_1,
          arraysize(navigation_1));

  UrlAndLegalDefault navigation_2[] = {
    { "http://slash/", true }
  };
  RunTest(ASCIIToUTF16("slash"), string16(), false, navigation_2,
          arraysize(navigation_2));

  RunTest(ASCIIToUTF16("this is a query"), string16(), false, NULL, 0);
}

TEST_F(HistoryURLProviderTest, DontAutocompleteOnTrailingWhitespace) {
  AutocompleteInput input(ASCIIToUTF16("slash "), string16::npos, string16(),
                          GURL(), AutocompleteInput::INVALID_SPEC, false, false,
                          true, AutocompleteInput::ALL_MATCHES);
  autocomplete_->Start(input, false);
  if (!autocomplete_->done())
    base::MessageLoop::current()->Run();

  // None of the matches should attempt to autocomplete.
  matches_ = autocomplete_->matches();
  for (size_t i = 0; i < matches_.size(); ++i) {
    EXPECT_TRUE(matches_[i].inline_autocompletion.empty());
    EXPECT_FALSE(matches_[i].allowed_to_be_default_match);
  }
}

TEST_F(HistoryURLProviderTest, TreatEmailsAsSearches) {
  // Visiting foo.com should not make this string be treated as a navigation.
  // That means the result should be scored around 1200 ("what you typed")
  // and not 1400+.
  const UrlAndLegalDefault expected[] = {
    { "http://user@foo.com/", true }
  };
  ASSERT_NO_FATAL_FAILURE(RunTest(ASCIIToUTF16("user@foo.com"), string16(),
                                  false, expected, arraysize(expected)));
  EXPECT_LE(1200, matches_[0].relevance);
  EXPECT_LT(matches_[0].relevance, 1210);
}

TEST_F(HistoryURLProviderTest, IntranetURLsWithPaths) {
  struct TestCase {
    const char* input;
    int relevance;
  } test_cases[] = {
    { "fooey", 0 },
    { "fooey/", 1200 },     // 1200 for URL would still navigate by default.
    { "fooey/a", 1200 },    // 1200 for UNKNOWN would not.
    { "fooey/a b", 1200 },  // Also UNKNOWN.
    { "gooey", 1410 },
    { "gooey/", 1410 },
    { "gooey/a", 1400 },
    { "gooey/a b", 1400 },
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(test_cases); ++i) {
    SCOPED_TRACE(test_cases[i].input);
    if (test_cases[i].relevance == 0) {
      RunTest(ASCIIToUTF16(test_cases[i].input), string16(), false, NULL, 0);
    } else {
      const UrlAndLegalDefault output[] = {
        { URLFixerUpper::FixupURL(test_cases[i].input, std::string()).spec(),
          true }
      };
      ASSERT_NO_FATAL_FAILURE(RunTest(ASCIIToUTF16(test_cases[i].input),
                              string16(), false, output, arraysize(output)));
      // Actual relevance should be at least what test_cases expects and
      // and no more than 10 more.
      EXPECT_LE(test_cases[i].relevance, matches_[0].relevance);
      EXPECT_LT(matches_[0].relevance, test_cases[i].relevance + 10);
    }
  }
}

TEST_F(HistoryURLProviderTest, IntranetURLsWithRefs) {
  struct TestCase {
    const char* input;
    int relevance;
    AutocompleteInput::Type type;
  } test_cases[] = {
    { "gooey", 1410, AutocompleteInput::UNKNOWN },
    { "gooey/", 1410, AutocompleteInput::URL },
    { "gooey#", 1200, AutocompleteInput::UNKNOWN },
    { "gooey/#", 1200, AutocompleteInput::URL },
    { "gooey#foo", 1200, AutocompleteInput::UNKNOWN },
    { "gooey/#foo", 1200, AutocompleteInput::URL },
    { "gooey# foo", 1200, AutocompleteInput::UNKNOWN },
    { "gooey/# foo", 1200, AutocompleteInput::URL },
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(test_cases); ++i) {
    SCOPED_TRACE(test_cases[i].input);
    const UrlAndLegalDefault output[] = {
      { URLFixerUpper::FixupURL(test_cases[i].input, std::string()).spec(),
        true }
    };
    AutocompleteInput::Type type;
    ASSERT_NO_FATAL_FAILURE(
        RunTest(ASCIIToUTF16(test_cases[i].input),
                string16(), false, output, arraysize(output), &type));
    // Actual relevance should be at least what test_cases expects and
    // and no more than 10 more.
    EXPECT_LE(test_cases[i].relevance, matches_[0].relevance);
    EXPECT_LT(matches_[0].relevance, test_cases[i].relevance + 10);
    // Input type should be what we expect.  This is important because
    // this provider counts on SearchProvider to give queries a relevance
    // score >1200 for UNKNOWN inputs and <1200 for URL inputs.  (That's
    // already tested in search_provider_unittest.cc.)  For this test
    // here to test that the user sees the correct behavior, it needs
    // to check that the input type was identified correctly.
    EXPECT_EQ(test_cases[i].type, type);
  }
}

// Makes sure autocompletion happens for intranet sites that have been
// previoulsy visited.
TEST_F(HistoryURLProviderTest, IntranetURLCompletion) {
  sort_matches_ = true;

  const UrlAndLegalDefault expected1[] = {
    { "http://intra/three", true },
    { "http://intra/two", true }
  };
  ASSERT_NO_FATAL_FAILURE(RunTest(ASCIIToUTF16("intra/t"), string16(), false,
                                  expected1, arraysize(expected1)));
  EXPECT_LE(1410, matches_[0].relevance);
  EXPECT_LT(matches_[0].relevance, 1420);
  EXPECT_EQ(matches_[0].relevance - 1, matches_[1].relevance);

  const UrlAndLegalDefault expected2[] = {
    { "http://moo/b", true },
    { "http://moo/bar", true }
  };
  ASSERT_NO_FATAL_FAILURE(RunTest(ASCIIToUTF16("moo/b"), string16(), false,
                                  expected2, arraysize(expected2)));
  // The url what you typed match should be around 1400, otherwise the
  // search what you typed match is going to be first.
  EXPECT_LE(1400, matches_[0].relevance);
  EXPECT_LT(matches_[0].relevance, 1410);

  const UrlAndLegalDefault expected3[] = {
    { "http://intra/one", true },
    { "http://intra/three", true },
    { "http://intra/two", true }
  };
  RunTest(ASCIIToUTF16("intra"), string16(), false, expected3,
          arraysize(expected3));

  const UrlAndLegalDefault expected4[] = {
    { "http://intra/one", true },
    { "http://intra/three", true },
    { "http://intra/two", true }
  };
  RunTest(ASCIIToUTF16("intra/"), string16(), false, expected4,
          arraysize(expected4));

  const UrlAndLegalDefault expected5[] = {
    { "http://intra/one", true }
  };
  ASSERT_NO_FATAL_FAILURE(RunTest(ASCIIToUTF16("intra/o"), string16(), false,
                                  expected5, arraysize(expected5)));
  EXPECT_LE(1410, matches_[0].relevance);
  EXPECT_LT(matches_[0].relevance, 1420);

  const UrlAndLegalDefault expected6[] = {
    { "http://intra/x", true }
  };
  ASSERT_NO_FATAL_FAILURE(RunTest(ASCIIToUTF16("intra/x"), string16(), false,
                                  expected6, arraysize(expected6)));
  EXPECT_LE(1400, matches_[0].relevance);
  EXPECT_LT(matches_[0].relevance, 1410);

  const UrlAndLegalDefault expected7[] = {
    { "http://typedhost/untypedpath", true }
  };
  ASSERT_NO_FATAL_FAILURE(RunTest(ASCIIToUTF16("typedhost/untypedpath"),
      string16(), false, expected7, arraysize(expected7)));
  EXPECT_LE(1400, matches_[0].relevance);
  EXPECT_LT(matches_[0].relevance, 1410);
}

TEST_F(HistoryURLProviderTest, CrashDueToFixup) {
  // This test passes if we don't crash.  The results don't matter.
  const char* const test_cases[] = {
    "//c",
    "\\@st"
  };
  for (size_t i = 0; i < arraysize(test_cases); ++i) {
    AutocompleteInput input(ASCIIToUTF16(test_cases[i]), string16::npos,
                            string16(), GURL(), AutocompleteInput::INVALID_SPEC,
                            false, false, true, AutocompleteInput::ALL_MATCHES);
    autocomplete_->Start(input, false);
    if (!autocomplete_->done())
      base::MessageLoop::current()->Run();
  }
}

TEST_F(HistoryURLProviderTest, CullSearchResults) {
  // Set up a default search engine.
  TemplateURLData data;
  data.SetKeyword(ASCIIToUTF16("TestEngine"));
  data.SetURL("http://testsearch.com/?q={searchTerms}");
  TemplateURLService* template_url_service =
      TemplateURLServiceFactory::GetForProfile(profile_.get());
  TemplateURL* template_url = new TemplateURL(profile_.get(), data);
  template_url_service->Add(template_url);
  template_url_service->SetDefaultSearchProvider(template_url);
  template_url_service->Load();

  // URLs we will be using, plus the visit counts they will initially get
  // (the redirect set below will also increment the visit counts). We want
  // the results to be in A,B,C order. Note also that our visit counts are
  // all high enough so that domain synthesizing won't get triggered.
  struct TestCase {
    const char* url;
    int count;
  } test_cases[] = {
    {"https://testsearch.com/", 30},
    {"https://testsearch.com/?q=foobar", 20},
    {"http://foobar.com/", 10}
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(test_cases); ++i) {
    history_service_->AddPageWithDetails(GURL(test_cases[i].url),
        UTF8ToUTF16("Title"), test_cases[i].count, test_cases[i].count,
        Time::Now(), false, history::SOURCE_BROWSED);
  }

  // We should not see search URLs when typing a previously used query.
  const UrlAndLegalDefault expected_when_searching_query[] = {
    { test_cases[2].url, false }
  };
  RunTest(ASCIIToUTF16("foobar"), string16(), true,
      expected_when_searching_query, arraysize(expected_when_searching_query));

  // We should not see search URLs when typing the search engine name.
  const UrlAndLegalDefault expected_when_searching_site[] = {
    { test_cases[0].url, false }
  };
  RunTest(ASCIIToUTF16("testsearch"), string16(), true,
      expected_when_searching_site, arraysize(expected_when_searching_site));
}

TEST_F(HistoryURLProviderTest, SuggestExactInput) {
  const size_t npos = std::string::npos;
  struct TestCase {
    // Inputs:
    const char* input;
    bool trim_http;
    // Expected Outputs:
    const char* contents;
    // Offsets of the ACMatchClassifications, terminated by npos.
    size_t offsets[3];
    // The index of the ACMatchClassification that should have the MATCH bit
    // set, npos if no ACMatchClassification should have the MATCH bit set.
    size_t match_classification_index;
  } test_cases[] = {
    { "http://www.somesite.com", false,
      "http://www.somesite.com", {0, npos, npos}, 0 },
    { "www.somesite.com", true,
      "www.somesite.com", {0, npos, npos}, 0 },
    { "www.somesite.com", false,
      "http://www.somesite.com", {0, 7, npos}, 1 },
    { "somesite.com", true,
      "somesite.com", {0, npos, npos}, 0 },
    { "somesite.com", false,
      "http://somesite.com", {0, 7, npos}, 1 },
    { "w", true,
      "w", {0, npos, npos}, 0 },
    { "w", false,
      "http://w", {0, 7, npos}, 1 },
    { "w.com", true,
      "w.com", {0, npos, npos}, 0 },
    { "w.com", false,
      "http://w.com", {0, 7, npos}, 1 },
    { "www.w.com", true,
      "www.w.com", {0, npos, npos}, 0 },
    { "www.w.com", false,
      "http://www.w.com", {0, 7, npos}, 1 },
    { "view-source:www.w.com/", true,
      "view-source:www.w.com", {0, npos, npos}, npos },
    { "view-source:www.w.com/", false,
      "view-source:http://www.w.com", {0, npos, npos}, npos },
    { "view-source:http://www.w.com/", false,
      "view-source:http://www.w.com", {0, npos, npos}, 0 },
    { "   view-source:", true,
      "view-source:", {0, npos, npos}, 0 },
    { "http:////////w.com", false,
      "http://w.com", {0, npos, npos}, npos },
    { "    http:////////www.w.com", false,
      "http://www.w.com", {0, npos, npos}, npos },
    { "http:a///www.w.com", false,
      "http://a///www.w.com", {0, npos, npos}, npos },
    { "mailto://a@b.com", true,
      "mailto://a@b.com", {0, npos, npos}, 0 },
    { "mailto://a@b.com", false,
      "mailto://a@b.com", {0, npos, npos}, 0 },
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(test_cases); ++i) {
    SCOPED_TRACE(testing::Message() << "Index " << i << " input: "
                                    << test_cases[i].input << ", trim_http: "
                                    << test_cases[i].trim_http);

    AutocompleteInput input(ASCIIToUTF16(test_cases[i].input), string16::npos,
                            string16(), GURL("about:blank"),
                            AutocompleteInput::INVALID_SPEC, false, false, true,
                            AutocompleteInput::ALL_MATCHES);
    AutocompleteMatch match = HistoryURLProvider::SuggestExactInput(
        autocomplete_.get(), input, test_cases[i].trim_http);
    EXPECT_EQ(ASCIIToUTF16(test_cases[i].contents), match.contents);
    for (size_t match_index = 0; match_index < match.contents_class.size();
         ++match_index) {
      EXPECT_EQ(test_cases[i].offsets[match_index],
                match.contents_class[match_index].offset);
      EXPECT_EQ(ACMatchClassification::URL |
                (match_index == test_cases[i].match_classification_index ?
                 ACMatchClassification::MATCH : 0),
                match.contents_class[match_index].style);
    }
    EXPECT_EQ(npos, test_cases[i].offsets[match.contents_class.size()]);
  }
}
