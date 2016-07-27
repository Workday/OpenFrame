// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/router/media_router_feature.h"

#include "build/build_config.h"
#include "content/public/browser/browser_context.h"

#if defined(ENABLE_EXTENSIONS)
#include "chrome/browser/extensions/api/tab_capture/tab_capture_api.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/feature_switch.h"
#endif

namespace media_router {

bool MediaRouterEnabled(content::BrowserContext* context) {
#if defined(ENABLE_MEDIA_ROUTER)
#if defined(OS_ANDROID)
  return true;
#elif defined(ENABLE_EXTENSIONS)
  extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(context);
  if (!registry) {
    DLOG(ERROR) << "ExtensionRegistry is null. Assume no cast extension "
                   "installed.";
    return extensions::FeatureSwitch::media_router()->IsEnabled();
  }

  const extensions::ExtensionSet& extension_set =
      registry->enabled_extensions();
  if (extension_set.Contains(extensions::kStableChromecastExtensionId) ||
      extension_set.Contains(extensions::kBetaChromecastExtensionId)) {
    return extensions::FeatureSwitch::media_router_with_cast_extension()
        ->IsEnabled();
  }

  return extensions::FeatureSwitch::media_router()->IsEnabled();
#else  // !defined(ENABLE_EXTENSIONS)
  return false;
#endif  // defined(OS_ANDROID)
#else   // !defined(ENABLE_MEDIA_ROUTER)
  return false;
#endif  // defined(ENABLE_MEDIA_ROUTER)
}

}  // namespace media_router
