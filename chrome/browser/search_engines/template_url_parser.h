// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_PARSER_H_
#define CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_PARSER_H_

#include <string>

#include "base/basictypes.h"

class Profile;
class TemplateURL;

// TemplateURLParser, as the name implies, handling reading of TemplateURLs
// from OpenSearch description documents.
class TemplateURLParser {
 public:
  class ParameterFilter {
   public:
    // Invoked for each parameter of the template URL while parsing.  If this
    // methods returns false, the parameter is not included.
    virtual bool KeepParameter(const std::string& key,
                               const std::string& value) = 0;

   protected:
    virtual ~ParameterFilter() {}
  };

  // Decodes the chunk of data representing a TemplateURL, creates the
  // TemplateURL, and returns it.  The caller owns the returned object.
  // |profile| should only be non-NULL if this function is called on the UI
  // thread.  Returns NULL if data does not describe a valid TemplateURL, the
  // URLs referenced do not point to valid http/https resources, or for some
  // other reason we do not support the described TemplateURL.
  // |parameter_filter| can be used if you want to filter some parameters out of
  // the URL.  For example, when importing from another browser, we remove any
  // parameter identifying that browser.  If set to NULL, the URL is not
  // modified.
  static TemplateURL* Parse(Profile* profile,
                            bool show_in_default_list,
                            const char* data,
                            size_t length,
                            ParameterFilter* parameter_filter);

 private:
  // No one should create one of these.
  TemplateURLParser();

  DISALLOW_COPY_AND_ASSIGN(TemplateURLParser);
};

#endif  // CHROME_BROWSER_SEARCH_ENGINES_TEMPLATE_URL_PARSER_H_
