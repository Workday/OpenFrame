// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_TABS_TABS_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_TABS_TABS_API_H_

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "chrome/browser/extensions/api/execute_code_function.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/common/extensions/api/tabs.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "extensions/common/extension_resource.h"
#include "extensions/common/user_script.h"
#include "url/gurl.h"

class BackingStore;
class GURL;
class SkBitmap;
class TabStripModel;

namespace base {
class DictionaryValue;
}

namespace content {
class WebContents;
}

namespace ui {
class ListSelectionModel;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace extensions {

// Windows
class WindowsGetFunction : public SyncExtensionFunction {
  virtual ~WindowsGetFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("windows.get", WINDOWS_GET)
};
class WindowsGetCurrentFunction : public SyncExtensionFunction {
  virtual ~WindowsGetCurrentFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("windows.getCurrent", WINDOWS_GETCURRENT)
};
class WindowsGetLastFocusedFunction : public SyncExtensionFunction {
  virtual ~WindowsGetLastFocusedFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("windows.getLastFocused", WINDOWS_GETLASTFOCUSED)
};
class WindowsGetAllFunction : public SyncExtensionFunction {
  virtual ~WindowsGetAllFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("windows.getAll", WINDOWS_GETALL)
};
class WindowsCreateFunction : public SyncExtensionFunction {
  virtual ~WindowsCreateFunction() {}
  virtual bool RunImpl() OVERRIDE;
  // Returns whether the window should be created in incognito mode.
  // |urls| is the list of urls to open. If we are creating an incognito window,
  // the function will remove these urls which may not be opened in incognito
  // mode.  If window creation leads the browser into an erroneous state,
  // |is_error| is set to true (also, error_ member variable is assigned
  // the proper error message).
  bool ShouldOpenIncognitoWindow(const base::DictionaryValue* args,
                                 std::vector<GURL>* urls,
                                 bool* is_error);
  DECLARE_EXTENSION_FUNCTION("windows.create", WINDOWS_CREATE)
};
class WindowsUpdateFunction : public SyncExtensionFunction {
  virtual ~WindowsUpdateFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("windows.update", WINDOWS_UPDATE)
};
class WindowsRemoveFunction : public SyncExtensionFunction {
  virtual ~WindowsRemoveFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("windows.remove", WINDOWS_REMOVE)
};

// Tabs
class TabsGetFunction : public SyncExtensionFunction {
  virtual ~TabsGetFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("tabs.get", TABS_GET)
};
class TabsGetCurrentFunction : public SyncExtensionFunction {
  virtual ~TabsGetCurrentFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("tabs.getCurrent", TABS_GETCURRENT)
};
class TabsGetSelectedFunction : public SyncExtensionFunction {
  virtual ~TabsGetSelectedFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("tabs.getSelected", TABS_GETSELECTED)
};
class TabsGetAllInWindowFunction : public SyncExtensionFunction {
  virtual ~TabsGetAllInWindowFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("tabs.getAllInWindow", TABS_GETALLINWINDOW)
};
class TabsQueryFunction : public SyncExtensionFunction {
  virtual ~TabsQueryFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("tabs.query", TABS_QUERY)
};
class TabsCreateFunction : public SyncExtensionFunction {
  virtual ~TabsCreateFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("tabs.create", TABS_CREATE)
};
class TabsDuplicateFunction : public SyncExtensionFunction {
  virtual ~TabsDuplicateFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("tabs.duplicate", TABS_DUPLICATE)
};
class TabsHighlightFunction : public SyncExtensionFunction {
  virtual ~TabsHighlightFunction() {}
  virtual bool RunImpl() OVERRIDE;
  bool HighlightTab(TabStripModel* tabstrip,
                    ui::ListSelectionModel* selection,
                    int *active_index,
                    int index);
  DECLARE_EXTENSION_FUNCTION("tabs.highlight", TABS_HIGHLIGHT)
};
class TabsUpdateFunction : public AsyncExtensionFunction {
 public:
  TabsUpdateFunction();

 protected:
  virtual ~TabsUpdateFunction() {}
  virtual bool UpdateURL(const std::string& url,
                         int tab_id,
                         bool* is_async);
  virtual void PopulateResult();

  content::WebContents* web_contents_;

 private:
  virtual bool RunImpl() OVERRIDE;
  void OnExecuteCodeFinished(const std::string& error,
                             int32 on_page_id,
                             const GURL& on_url,
                             const base::ListValue& script_result);

