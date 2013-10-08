// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/pref_names.h"

namespace apps {

namespace prefs {

// If set, the user requested to launch the app with this extension id while
// in Metro mode, and then relaunched to Desktop mode to start it.
const char kAppLaunchForMetroRestart[] = "apps.app_launch_for_metro_restart";

// Set with |kAppLaunchForMetroRestart|, the profile whose loading triggers
// launch of the specified app when restarting Chrome in desktop mode.
const char kAppLaunchForMetroRestartProfile[] =
    "apps.app_launch_for_metro_restart_profile";

// A boolean that tracks whether the user has ever enabled the app launcher.
const char kAppLauncherHasBeenEnabled[] =
    "apps.app_launcher.has_been_enabled";

// TODO(calamity): remove this pref since app launcher will always be
// installed.
// Local state caching knowledge of whether the app launcher is installed.
const char kAppLauncherIsEnabled[] =
    "apps.app_launcher.should_show_apps_page";

// A boolean that indicates whether app shortcuts have been created.
// On a transition from false to true, shortcuts are created for all apps.
const char kShortcutsHaveBeenCreated[] = "apps.shortcuts_have_been_created";

// A boolean identifying if we should show the app launcher promo or not.
const char kShowAppLauncherPromo[] = "app_launcher.show_promo";

}  // namespace prefs

}  // namespace apps
