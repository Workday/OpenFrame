// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/feature_list.h"

#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace base {

namespace {

// Pointer to the FeatureList instance singleton that was set via
// FeatureList::SetInstance(). Does not use base/memory/singleton.h in order to
// have more control over initialization timing. Leaky.
FeatureList* g_instance = nullptr;

// Some characters are not allowed to appear in feature names or the associated
// field trial names, as they are used as special characters for command-line
// serialization. This function checks that the strings are ASCII (since they
// are used in command-line API functions that require ASCII) and whether there
// are any reserved characters present, returning true if the string is valid.
// Only called in DCHECKs.
bool IsValidFeatureOrFieldTrialName(const std::string& name) {
  return IsStringASCII(name) && name.find_first_of(",<") == std::string::npos;
}

}  // namespace

FeatureList::FeatureList() : initialized_(false) {}

FeatureList::~FeatureList() {}

void FeatureList::InitializeFromCommandLine(
    const std::string& enable_features,
    const std::string& disable_features) {
  DCHECK(!initialized_);

  // Process disabled features first, so that disabled ones take precedence over
  // enabled ones (since RegisterOverride() uses insert()).
  RegisterOverridesFromCommandLine(disable_features, OVERRIDE_DISABLE_FEATURE);
  RegisterOverridesFromCommandLine(enable_features, OVERRIDE_ENABLE_FEATURE);
}

bool FeatureList::IsFeatureOverriddenFromCommandLine(
    const std::string& feature_name,
    OverrideState state) const {
  auto it = overrides_.find(feature_name);
  return it != overrides_.end() && it->second.overridden_state == state &&
         !it->second.overridden_by_field_trial;
}

void FeatureList::AssociateReportingFieldTrial(
    const std::string& feature_name,
    OverrideState for_overridden_state,
    FieldTrial* field_trial) {
  DCHECK(
      IsFeatureOverriddenFromCommandLine(feature_name, for_overridden_state));

  // Only one associated field trial is supported per feature. This is generally
  // enforced server-side.
  OverrideEntry* entry = &overrides_.find(feature_name)->second;
  if (entry->field_trial) {
    NOTREACHED() << "Feature " << feature_name
                 << " already has trial: " << entry->field_trial->trial_name()
                 << ", associating trial: " << field_trial->trial_name();
    return;
  }

  entry->field_trial = field_trial;
}

void FeatureList::RegisterFieldTrialOverride(const std::string& feature_name,
                                             OverrideState override_state,
                                             FieldTrial* field_trial) {
  DCHECK(field_trial);
  DCHECK(!ContainsKey(overrides_, feature_name) ||
         !overrides_.find(feature_name)->second.field_trial)
      << "Feature " << feature_name
      << " has conflicting field trial overrides: "
      << overrides_.find(feature_name)->second.field_trial->trial_name()
      << " / " << field_trial->trial_name();

  RegisterOverride(feature_name, override_state, field_trial);
}

void FeatureList::GetFeatureOverrides(std::string* enable_overrides,
                                      std::string* disable_overrides) {
  DCHECK(initialized_);

  enable_overrides->clear();
  disable_overrides->clear();

  for (const auto& entry : overrides_) {
    std::string* target_list = nullptr;
    switch (entry.second.overridden_state) {
      case OVERRIDE_ENABLE_FEATURE:
        target_list = enable_overrides;
        break;
      case OVERRIDE_DISABLE_FEATURE:
        target_list = disable_overrides;
        break;
    }

    if (!target_list->empty())
      target_list->push_back(',');
    target_list->append(entry.first);
    if (entry.second.field_trial) {
      target_list->push_back('<');
      target_list->append(entry.second.field_trial->trial_name());
    }
  }
}

// static
bool FeatureList::IsEnabled(const Feature& feature) {
  return GetInstance()->IsFeatureEnabled(feature);
}

// static
std::vector<std::string> FeatureList::SplitFeatureListString(
    const std::string& input) {
  return SplitString(input, ",", TRIM_WHITESPACE, SPLIT_WANT_NONEMPTY);
}

// static
void FeatureList::InitializeInstance() {
  if (g_instance)
    return;
  SetInstance(make_scoped_ptr(new FeatureList));
}

// static
FeatureList* FeatureList::GetInstance() {
  return g_instance;
}

// static
void FeatureList::SetInstance(scoped_ptr<FeatureList> instance) {
  DCHECK(!g_instance);
  instance->FinalizeInitialization();

  // Note: Intentional leak of global singleton.
  g_instance = instance.release();
}

// static
void FeatureList::ClearInstanceForTesting() {
  delete g_instance;
  g_instance = nullptr;
}

void FeatureList::FinalizeInitialization() {
  DCHECK(!initialized_);
  initialized_ = true;
}

bool FeatureList::IsFeatureEnabled(const Feature& feature) {
  DCHECK(initialized_);
  DCHECK(IsValidFeatureOrFieldTrialName(feature.name)) << feature.name;
  DCHECK(CheckFeatureIdentity(feature)) << feature.name;

  auto it = overrides_.find(feature.name);
  if (it != overrides_.end()) {
    const OverrideEntry& entry = it->second;

    // Activate the corresponding field trial, if necessary.
    if (entry.field_trial)
      entry.field_trial->group();

    // TODO(asvitkine) Expand this section as more support is added.
    return entry.overridden_state == OVERRIDE_ENABLE_FEATURE;
  }
  // Otherwise, return the default state.
  return feature.default_state == FEATURE_ENABLED_BY_DEFAULT;
}

void FeatureList::RegisterOverridesFromCommandLine(
    const std::string& feature_list,
    OverrideState overridden_state) {
  for (const auto& value : SplitFeatureListString(feature_list)) {
    StringPiece feature_name(value);
    base::FieldTrial* trial = nullptr;

    // The entry may be of the form FeatureName<FieldTrialName - in which case,
    // this splits off the field trial name and associates it with the override.
    std::string::size_type pos = feature_name.find('<');
    if (pos != std::string::npos) {
      feature_name.set(value.data(), pos);
      trial = base::FieldTrialList::Find(value.substr(pos + 1));
    }

    RegisterOverride(feature_name, overridden_state, trial);
  }
}

void FeatureList::RegisterOverride(StringPiece feature_name,
                                   OverrideState overridden_state,
                                   FieldTrial* field_trial) {
  DCHECK(!initialized_);
  if (field_trial) {
    DCHECK(IsValidFeatureOrFieldTrialName(field_trial->trial_name()))
        << field_trial->trial_name();
  }

  // Note: The semantics of insert() is that it does not overwrite the entry if
  // one already exists for the key. Thus, only the first override for a given
  // feature name takes effect.
  overrides_.insert(std::make_pair(
      feature_name.as_string(), OverrideEntry(overridden_state, field_trial)));
}

bool FeatureList::CheckFeatureIdentity(const Feature& feature) {
  AutoLock auto_lock(feature_identity_tracker_lock_);

  auto it = feature_identity_tracker_.find(feature.name);
  if (it == feature_identity_tracker_.end()) {
    // If it's not tracked yet, register it.
    feature_identity_tracker_[feature.name] = &feature;
    return true;
  }
  // Compare address of |feature| to the existing tracked entry.
  return it->second == &feature;
}

FeatureList::OverrideEntry::OverrideEntry(OverrideState overridden_state,
                                          FieldTrial* field_trial)
    : overridden_state(overridden_state),
      field_trial(field_trial),
      overridden_by_field_trial(field_trial != nullptr) {}

}  // namespace base
