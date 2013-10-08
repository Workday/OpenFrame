// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_TEST_UTIL_H_
#define CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_TEST_UTIL_H_

#include <string>

#include "base/basictypes.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string16.h"
#include "chrome/browser/search_engines/template_url_service_observer.h"
#include "chrome/test/base/testing_browser_process.h"
#include "content/public/test/test_browser_thread_bundle.h"

class GURL;
class TemplateURLService;
class TestingProfile;
class TestingTemplateURLService;
class TestingProfile;
class WebDataService;

// TemplateURLServiceTestUtilBase contains basic API to ease testing of
// TemplateURLService. User should take care of the infrastructure separately.
class TemplateURLServiceTestUtilBase : public TemplateURLServiceObserver {
 public:
  TemplateURLServiceTestUtilBase();
  virtual ~TemplateURLServiceTestUtilBase();

  void CreateTemplateUrlService();

  // TemplateURLServiceObserver implemementation.
  virtual void OnTemplateURLServiceChanged() OVERRIDE;

  // Gets the observer count.
  int GetObserverCount();

  // Sets the observer count to 0.
  void ResetObserverCount();

  // Makes sure the load was successful and sent the correct notification.
  void VerifyLoad();

  // Makes the model believe it has been loaded (without actually doing the
  // load). Since this avoids setting the built-in keyword version, the next
  // load will do a merge from prepopulated data.
  void ChangeModelToLoadState();

  // Deletes the current model (and doesn't create a new one).
  void ClearModel();

  // Creates a new TemplateURLService.
  void ResetModel(bool verify_load);

  // Returns the search term from the last invocation of
  // TemplateURLService::SetKeywordSearchTermsForURL and clears the search term.
  string16 GetAndClearSearchTerm();

  // Set the google base url.  |base_url| must be valid.
  void SetGoogleBaseURL(const GURL& base_url) const;

  // Set the managed preferences for the default search provider and trigger
  // notification. If |alternate_url| is empty, uses an empty list of alternate
  // URLs, otherwise use a list containing a single entry.
  void SetManagedDefaultSearchPreferences(
      bool enabled,
      const std::string& name,
      const std::string& keyword,
      const std::string& search_url,
      const std::string& suggest_url,
      const std::string& icon_url,
      const std::string& encodings,
      const std::string& alternate_url,
      const std::string& search_terms_replacement_key);

  // Remove all the managed preferences for the default search provider and
  // trigger notification.
  void RemoveManagedDefaultSearchPreferences();

  // Returns the TemplateURLService.
  TemplateURLService* model() const;

  // Returns the TestingProfile.
  virtual TestingProfile* profile() const = 0;

 private:
  int changed_count_;

  DISALLOW_COPY_AND_ASSIGN(TemplateURLServiceTestUtilBase);
};

// TemplateURLServiceTestUtil sets up TestingProfile, TemplateURLService and
// required threads.
class TemplateURLServiceTestUtil : public TemplateURLServiceTestUtilBase {
 public:
  TemplateURLServiceTestUtil();
  virtual ~TemplateURLServiceTestUtil();

  // Sets up the data structures for this class (mirroring gtest standard
  // methods).
  void SetUp();

  // Cleans up data structures for this class  (mirroring gtest standard
  // methods).
  void TearDown();

  // Returns the TestingProfile.
  virtual TestingProfile* profile() const OVERRIDE;

 private:
  // Needed to make the DeleteOnUIThread trait of WebDataService work
  // properly.
  content::TestBrowserThreadBundle thread_bundle_;
  scoped_ptr<TestingProfile> profile_;
  base::ScopedTempDir temp_dir_;

  DISALLOW_COPY_AND_ASSIGN(TemplateURLServiceTestUtil);
};

#endif  // CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_TEST_UTIL_H_
