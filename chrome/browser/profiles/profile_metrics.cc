// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/profile_metrics.h"

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/user_metrics.h"

namespace {

ProfileMetrics::ProfileType GetProfileType(
    const base::FilePath& profile_path) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  ProfileMetrics::ProfileType metric = ProfileMetrics::SECONDARY;
  ProfileManager* manager = g_browser_process->profile_manager();
  base::FilePath user_data_dir;
  // In unittests, we do not always have a profile_manager so check.
  if (manager) {
    user_data_dir = manager->user_data_dir();
  }
  if (profile_path == user_data_dir.AppendASCII(chrome::kInitialProfile)) {
    metric = ProfileMetrics::ORIGINAL;
  }
  return metric;
}

}  // namespace

enum ProfileAvatar {
  AVATAR_GENERIC = 0,       // The names for avatar icons
  AVATAR_GENERIC_AQUA,
  AVATAR_GENERIC_BLUE,
  AVATAR_GENERIC_GREEN,
  AVATAR_GENERIC_ORANGE,
  AVATAR_GENERIC_PURPLE,
  AVATAR_GENERIC_RED,
  AVATAR_GENERIC_YELLOW,
  AVATAR_SECRET_AGENT,
  AVATAR_SUPERHERO,
  AVATAR_VOLLEYBALL,        // 10
  AVATAR_BUSINESSMAN,
  AVATAR_NINJA,
  AVATAR_ALIEN,
  AVATAR_AWESOME,
  AVATAR_FLOWER,
  AVATAR_PIZZA,
  AVATAR_SOCCER,
  AVATAR_BURGER,
  AVATAR_CAT,
  AVATAR_CUPCAKE,           // 20
  AVATAR_DOG,
  AVATAR_HORSE,
  AVATAR_MARGARITA,
  AVATAR_NOTE,
  AVATAR_SUN_CLOUD,
  AVATAR_UNKNOWN,           // 26
  AVATAR_GAIA,              // 27
  NUM_PROFILE_AVATAR_METRICS
};

void ProfileMetrics::LogNumberOfProfiles(ProfileManager* manager) {
  const ProfileInfoCache& info_cache = manager->GetProfileInfoCache();
  size_t number_of_profiles = info_cache.GetNumberOfProfiles();
  UMA_HISTOGRAM_COUNTS_100("Profile.NumberOfProfiles",
                            number_of_profiles);

  // Ignore other metrics if we have no profiles, e.g. in Chrome Frame tests.
  if (number_of_profiles) {
    size_t number_of_managed_profiles = 0;
    size_t number_of_signed_in_profiles = 0;
    for (size_t i = 0; i < number_of_profiles; ++i) {
      if (info_cache.ProfileIsManagedAtIndex(i))
        ++number_of_managed_profiles;
      if (!info_cache.GetUserNameOfProfileAtIndex(i).empty())
        ++number_of_signed_in_profiles;
    }
    UMA_HISTOGRAM_COUNTS_100("Profile.NumberOfManagedProfiles",
                              number_of_managed_profiles);
    UMA_HISTOGRAM_COUNTS_100("Profile.PercentageOfManagedProfiles",
        100 * number_of_managed_profiles / number_of_profiles);
    UMA_HISTOGRAM_COUNTS_100("Profile.NumberOfSignedInProfiles",
                              number_of_signed_in_profiles);
  }
}

void ProfileMetrics::LogProfileAddNewUser(ProfileAdd metric) {
  DCHECK(metric < NUM_PROFILE_ADD_METRICS);
  UMA_HISTOGRAM_ENUMERATION("Profile.AddNewUser", metric,
                            NUM_PROFILE_ADD_METRICS);
  UMA_HISTOGRAM_ENUMERATION("Profile.NetUserCount", ADD_NEW_USER,
                            NUM_PROFILE_NET_METRICS);
}