  DECLARE_EXTENSION_FUNCTION("tabs.update", TABS_UPDATE)
};
class TabsMoveFunction : public SyncExtensionFunction {
  virtual ~TabsMoveFunction() {}
  virtual bool RunImpl() OVERRIDE;
  bool MoveTab(int tab_id,
               int* new_index,
               int iteration,
               base::ListValue* tab_values,
               int* window_id);
  DECLARE_EXTENSION_FUNCTION("tabs.move", TABS_MOVE)
};
class TabsReloadFunction : public SyncExtensionFunction {
  virtual ~TabsReloadFunction() {}
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION("tabs.reload", TABS_RELOAD)
};
class TabsRemoveFunction : public SyncExtensionFunction {
  virtual ~TabsRemoveFunction() {}
  virtual bool RunImpl() OVERRIDE;
  bool RemoveTab(int tab_id);
  DECLARE_EXTENSION_FUNCTION("tabs.remove", TABS_REMOVE)
};
class TabsDetectLanguageFunction : public AsyncExtensionFunction,
                                  public content::NotificationObserver {
 private:
  virtual ~TabsDetectLanguageFunction() {}
  virtual bool RunImpl() OVERRIDE;

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;
  void GotLanguage(const std::string& language);
  content::NotificationRegistrar registrar_;
  DECLARE_EXTENSION_FUNCTION("tabs.detectLanguage", TABS_DETECTLANGUAGE)
};
class TabsCaptureVisibleTabFunction : public AsyncExtensionFunction {
 public:
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

 protected:
  typedef api::tabs::CaptureVisibleTab::Params::Options::Format ImageFormat;

  // The default quality setting used when encoding jpegs.
  static const int kDefaultQuality;

  virtual ~TabsCaptureVisibleTabFunction() {}
  virtual bool RunImpl() OVERRIDE;
  virtual bool GetTabToCapture(content::WebContents** web_contents);
  void SendResultFromBitmap(const SkBitmap& screen_capture);

 private:
  // Callback for the RWH::CopyFromBackingStore call.
  void CopyFromBackingStoreComplete(bool succeeded,
                                    const SkBitmap& bitmap);

  // Callback for the RWH::GetSnapshotFromRenderer call. This path is used if
  // CopyFromBackingStore fails for some reason.
  void GetSnapshotFromRendererComplete(bool succeeded,
                                       const SkBitmap& bitmap);
  void SendInternalError();

  // The format (JPEG vs PNG) of the resulting image.  Set in RunImpl().
  ImageFormat image_format_;

  // Quality setting to use when encoding jpegs.  Set in RunImpl().
  int image_quality_;

  DECLARE_EXTENSION_FUNCTION("tabs.captureVisibleTab", TABS_CAPTUREVISIBLETAB)
};

// Implement API call tabs.executeScript and tabs.insertCSS.
class ExecuteCodeInTabFunction : public ExecuteCodeFunction {
 public:
  ExecuteCodeInTabFunction();

 protected:
  virtual ~ExecuteCodeInTabFunction();

  // ExtensionFunction:
  virtual bool HasPermission() OVERRIDE;

  // Initialize the |execute_tab_id_| and |details_| if they haven't already
  // been. Returns whether initialization was successful.
  virtual bool Init() OVERRIDE;
  virtual bool CanExecuteScriptOnPage() OVERRIDE;
  virtual ScriptExecutor* GetScriptExecutor() OVERRIDE;
  virtual bool IsWebView() const OVERRIDE;

 private:
  // Id of tab which executes code.
  int execute_tab_id_;
};

class TabsExecuteScriptFunction : public ExecuteCodeInTabFunction {
 protected:
  virtual bool ShouldInsertCSS() const OVERRIDE;

 private:
  virtual ~TabsExecuteScriptFunction() {}

  virtual void OnExecuteCodeFinished(
      const std::string& error,
      int32 on_page_id,
      const GURL& on_url,
      const base::ListValue& script_result) OVERRIDE;

  DECLARE_EXTENSION_FUNCTION("tabs.executeScript", TABS_EXECUTESCRIPT)
};

class TabsInsertCSSFunction : public ExecuteCodeInTabFunction {
 private:
  virtual ~TabsInsertCSSFunction() {}

  virtual bool ShouldInsertCSS() const OVERRIDE;

  DECLARE_EXTENSION_FUNCTION("tabs.insertCSS", TABS_INSERTCSS)
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_TABS_TABS_API_H_
