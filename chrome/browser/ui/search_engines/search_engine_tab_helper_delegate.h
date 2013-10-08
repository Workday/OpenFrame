// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SEARCH_ENGINES_SEARCH_ENGINE_TAB_HELPER_DELEGATE_H_
#define CHROME_BROWSER_UI_SEARCH_ENGINES_SEARCH_ENGINE_TAB_HELPER_DELEGATE_H_

class Profile;
class TemplateURL;

namespace content {
class WebContents;
}

// Objects implement this interface to get notified about changes in the
// SearchEngineTabHelper and to provide necessary functionality.
class SearchEngineTabHelperDelegate {
 public:
  // Shows a confirmation dialog box for adding a search engine described by
  // |template_url|. Takes ownership of |template_url|.
  virtual void ConfirmAddSearchProvider(TemplateURL* template_url,
                                        Profile* profile) = 0;

 protected:
  virtual ~SearchEngineTabHelperDelegate();
};

#endif  // CHROME_BROWSER_UI_SEARCH_ENGINES_SEARCH_ENGINE_TAB_HELPER_DELEGATE_H_
