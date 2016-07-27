// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_PREFS_IOS_CHROME_PREF_SERVICE_FACTORY_H_
#define IOS_CHROME_BROWSER_PREFS_IOS_CHROME_PREF_SERVICE_FACTORY_H_

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"

class PrefRegistry;
class PrefService;
class PrefStore;
class TrackedPreferenceValidationDelegate;

namespace base {
class FilePath;
class SequencedTaskRunner;
}

namespace ios {
class ChromeBrowserState;
}

namespace syncable_prefs {
class PrefServiceSyncable;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

// Factory methods that create and initialize a new instance of a PrefService
// for Chrome on iOS with the applicable PrefStores. The |pref_filename| points
// to the user preference file. This is the usual way to create a new
// PrefService. |pref_registry| keeps the list of registered prefs and their
// default values.
scoped_ptr<PrefService> CreateLocalState(
    const base::FilePath& pref_filename,
    base::SequencedTaskRunner* pref_io_task_runner,
    const scoped_refptr<PrefRegistry>& pref_registry);

scoped_ptr<syncable_prefs::PrefServiceSyncable> CreateBrowserStatePrefs(
    const base::FilePath& browser_state_path,
    base::SequencedTaskRunner* pref_io_task_runner,
    TrackedPreferenceValidationDelegate* validation_delegate,
    const scoped_refptr<user_prefs::PrefRegistrySyncable>& pref_registry);

// Creates an incognito copy of |pref_service| that shares most prefs but uses
// a fresh non-persistent overlay for the user pref store.
scoped_ptr<syncable_prefs::PrefServiceSyncable>
CreateIncognitoBrowserStatePrefs(
    syncable_prefs::PrefServiceSyncable* main_pref_store);

#endif  // IOS_CHROME_BROWSER_PREFS_IOS_CHROME_PREF_SERVICE_FACTORY_H_
