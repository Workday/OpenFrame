// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/automation/automation_window_tracker.h"

#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/native_window_notification_source.h"

AutomationWindowTracker::AutomationWindowTracker(IPC::Sender* automation)
    : AutomationResourceTracker<gfx::NativeWindow>(automation) {
}

AutomationWindowTracker::~AutomationWindowTracker() {
}

void AutomationWindowTracker::AddObserver(gfx::NativeWindow resource) {
  registrar_.Add(this, chrome::NOTIFICATION_WINDOW_CLOSED,
                 content::Source<gfx::NativeWindow>(resource));
}

void AutomationWindowTracker::RemoveObserver(gfx::NativeWindow resource) {
  registrar_.Remove(this, chrome::NOTIFICATION_WINDOW_CLOSED,
                    content::Source<gfx::NativeWindow>(resource));
}
