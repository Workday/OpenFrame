// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/features/complex_feature.h"

namespace extensions {

ComplexFeature::ComplexFeature(scoped_ptr<FeatureList> features) {
  DCHECK_GT(features->size(), 0UL);
  features_.swap(*features);
}

ComplexFeature::~ComplexFeature() {
}

Feature::Availability ComplexFeature::IsAvailableToManifest(
    const std::string& extension_id, Manifest::Type type, Location location,
    int manifest_version, Platform platform) const {
  Feature::Availability first_availability =
      features_[0]->IsAvailableToManifest(
          extension_id, type, location, manifest_version, platform);
  if (first_availability.is_available())
    return first_availability;

  for (FeatureList::const_iterator it = features_.begin() + 1;
       it != features_.end(); ++it) {
    Availability availability = (*it)->IsAvailableToManifest(
        extension_id, type, location, manifest_version, platform);
    if (availability.is_available())
      return availability;
  }
  // If none of the SimpleFeatures are available, we return the availability
  // info of the first SimpleFeature that was not available.
  return first_availability;
}

Feature::Availability ComplexFeature::IsAvailableToContext(
    const Extension* extension,
    Context context,
    const GURL& url,
    Platform platform) const {
  Feature::Availability first_availability =
      features_[0]->IsAvailableToContext(extension, context, url, platform);
  if (first_availability.is_available())
    return first_availability;

  for (FeatureList::const_iterator it = features_.begin() + 1;
       it != features_.end(); ++it) {
    Availability availability =
        (*it)->IsAvailableToContext(extension, context, url, platform);
    if (availability.is_available())
      return availability;
  }
  // If none of the SimpleFeatures are available, we return the availability
  // info of the first SimpleFeature that was not available.
  return first_availability;
}

std::set<Feature::Context>* ComplexFeature::GetContexts() {
  // TODO(justinlin): Current use cases for ComplexFeatures are simple (e.g.
  // allow API in dev channel for everyone but stable channel for a whitelist),
  // but if they get more complicated, we need to return some meaningful context
  // set. Either that or remove this method from the Feature interface.
  return features_[0]->GetContexts();
}

bool ComplexFeature::IsInternal() const {
  NOTREACHED();
  return false;
}

std::string ComplexFeature::GetAvailabilityMessage(AvailabilityResult result,
                                                   Manifest::Type type,
                                                   const GURL& url) const {
  if (result == IS_AVAILABLE)
    return std::string();

  // TODO(justinlin): Form some kind of combined availabilities/messages from
  // SimpleFeatures.
  return features_[0]->GetAvailabilityMessage(result, type, url);
}

bool ComplexFeature::IsIdInWhitelist(const std::string& extension_id) const {
  for (FeatureList::const_iterator it = features_.begin();
       it != features_.end(); ++it) {
    if ((*it)->IsIdInWhitelist(extension_id))
      return true;
  }
  return false;
}

}  // namespace extensions
