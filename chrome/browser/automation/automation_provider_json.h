// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Support utilities for the JSON automation interface used by PyAuto.

#ifndef CHROME_BROWSER_AUTOMATION_AUTOMATION_PROVIDER_JSON_H_
#define CHROME_BROWSER_AUTOMATION_AUTOMATION_PROVIDER_JSON_H_

#include <string>

#include "base/compiler_specific.h"
#include "chrome/common/automation_constants.h"

class AutomationProvider;
class Browser;
class Profile;

namespace base {
class DictionaryValue;
class Value;
}

namespace content {
class RenderViewHost;
class WebContents;
}

namespace extensions {
class Extension;
}

namespace IPC {
class Message;
}

// Helper to ensure we always send a reply message for JSON automation requests.
class AutomationJSONReply {
 public:
  // Creates a new reply object for the IPC message |reply_message| for
  // |provider|. The caller is expected to call SendSuccess() or SendError()
  // before destroying this object.
  AutomationJSONReply(AutomationProvider* provider,
                      IPC::Message* reply_message);

  ~AutomationJSONReply();

  // Send a success reply along with data contained in |value|.
  // An empty message will be sent if |value| is NULL.
  void SendSuccess(const base::Value* value);

  // Send an error reply along with error message |error_message|.
  void SendError(const std::string& error_message);

 private:
  AutomationProvider* provider_;
  IPC::Message* message_;
};

// Gets the browser specified by the given dictionary |args|. |args| should
// contain a key 'windex' which refers to the index of the browser. Returns
// true on success and sets |browser|. Otherwise, |error| will be set.
bool GetBrowserFromJSONArgs(base::DictionaryValue* args,
                            Browser** browser,
                            std::string* error) WARN_UNUSED_RESULT;

// Gets the tab specified by the given dictionary |args|. |args| should
// contain a key 'windex' which refers to the index of the parent browser,
// and a key 'tab_index' which refers to the index of the tab in that browser.
// Returns true on success and sets |tab|. Otherwise, |error| will be set.
bool GetTabFromJSONArgs(base::DictionaryValue* args,
                        content::WebContents** tab,
                        std::string* error) WARN_UNUSED_RESULT;

// Gets the browser and tab specified by the given dictionary |args|. |args|
// should contain a key 'windex' which refers to the index of the browser and
// a key 'tab_index' which refers to the index of the tab in that browser.
// Returns true on success and sets |browser| and |tab|. Otherwise, |error|
// will be set.
bool GetBrowserAndTabFromJSONArgs(base::DictionaryValue* args,
                                  Browser** browser,
                                  content::WebContents** tab,
                                  std::string* error) WARN_UNUSED_RESULT;

// Gets the render view specified by the given dictionary |args|. |args|
// should contain a key 'view_id' which refers to an automation ID for the
// render view. Returns true on success and sets |rvh|. Otherwise, |error|
// will be set.
bool GetRenderViewFromJSONArgs(
    base::DictionaryValue* args,
    Profile* profile,
    content::RenderViewHost** rvh,
    std::string* error) WARN_UNUSED_RESULT;

// Gets the extension specified by the given dictionary |args|. |args|
// should contain the given key which refers to an extension ID. Returns
// true on success and sets |extension|. Otherwise, |error| will be set.
// The retrieved extension may be disabled or crashed.
bool GetExtensionFromJSONArgs(
    base::DictionaryValue* args,
    const std::string& key,
    Profile* profile,
    const extensions::Extension** extension,
    std::string* error) WARN_UNUSED_RESULT;

// Gets the enabled extension specified by the given dictionary |args|. |args|
// should contain the given key which refers to an extension ID. Returns
// true on success and sets |extension|. Otherwise, |error| will be set.
// The retrieved extension will not be disabled or crashed.
bool GetEnabledExtensionFromJSONArgs(
    base::DictionaryValue* args,
    const std::string& key,
    Profile* profile,
    const extensions::Extension** extension,
    std::string* error) WARN_UNUSED_RESULT;

#endif  // CHROME_BROWSER_AUTOMATION_AUTOMATION_PROVIDER_JSON_H_
