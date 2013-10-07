// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_OMNIBOX_OMNIBOX_API_TESTBASE_H_
#define CHROME_BROWSER_EXTENSIONS_API_OMNIBOX_OMNIBOX_API_TESTBASE_H_

#include "base/format_macros.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_result.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/omnibox/location_bar.h"
#include "chrome/browser/ui/omnibox/omnibox_popup_model.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "content/public/test/test_utils.h"


class AutocompleteController;

class OmniboxApiTest : public ExtensionApiTest {
 protected:
  LocationBar* GetLocationBar(Browser* browser) const {
    return browser->window()->GetLocationBar();
  }

  AutocompleteController* GetAutocompleteController(Browser* browser) const {
    return GetLocationBar(browser)->GetLocationEntry()->model()->popup_model()->
        autocomplete_controller();
  }

  // TODO(phajdan.jr): Get rid of this wait-in-a-loop pattern.
  void WaitForAutocompleteDone(AutocompleteController* controller) {
    while (!controller->done()) {
      content::WindowedNotificationObserver ready_observer(
          chrome::NOTIFICATION_AUTOCOMPLETE_CONTROLLER_RESULT_READY,
          content::Source<AutocompleteController>(controller));
      ready_observer.Wait();
    }
  }

  static string16 AutocompleteResultAsString(const AutocompleteResult& result) {
    std::string output(base::StringPrintf("{%" PRIuS "} ", result.size()));
    for (size_t i = 0; i < result.size(); ++i) {
      AutocompleteMatch match = result.match_at(i);
      std::string provider_name = match.provider->GetName();
      output.append(base::StringPrintf("[\"%s\" by \"%s\"] ",
                                       UTF16ToUTF8(match.contents).c_str(),
                                       provider_name.c_str()));
    }
    return UTF8ToUTF16(output);
  }
};

#endif  // CHROME_BROWSER_EXTENSIONS_API_OMNIBOX_OMNIBOX_API_TESTBASE_H_
