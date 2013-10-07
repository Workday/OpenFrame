// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_browser_field_trials_mobile.h"

#include <string>

#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "base/prefs/pref_service.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"

namespace chrome {

namespace {

// Base function used by all data reduction proxy field trials. A trial is
// only conducted for installs that are in the "Enabled" group. They are always
// enabled on DEV and BETA channels, and for STABLE, a percentage will be
// controlled from the server. Until the percentage is learned from the server,
// a client-side configuration is used.
void DataCompressionProxyBaseFieldTrial(
    const char* trial_name,
    const base::FieldTrial::Probability enabled_group_probability,
    const base::FieldTrial::Probability total_probability) {
  const char kEnabled[] = "Enabled";
  const char kDisabled[] = "Disabled";

  // Find out if this is a stable channel.
  const bool kIsStableChannel =
      chrome::VersionInfo::GetChannel() == chrome::VersionInfo::CHANNEL_STABLE;

  // Experiment enabled until Jan 1, 2015. By default, disabled.
  scoped_refptr<base::FieldTrial> trial(
      base::FieldTrialList::FactoryGetFieldTrial(
          trial_name,
          total_probability,
          kDisabled, 2015, 1, 1, base::FieldTrial::ONE_TIME_RANDOMIZED, NULL));

  // Non-stable channels will run with probability 1.
  const int kEnabledGroup = trial->AppendGroup(
      kEnabled,
      kIsStableChannel ? enabled_group_probability : total_probability);

  const int v = trial->group();
  VLOG(1) << trial_name <<  " enabled group id: " << kEnabledGroup
          << ". Selected group id: " << v;
}

void DataCompressionProxyFieldTrials() {
  // Governs the rollout of the compression proxy for Chrome on mobile
  // platforms. Always enabled in DEV and BETA channels. For STABLE, the
  // percentage will be controlled from the server, and is configured to be
  // 10% = 100 / 1000 until overridden by the server.
  DataCompressionProxyBaseFieldTrial(
      "DataCompressionProxyRollout", 100, 1000);

  if (base::FieldTrialList::FindFullName(
      "DataCompressionProxyRollout") == "Enabled") {

    // Governs the rollout of the _promo_ for the compression proxy
    // independently from the rollout of compression proxy. The enabled
    // percentage is configured to be 100% = 1000 / 1000 until overridden by the
    // server. When this trial is "Enabled," users get a promo, whereas
    // otherwise, compression is enabled without a promo.
    DataCompressionProxyBaseFieldTrial(
        "DataCompressionProxyPromoVisibility", 1000, 1000);
  }
}

void NewTabButtonInToolbarFieldTrial(const CommandLine& parsed_command_line) {
  // Do not enable this field trials for tablet devices.
  if (parsed_command_line.HasSwitch(switches::kTabletUI))
    return;

  const char kPhoneNewTabToolbarButtonFieldTrialName[] =
      "PhoneNewTabToolbarButton";
  const base::FieldTrial::Probability kPhoneNewTabToolbarButtonDivisor = 100;

  // 50/100 = 50% for Non-Stable users.
  //  0/100 = 0%  for Stable users.
  const base::FieldTrial::Probability kPhoneNewTabToolbarButtonNonStable = 50;
  const base::FieldTrial::Probability kPhoneNewTabToolbarButtonStable = 0;
  const char kEnabled[] = "Enabled";
  const char kDisabled[] = "Disabled";

  // Find out if this is a stable channel.
  const bool kIsStableChannel =
      chrome::VersionInfo::GetChannel() == chrome::VersionInfo::CHANNEL_STABLE;

  // Experiment enabled until Jan 1, 2015. By default, disabled.
  scoped_refptr<base::FieldTrial> trial(
      base::FieldTrialList::FactoryGetFieldTrial(
          kPhoneNewTabToolbarButtonFieldTrialName,
          kPhoneNewTabToolbarButtonDivisor,
          kDisabled, 2015, 1, 1, base::FieldTrial::ONE_TIME_RANDOMIZED, NULL));

  const int kEnabledGroup = trial->AppendGroup(
      kEnabled,
      kIsStableChannel ?
          kPhoneNewTabToolbarButtonStable : kPhoneNewTabToolbarButtonNonStable);

  const int v = trial->group();
  VLOG(1) << "Phone NewTab toolbar button enabled group id: " << kEnabledGroup
          << ". Selected group id: " << v;
}

}  // namespace

void SetupMobileFieldTrials(const CommandLine& parsed_command_line,
                            const base::Time& install_time,
                            PrefService* local_state) {
  DataCompressionProxyFieldTrials();

#if defined(OS_ANDROID)
  NewTabButtonInToolbarFieldTrial(parsed_command_line);
#endif
}

}  // namespace chrome
