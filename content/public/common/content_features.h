// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines all the public base::FeatureList features for the content
// module.

#ifndef CONTENT_PUBLIC_COMMON_CONTENT_FEATURES_H_
#define CONTENT_PUBLIC_COMMON_CONTENT_FEATURES_H_

#include "build/build_config.h"
#include "base/feature_list.h"
#include "content/common/content_export.h"

namespace content {

#if defined(OS_ANDROID)
// FeatureList definition for the Seccomp field trial.
CONTENT_EXPORT extern const base::Feature kSeccompSandboxAndroidFeature;
#endif  // defined(OS_ANDROID)

// DON'T ADD RANDOM STUFF HERE. Put it in the main section above in
// alphabetical order, or in one of the ifdefs (also in order in each section).

}  // namespace content

#endif  // CONTENT_PUBLIC_COMMON_CONTENT_FEATURES_H_
