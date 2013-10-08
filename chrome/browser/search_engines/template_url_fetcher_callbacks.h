// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_FETCHER_CALLBACKS_H_
#define CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_FETCHER_CALLBACKS_H_

class Profile;
class TemplateURL;

// Callbacks for the TemplateURLFetcher.
class TemplateURLFetcherCallbacks {
 public:
  TemplateURLFetcherCallbacks() {}
  virtual ~TemplateURLFetcherCallbacks() {}

  // Performs the confirmation step for adding a search engine described by
  // |template_url|. Takes ownership of |template_url|.
  virtual void ConfirmAddSearchProvider(TemplateURL* template_url,
                                        Profile* profile) = 0;
};

#endif  // CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_FETCHER_CALLBACKS_H_
