// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/avatar_menu.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "ui/base/resource/resource_bundle.h"

// static
void AvatarMenu::GetImageForMenuButton(const base::FilePath& profile_path,
                                       gfx::Image* image,
                                       bool* is_rectangle) {
  ProfileInfoCache& cache =
      g_browser_process->profile_manager()->GetProfileInfoCache();
  size_t index = cache.GetIndexOfProfileWithPath(profile_path);
  if (index == std::string::npos) {
    NOTREACHED();
    return;
  }

  // If there is a Gaia image available, try to use that.
  if (cache.IsUsingGAIAPictureOfProfileAtIndex(index)) {
    const gfx::Image* gaia_image = cache.GetGAIAPictureOfProfileAtIndex(index);
    if (gaia_image) {
      *image = *gaia_image;
      *is_rectangle = true;
      return;
    }
  }

  // Otherwise, use the default resource, not the downloaded high-res one.
  const size_t icon_index = cache.GetAvatarIconIndexOfProfileAtIndex(index);
  const int resource_id =
      profiles::GetDefaultAvatarIconResourceIDAtIndex(icon_index);
  *image = ResourceBundle::GetSharedInstance().GetNativeImageNamed(resource_id);
  *is_rectangle = false;
}
