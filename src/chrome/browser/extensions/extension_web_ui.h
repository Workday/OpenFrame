// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_WEB_UI_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_WEB_UI_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/favicon/favicon_service.h"
#include "chrome/common/extensions/manifest_url_handler.h"
#include "content/public/browser/web_ui_controller.h"

class Profile;

namespace content {
class BrowserContext;
class WebContents;
}

namespace extensions {
class BookmarkManagerPrivateEventRouter;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

// This class implements WebUI for extensions and allows extensions to put UI in
// the main tab contents area. For example, each extension can specify an
// "options_page", and that page is displayed in the tab contents area and is
// hosted by this class.
class ExtensionWebUI : public content::WebUIController {
 public:
  static const char kExtensionURLOverrides[];

  ExtensionWebUI(content::WebUI* web_ui, const GURL& url);

  virtual ~ExtensionWebUI();

  virtual extensions::BookmarkManagerPrivateEventRouter*
      bookmark_manager_private_event_router();

  // BrowserURLHandler
  static bool HandleChromeURLOverride(GURL* url,
                                      content::BrowserContext* browser_context);
  static bool HandleChromeURLOverrideReverse(
      GURL* url, content::BrowserContext* browser_context);

  // Register and unregister a dictionary of one or more overrides.
  // Page names are the keys, and chrome-extension: URLs are the values.
  // (e.g. { "newtab": "chrome-extension://<id>/my_new_tab.html" }
  static void RegisterChromeURLOverrides(Profile* profile,
      const extensions::URLOverrides::URLOverrideMap& overrides);
  static void UnregisterChromeURLOverrides(Profile* profile,
      const extensions::URLOverrides::URLOverrideMap& overrides);
  static void UnregisterChromeURLOverride(const std::string& page,
                                          Profile* profile,
                                          const base::Value* override);

  // Called from BrowserPrefs
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // Get the favicon for the extension by getting an icon from the manifest.
  // Note. |callback| is always run asynchronously.
  static void GetFaviconForURL(
      Profile* profile,
      const GURL& page_url,
      const FaviconService::FaviconResultsCallback& callback);

 private:
  // Unregister the specified override, and if it's the currently active one,
  // ensure that something takes its place.
  static void UnregisterAndReplaceOverride(const std::string& page,
                                           Profile* profile,
                                           base::ListValue* list,
                                           const base::Value* override);

  // TODO(aa): This seems out of place. Why is it not with the event routers for
  // the other extension APIs?
  scoped_ptr<extensions::BookmarkManagerPrivateEventRouter>
      bookmark_manager_private_event_router_;

  // The URL this WebUI was created for.
  GURL url_;
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_WEB_UI_H_
