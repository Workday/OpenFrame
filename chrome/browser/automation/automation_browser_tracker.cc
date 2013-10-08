// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/automation/automation_browser_tracker.h"

#include "chrome/browser/chrome_notification_types.h"
#include "content/public/browser/notification_source.h"

AutomationBrowserTracker::AutomationBrowserTracker(IPC::Sender* automation)
    : AutomationResourceTracker<Browser*>(automation) {
}

AutomationBrowserTracker::~AutomationBrowserTracker() {}

void AutomationBrowserTracker::AddObserver(Browser* resource) {
  registrar_.Add(this, chrome::NOTIFICATION_BROWSER_CLOSED,
                 content::Source<Browser>(resource));
}

void AutomationBrowserTracker::RemoveObserver(Browser* resource) {
  registrar_.Remove(this, chrome::NOTIFICATION_BROWSER_CLOSED,
                    content::Source<Browser>(resource));
}
