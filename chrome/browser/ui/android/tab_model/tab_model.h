// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ANDROID_TAB_MODEL_TAB_MODEL_H_
#define CHROME_BROWSER_UI_ANDROID_TAB_MODEL_TAB_MODEL_H_

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/sessions/session_id.h"
#include "chrome/browser/sync/glue/synced_tab_delegate.h"
#include "chrome/browser/ui/toolbar/toolbar_model.h"
#include "chrome/browser/ui/toolbar/toolbar_model_delegate.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

namespace browser_sync {
class SyncedWindowDelegate;
class SyncedWindowDelegateAndroid;
class SyncedTabDelegate;
}

namespace content {
class WebContents;
}

class Profile;

// Abstract representation of a Tab Model for Android.  Since Android does
// not use Browser/BrowserList, this is required to allow Chrome to interact
// with Android's Tabs and Tab Model.
class TabModel : public content::NotificationObserver,
                 public ToolbarModelDelegate {
 public:
  explicit TabModel(Profile* profile);
  // Deprecated: This constructor is deprecated and should be removed once
  // downstream Android uses new constructor. See crbug.com/159704.
  TabModel();
  virtual ~TabModel();

  // Implementation of ToolbarDelegate
  virtual content::WebContents* GetActiveWebContents() const OVERRIDE;

  virtual Profile* GetProfile() const;
  virtual bool IsOffTheRecord() const;
  virtual browser_sync::SyncedWindowDelegate* GetSyncedWindowDelegate() const;
  virtual SessionID::id_type GetSessionId() const;

  virtual int GetTabCount() const = 0;
  virtual int GetActiveIndex() const = 0;
  virtual content::WebContents* GetWebContentsAt(int index) const = 0;
  virtual browser_sync::SyncedTabDelegate* GetTabAt(int index) const = 0;

  // Used for restoring tabs from synced foreign sessions.
  virtual void CreateTab(content::WebContents* web_contents) = 0;

  // Used for creating a new tab with a given URL.
  virtual content::WebContents* CreateTabForTesting(const GURL& url) = 0;

  // Return true if we are currently restoring sessions asynchronously.
  virtual bool IsSessionRestoreInProgress() const = 0;

  virtual void OpenClearBrowsingData() const = 0;

  ToolbarModel::SecurityLevel GetSecurityLevelForCurrentTab();

  // Returns search terms extracted from the current url if possible.
  string16 GetSearchTermsForCurrentTab();

  // Returns the parameter that is used to trigger query extraction.
  std::string GetQueryExtractionParam();

  // Calls through to the ToolbarModel's GetCorpusNameForMobile -- see
  // comments in toolbar_model.h.
  string16 GetCorpusNameForCurrentTab();

 protected:
  // Instructs the TabModel to broadcast a notification that all tabs are now
  // loaded from storage.
  void BroadcastSessionRestoreComplete();

  ToolbarModel* GetToolbarModel();

 private:
  // Determines how TabModel will interact with the profile.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // The profile associated with this TabModel.
  Profile* profile_;

  // Describes if this TabModel contains an off-the-record profile.
  bool is_off_the_record_;

  // The SyncedWindowDelegate associated with this TabModel.
  scoped_ptr<browser_sync::SyncedWindowDelegateAndroid> synced_window_delegate_;

  scoped_ptr<ToolbarModel> toolbar_model_;

  // Unique identifier of this TabModel for session restore. This id is only
  // unique within the current session, and is not guaranteed to be unique
  // across sessions.
  SessionID session_id_;

  // The Registrar used to register TabModel for notifications.
  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(TabModel);
};

#endif  // CHROME_BROWSER_UI_ANDROID_TAB_MODEL_TAB_MODEL_H_
