// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_browser_field_trials.h"

#include <string>

#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "chrome/browser/metrics/gzipped_protobufs_field_trial.h"
#include "chrome/browser/omnibox/omnibox_field_trial.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/metrics/variations/uniformity_field_trials.h"
#include "chrome/common/pref_names.h"

#if defined(OS_ANDROID) || defined(OS_IOS)
#include "chrome/browser/chrome_browser_field_trials_mobile.h"
#else
#include "chrome/browser/chrome_browser_field_trials_desktop.h"
#endif

ChromeBrowserFieldTrials::ChromeBrowserFieldTrials(
    const CommandLine& parsed_command_line)
    : parsed_command_line_(parsed_command_line) {
}

ChromeBrowserFieldTrials::~ChromeBrowserFieldTrials() {
}

void ChromeBrowserFieldTrials::SetupFieldTrials(PrefService* local_state) {
  const base::Time install_time = base::Time::FromTimeT(
      local_state->GetInt64(prefs::kInstallDate));
  DCHECK(!install_time.is_null());

  // Field trials that are shared by all platforms.
  chrome_variations::SetupUniformityFieldTrials(install_time);
  metrics::CreateGzippedProtobufsFieldTrial();
  InstantiateDynamicTrials();

#if defined(OS_ANDROID) || defined(OS_IOS)
  chrome::SetupMobileFieldTrials(
      parsed_command_line_, install_time, local_state);
#else
  chrome::SetupDesktopFieldTrials(
      parsed_command_line_, install_time, local_state);
#endif
}

void ChromeBrowserFieldTrials::InstantiateDynamicTrials() {
  // Call |FindValue()| on the trials below, which may come from the server, to
  // ensure they get marked as "used" for the purposes of data reporting.
  base::FieldTrialList::FindValue("UMA-Dynamic-Binary-Uniformity-Trial");
  base::FieldTrialList::FindValue("UMA-Dynamic-Uniformity-Trial");
  base::FieldTrialList::FindValue("InstantDummy");
  base::FieldTrialList::FindValue("InstantChannel");
  base::FieldTrialList::FindValue("Test0PercentDefault");
  // MouseEventPreconnect trial is used from renderer process.
  // Mark here so it will be sync-ed.
  base::FieldTrialList::FindValue("MouseEventPreconnect");
  // Activate the autocomplete dynamic field trials.
  OmniboxFieldTrial::ActivateDynamicTrials();
}