void ProfileMetrics::LogProfileAvatarSelection(size_t icon_index) {
  DCHECK(icon_index < NUM_PROFILE_AVATAR_METRICS);
  ProfileAvatar icon_name = AVATAR_UNKNOWN;
  switch (icon_index) {
    case 0:
      icon_name = AVATAR_GENERIC;
      break;
    case 1:
      icon_name = AVATAR_GENERIC_AQUA;
      break;
    case 2:
      icon_name = AVATAR_GENERIC_BLUE;
      break;
    case 3:
      icon_name = AVATAR_GENERIC_GREEN;
      break;
    case 4:
      icon_name = AVATAR_GENERIC_ORANGE;
      break;
    case 5:
      icon_name = AVATAR_GENERIC_PURPLE;
      break;
    case 6:
      icon_name = AVATAR_GENERIC_RED;
      break;
    case 7:
      icon_name = AVATAR_GENERIC_YELLOW;
      break;
    case 8:
      icon_name = AVATAR_SECRET_AGENT;
      break;
    case 9:
      icon_name = AVATAR_SUPERHERO;
      break;
    case 10:
      icon_name = AVATAR_VOLLEYBALL;
      break;
    case 11:
      icon_name = AVATAR_BUSINESSMAN;
      break;
    case 12:
      icon_name = AVATAR_NINJA;
      break;
    case 13:
      icon_name = AVATAR_ALIEN;
      break;
    case 14:
      icon_name = AVATAR_AWESOME;
      break;
    case 15:
      icon_name = AVATAR_FLOWER;
      break;
    case 16:
      icon_name = AVATAR_PIZZA;
      break;
    case 17:
      icon_name = AVATAR_SOCCER;
      break;
    case 18:
      icon_name = AVATAR_BURGER;
      break;
    case 19:
      icon_name = AVATAR_CAT;
      break;
    case 20:
      icon_name = AVATAR_CUPCAKE;
      break;
    case 21:
      icon_name = AVATAR_DOG;
      break;
    case 22:
      icon_name = AVATAR_HORSE;
      break;
    case 23:
      icon_name = AVATAR_MARGARITA;
      break;
    case 24:
      icon_name = AVATAR_NOTE;
      break;
    case 25:
      icon_name = AVATAR_SUN_CLOUD;
      break;
    case 27:
      icon_name = AVATAR_GAIA;
      break;
    default:  // We should never actually get here.
      NOTREACHED();
      break;
  }
  UMA_HISTOGRAM_ENUMERATION("Profile.Avatar", icon_name,
                            NUM_PROFILE_AVATAR_METRICS);
}

void ProfileMetrics::LogProfileDeleteUser(ProfileNetUserCounts metric) {
  DCHECK(metric < NUM_PROFILE_NET_METRICS);
  UMA_HISTOGRAM_ENUMERATION("Profile.NetUserCount", metric,
                            NUM_PROFILE_NET_METRICS);
}

void ProfileMetrics::LogProfileOpenMethod(ProfileOpen metric) {
  DCHECK(metric < NUM_PROFILE_OPEN_METRICS);
  UMA_HISTOGRAM_ENUMERATION("Profile.OpenMethod", metric,
                            NUM_PROFILE_OPEN_METRICS);
}

void ProfileMetrics::LogProfileSwitchGaia(ProfileGaia metric) {
  if (metric == GAIA_OPT_IN)
    LogProfileAvatarSelection(AVATAR_GAIA);
  UMA_HISTOGRAM_ENUMERATION("Profile.SwitchGaiaPhotoSettings",
                            metric,
                            NUM_PROFILE_GAIA_METRICS);
}

void ProfileMetrics::LogProfileSwitchUser(ProfileOpen metric) {
  DCHECK(metric < NUM_PROFILE_OPEN_METRICS);
  UMA_HISTOGRAM_ENUMERATION("Profile.OpenMethod", metric,
                            NUM_PROFILE_OPEN_METRICS);
}

void ProfileMetrics::LogProfileSyncInfo(ProfileSync metric) {
  DCHECK(metric < NUM_PROFILE_SYNC_METRICS);
  UMA_HISTOGRAM_ENUMERATION("Profile.SyncCustomize", metric,
                            NUM_PROFILE_SYNC_METRICS);
}

void ProfileMetrics::LogProfileLaunch(Profile* profile) {
  base::FilePath profile_path = profile->GetPath();
  UMA_HISTOGRAM_ENUMERATION("Profile.LaunchBrowser",
                            GetProfileType(profile_path),
                            NUM_PROFILE_TYPE_METRICS);

  if (profile->IsManaged()) {
    content::RecordAction(
        content::UserMetricsAction("ManagedMode_NewManagedUserWindow"));
  }
}

void ProfileMetrics::LogProfileSyncSignIn(const base::FilePath& profile_path) {
  UMA_HISTOGRAM_ENUMERATION("Profile.SyncSignIn",
                            GetProfileType(profile_path),
                            NUM_PROFILE_TYPE_METRICS);
}

void ProfileMetrics::LogProfileUpdate(const base::FilePath& profile_path) {
  UMA_HISTOGRAM_ENUMERATION("Profile.Update",
                            GetProfileType(profile_path),
                            NUM_PROFILE_TYPE_METRICS);
}
