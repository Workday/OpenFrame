// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/web_app_win.h"

#include <shlobj.h>

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/file_enumerator.h"
#include "base/logging.h"
#include "base/md5.h"
#include "base/path_service.h"
#include "base/strings/string_piece.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/shortcut.h"
#include "base/win/windows_version.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/installer/launcher_support/chrome_launcher_support.h"
#include "chrome/installer/util/util_constants.h"
#include "content/public/browser/browser_thread.h"
#include "ui/gfx/icon_util.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_family.h"

namespace {

const base::FilePath::CharType kIconChecksumFileExt[] =
    FILE_PATH_LITERAL(".ico.md5");

// Width and height of icons exported to .ico files.

// Calculates checksum of an icon family using MD5.
// The checksum is derived from all of the icons in the family.
void GetImageCheckSum(const gfx::ImageFamily& image, base::MD5Digest* digest) {
  DCHECK(digest);
  base::MD5Context md5_context;
  base::MD5Init(&md5_context);

  for (gfx::ImageFamily::const_iterator it = image.begin(); it != image.end();
       ++it) {
    SkBitmap bitmap = it->AsBitmap();

    SkAutoLockPixels image_lock(bitmap);
    base::StringPiece image_data(
        reinterpret_cast<const char*>(bitmap.getPixels()), bitmap.getSize());
    base::MD5Update(&md5_context, image_data);
  }

  base::MD5Final(digest, &md5_context);
}

// Saves |image| as an |icon_file| with the checksum.
bool SaveIconWithCheckSum(const base::FilePath& icon_file,
                          const gfx::ImageFamily& image) {
  if (!IconUtil::CreateIconFileFromImageFamily(image, icon_file))
    return false;

  base::MD5Digest digest;
  GetImageCheckSum(image, &digest);

  base::FilePath cheksum_file(icon_file.ReplaceExtension(kIconChecksumFileExt));
  return file_util::WriteFile(cheksum_file,
                              reinterpret_cast<const char*>(&digest),
                              sizeof(digest)) == sizeof(digest);
}

// Returns true if |icon_file| is missing or different from |image|.
bool ShouldUpdateIcon(const base::FilePath& icon_file,
                      const gfx::ImageFamily& image) {
  base::FilePath checksum_file(
      icon_file.ReplaceExtension(kIconChecksumFileExt));

  // Returns true if icon_file or checksum file is missing.
  if (!base::PathExists(icon_file) ||
      !base::PathExists(checksum_file))
    return true;

  base::MD5Digest persisted_image_checksum;
  if (sizeof(persisted_image_checksum) != file_util::ReadFile(checksum_file,
                      reinterpret_cast<char*>(&persisted_image_checksum),
                      sizeof(persisted_image_checksum)))
    return true;

  base::MD5Digest downloaded_image_checksum;
  GetImageCheckSum(image, &downloaded_image_checksum);

  // Update icon if checksums are not equal.
  return memcmp(&persisted_image_checksum, &downloaded_image_checksum,
                sizeof(base::MD5Digest)) != 0;
}

// Returns true if |shortcut_file_name| matches profile |profile_path|, and has
// an --app-id flag.
bool IsAppShortcutForProfile(const base::FilePath& shortcut_file_name,
                             const base::FilePath& profile_path) {
  string16 cmd_line_string;
  if (base::win::ResolveShortcut(shortcut_file_name, NULL, &cmd_line_string)) {
    cmd_line_string = L"program " + cmd_line_string;
    CommandLine shortcut_cmd_line = CommandLine::FromString(cmd_line_string);
    return shortcut_cmd_line.HasSwitch(switches::kProfileDirectory) &&
           shortcut_cmd_line.GetSwitchValuePath(switches::kProfileDirectory) ==
               profile_path.BaseName() &&
           shortcut_cmd_line.HasSwitch(switches::kAppId);
  }

  return false;
}

// Finds shortcuts in |shortcut_path| that match profile for |profile_path| and
// extension with title |shortcut_name|.
// If |shortcut_name| is empty, finds all shortcuts matching |profile_path|.
std::vector<base::FilePath> FindAppShortcutsByProfileAndTitle(
    const base::FilePath& shortcut_path,
    const base::FilePath& profile_path,
    const string16& shortcut_name) {
  std::vector<base::FilePath> shortcut_paths;

  if (shortcut_name.empty()) {
    // Find all shortcuts for this profile.
    base::FileEnumerator files(shortcut_path, false,
                               base::FileEnumerator::FILES,
                               FILE_PATH_LITERAL("*.lnk"));
    base::FilePath shortcut_file = files.Next();
    while (!shortcut_file.empty()) {
      if (IsAppShortcutForProfile(shortcut_file, profile_path))
        shortcut_paths.push_back(shortcut_file);
      shortcut_file = files.Next();
    }
  } else {
    // Find all shortcuts matching |shortcut_name|.
    base::FilePath base_path = shortcut_path.
        Append(web_app::internals::GetSanitizedFileName(shortcut_name)).
        AddExtension(FILE_PATH_LITERAL(".lnk"));

    const int fileNamesToCheck = 10;
    for (int i = 0; i < fileNamesToCheck; ++i) {
      base::FilePath shortcut_file = base_path;
      if (i > 0) {
        shortcut_file = shortcut_file.InsertBeforeExtensionASCII(
            base::StringPrintf(" (%d)", i));
      }
      if (base::PathExists(shortcut_file) &&
          IsAppShortcutForProfile(shortcut_file, profile_path)) {
        shortcut_paths.push_back(shortcut_file);
      }
    }
  }

  return shortcut_paths;
}

// Creates application shortcuts in a given set of paths.
// |shortcut_paths| is a list of directories in which shortcuts should be
// created. If |creation_reason| is SHORTCUT_CREATION_AUTOMATED and there is an
// existing shortcut to this app for this profile, does nothing (succeeding).
// Returns true on success, false on failure.
// Must be called on the FILE thread.
bool CreateShortcutsInPaths(
    const base::FilePath& web_app_path,
    const ShellIntegration::ShortcutInfo& shortcut_info,
    const std::vector<base::FilePath>& shortcut_paths,
    web_app::ShortcutCreationReason creation_reason,
    std::vector<base::FilePath>* out_filenames) {
  // Ensure web_app_path exists.
  if (!base::PathExists(web_app_path) &&
      !file_util::CreateDirectory(web_app_path)) {
    return false;
  }

  // Generates file name to use with persisted ico and shortcut file.
  base::FilePath file_name =
      web_app::internals::GetSanitizedFileName(shortcut_info.title);

  // Creates an ico file to use with shortcut.
  base::FilePath icon_file = web_app_path.Append(file_name).AddExtension(
      FILE_PATH_LITERAL(".ico"));
  if (!web_app::internals::CheckAndSaveIcon(icon_file, shortcut_info.favicon)) {
    return false;
  }

  base::FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED();
    return false;
  }

  // Working directory.
  base::FilePath working_dir(chrome_exe.DirName());

  CommandLine cmd_line(CommandLine::NO_PROGRAM);
  cmd_line = ShellIntegration::CommandLineArgsForLauncher(shortcut_info.url,
      shortcut_info.extension_id, shortcut_info.profile_path);

  // TODO(evan): we rely on the fact that command_line_string() is
  // properly quoted for a Windows command line.  The method on
  // CommandLine should probably be renamed to better reflect that
  // fact.
  string16 wide_switches(cmd_line.GetCommandLineString());

  // Sanitize description
  string16 description = shortcut_info.description;
  if (description.length() >= MAX_PATH)
    description.resize(MAX_PATH - 1);

  // Generates app id from web app url and profile path.
  std::string app_name(web_app::GenerateApplicationNameFromInfo(shortcut_info));
  string16 app_id(ShellIntegration::GetAppModelIdForProfile(
      UTF8ToUTF16(app_name), shortcut_info.profile_path));

  bool success = true;
  for (size_t i = 0; i < shortcut_paths.size(); ++i) {
    base::FilePath shortcut_file = shortcut_paths[i].Append(file_name).
        AddExtension(installer::kLnkExt);
    if (creation_reason == web_app::SHORTCUT_CREATION_AUTOMATED) {
      // Check whether there is an existing shortcut to this app.
      std::vector<base::FilePath> shortcut_files =
          FindAppShortcutsByProfileAndTitle(shortcut_paths[i],
                                            shortcut_info.profile_path,
                                            shortcut_info.title);
      if (!shortcut_files.empty())
        continue;
    }
    if (shortcut_paths[i] != web_app_path) {
      int unique_number =
          file_util::GetUniquePathNumber(shortcut_file, FILE_PATH_LITERAL(""));
      if (unique_number == -1) {
        success = false;
        continue;
      } else if (unique_number > 0) {
        shortcut_file = shortcut_file.InsertBeforeExtensionASCII(
            base::StringPrintf(" (%d)", unique_number));
      }
    }
    base::win::ShortcutProperties shortcut_properties;
    shortcut_properties.set_target(chrome_exe);
    shortcut_properties.set_working_dir(working_dir);
    shortcut_properties.set_arguments(wide_switches);
    shortcut_properties.set_description(description);
    shortcut_properties.set_icon(icon_file, 0);
    shortcut_properties.set_app_id(app_id);
    shortcut_properties.set_dual_mode(false);
    if (!base::PathExists(shortcut_file.DirName()) &&
        !file_util::CreateDirectory(shortcut_file.DirName())) {
      NOTREACHED();
      return false;
    }
    success = base::win::CreateOrUpdateShortcutLink(
        shortcut_file, shortcut_properties,
        base::win::SHORTCUT_CREATE_ALWAYS) && success;
    if (out_filenames)
      out_filenames->push_back(shortcut_file);
  }

  return success;
}

// Gets the directories with shortcuts for an app, and deletes the shortcuts.
// This will search the standard locations for shortcuts named |title| that open
// in the profile with |profile_path|.
// |was_pinned_to_taskbar| will be set to true if there was previously a
// shortcut pinned to the taskbar for this app; false otherwise.
// If |web_app_path| is empty, this will not delete shortcuts from the web app
// directory. If |title| is empty, all shortcuts for this profile will be
// deleted.
// |shortcut_paths| will be populated with a list of directories where shortcuts
// for this app were found (and deleted). This will delete duplicate shortcuts,
// but only return each path once, even if it contained multiple deleted
// shortcuts. Both of these may be NULL.
void GetShortcutLocationsAndDeleteShortcuts(
    const base::FilePath& web_app_path,
    const base::FilePath& profile_path,
    const string16& title,
    bool* was_pinned_to_taskbar,
    std::vector<base::FilePath>* shortcut_paths) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));

  // Get all possible locations for shortcuts.
  ShellIntegration::ShortcutLocations all_shortcut_locations;
  all_shortcut_locations.in_applications_menu = true;
  all_shortcut_locations.in_quick_launch_bar = true;
  all_shortcut_locations.on_desktop = true;
  // Delete shortcuts from the Chrome Apps subdirectory.
  // This matches the subdir name set by CreateApplicationShortcutView::Accept
  // for Chrome apps (not URL apps, but this function does not apply for them).
  all_shortcut_locations.applications_menu_subdir =
      web_app::GetAppShortcutsSubdirName();
  std::vector<base::FilePath> all_paths = web_app::internals::GetShortcutPaths(
      all_shortcut_locations);
  if (base::win::GetVersion() >= base::win::VERSION_WIN7 &&
      !web_app_path.empty()) {
    all_paths.push_back(web_app_path);
  }

  if (was_pinned_to_taskbar) {
    // Determine if there is a link to this app in the TaskBar pin directory.
    base::FilePath taskbar_pin_path;
    if (PathService::Get(base::DIR_TASKBAR_PINS, &taskbar_pin_path)) {
      std::vector<base::FilePath> taskbar_pin_files =
          FindAppShortcutsByProfileAndTitle(taskbar_pin_path, profile_path,
                                            title);
      *was_pinned_to_taskbar = !taskbar_pin_files.empty();
    } else {
      *was_pinned_to_taskbar = false;
    }
  }

  for (std::vector<base::FilePath>::const_iterator i = all_paths.begin();
       i != all_paths.end(); ++i) {
    std::vector<base::FilePath> shortcut_files =
        FindAppShortcutsByProfileAndTitle(*i, profile_path, title);
    if (shortcut_paths && !shortcut_files.empty()) {
      shortcut_paths->push_back(*i);
    }
    for (std::vector<base::FilePath>::const_iterator j = shortcut_files.begin();
         j != shortcut_files.end(); ++j) {
      // Any shortcut could have been pinned, either by chrome or the user, so
      // they are all unpinned.
      base::win::TaskbarUnpinShortcutLink(j->value().c_str());
      base::DeleteFile(*j, false);
    }
  }
}

}  // namespace

namespace web_app {

base::FilePath CreateShortcutInWebAppDir(
    const base::FilePath& web_app_dir,
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  std::vector<base::FilePath> paths;
  paths.push_back(web_app_dir);
  std::vector<base::FilePath> out_filenames;
  CreateShortcutsInPaths(web_app_dir, shortcut_info, paths,
                         SHORTCUT_CREATION_BY_USER, &out_filenames);
  DCHECK_EQ(out_filenames.size(), 1u);
  return out_filenames[0];
}

namespace internals {

// Saves |image| to |icon_file| if the file is outdated and refresh shell's
// icon cache to ensure correct icon is displayed. Returns true if icon_file
// is up to date or successfully updated.
bool CheckAndSaveIcon(const base::FilePath& icon_file,
                      const gfx::ImageFamily& image) {
  if (ShouldUpdateIcon(icon_file, image)) {
    if (SaveIconWithCheckSum(icon_file, image)) {
      // Refresh shell's icon cache. This call is quite disruptive as user would
      // see explorer rebuilding the icon cache. It would be great that we find
      // a better way to achieve this.
      SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT,
                     NULL, NULL);
    } else {
      return false;
    }
  }

  return true;
}

bool CreatePlatformShortcuts(
    const base::FilePath& web_app_path,
    const ShellIntegration::ShortcutInfo& shortcut_info,
    const ShellIntegration::ShortcutLocations& creation_locations,
    ShortcutCreationReason creation_reason) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));

  // Shortcut paths under which to create shortcuts.
  std::vector<base::FilePath> shortcut_paths =
      GetShortcutPaths(creation_locations);

  bool pin_to_taskbar = creation_locations.in_quick_launch_bar &&
                        (base::win::GetVersion() >= base::win::VERSION_WIN7);

  // Create/update the shortcut in the web app path for the "Pin To Taskbar"
  // option in Win7. We use the web app path shortcut because we will overwrite
  // it rather than appending unique numbers if the shortcut already exists.
  // This prevents pinned apps from having unique numbers in their names.
  if (pin_to_taskbar)
    shortcut_paths.push_back(web_app_path);

  if (shortcut_paths.empty())
    return false;

  if (!CreateShortcutsInPaths(web_app_path, shortcut_info, shortcut_paths,
                              creation_reason, NULL))
    return false;

  if (pin_to_taskbar) {
    base::FilePath file_name =
        web_app::internals::GetSanitizedFileName(shortcut_info.title);
    // Use the web app path shortcut for pinning to avoid having unique numbers
    // in the application name.
    base::FilePath shortcut_to_pin = web_app_path.Append(file_name).
        AddExtension(installer::kLnkExt);
    if (!base::win::TaskbarPinShortcutLink(shortcut_to_pin.value().c_str()))
      return false;
  }

  return true;
}

void UpdatePlatformShortcuts(
    const base::FilePath& web_app_path,
    const string16& old_app_title,
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));

  // Generates file name to use with persisted ico and shortcut file.
  base::FilePath file_name =
      web_app::internals::GetSanitizedFileName(shortcut_info.title);

  if (old_app_title != shortcut_info.title) {
    // The app's title has changed. Delete all existing app shortcuts and
    // recreate them in any locations they already existed (but do not add them
    // to locations where they do not currently exist).
    bool was_pinned_to_taskbar;
    std::vector<base::FilePath> shortcut_paths;
    GetShortcutLocationsAndDeleteShortcuts(
        web_app_path, shortcut_info.profile_path, old_app_title,
        &was_pinned_to_taskbar, &shortcut_paths);
    CreateShortcutsInPaths(web_app_path, shortcut_info, shortcut_paths,
                           SHORTCUT_CREATION_BY_USER, NULL);
    // If the shortcut was pinned to the taskbar,
    // GetShortcutLocationsAndDeleteShortcuts will have deleted it. In that
    // case, re-pin it.
    if (was_pinned_to_taskbar) {
      base::FilePath file_name =
          web_app::internals::GetSanitizedFileName(shortcut_info.title);
      // Use the web app path shortcut for pinning to avoid having unique
      // numbers in the application name.
      base::FilePath shortcut_to_pin = web_app_path.Append(file_name).
          AddExtension(installer::kLnkExt);
      base::win::TaskbarPinShortcutLink(shortcut_to_pin.value().c_str());
    }
  }

  // If an icon file exists, and is out of date, replace it with the new icon
  // and let the shell know the icon has been modified.
  base::FilePath icon_file = web_app_path.Append(file_name).AddExtension(
      FILE_PATH_LITERAL(".ico"));
  if (base::PathExists(icon_file)) {
    web_app::internals::CheckAndSaveIcon(icon_file, shortcut_info.favicon);
  }
}

void DeletePlatformShortcuts(
    const base::FilePath& web_app_path,
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  GetShortcutLocationsAndDeleteShortcuts(
      web_app_path, shortcut_info.profile_path, shortcut_info.title, NULL,
      NULL);

  // If there are no more shortcuts in the Chrome Apps subdirectory, remove it.
  base::FilePath chrome_apps_dir;
  if (PathService::Get(base::DIR_START_MENU, &chrome_apps_dir)) {
    chrome_apps_dir = chrome_apps_dir.Append(GetAppShortcutsSubdirName());
    if (file_util::IsDirectoryEmpty(chrome_apps_dir))
      base::DeleteFile(chrome_apps_dir, false);
  }
}

void DeleteAllShortcutsForProfile(const base::FilePath& profile_path) {
  GetShortcutLocationsAndDeleteShortcuts(base::FilePath(), profile_path, L"",
                                         NULL, NULL);

  // If there are no more shortcuts in the Chrome Apps subdirectory, remove it.
  base::FilePath chrome_apps_dir;
  if (PathService::Get(base::DIR_START_MENU, &chrome_apps_dir)) {
    chrome_apps_dir = chrome_apps_dir.Append(GetAppShortcutsSubdirName());
    if (file_util::IsDirectoryEmpty(chrome_apps_dir))
      base::DeleteFile(chrome_apps_dir, false);
  }
}

std::vector<base::FilePath> GetShortcutPaths(
    const ShellIntegration::ShortcutLocations& creation_locations) {
  // Shortcut paths under which to create shortcuts.
  std::vector<base::FilePath> shortcut_paths;
  // Locations to add to shortcut_paths.
  struct {
    bool use_this_location;
    int location_id;
    const wchar_t* subdir;
  } locations[] = {
    {
      creation_locations.on_desktop,
      base::DIR_USER_DESKTOP,
      NULL
    }, {
      creation_locations.in_applications_menu,
      base::DIR_START_MENU,
      creation_locations.applications_menu_subdir.empty() ? NULL :
          creation_locations.applications_menu_subdir.c_str()
    }, {
      creation_locations.in_quick_launch_bar,
      // For Win7, in_quick_launch_bar means pinning to taskbar. Use
      // base::PATH_START as a flag for this case.
      (base::win::GetVersion() >= base::win::VERSION_WIN7) ?
          base::PATH_START : base::DIR_APP_DATA,
      (base::win::GetVersion() >= base::win::VERSION_WIN7) ?
          NULL : L"Microsoft\\Internet Explorer\\Quick Launch"
    }
  };
  // Populate shortcut_paths.
  for (int i = 0; i < arraysize(locations); ++i) {
    if (locations[i].use_this_location) {
      base::FilePath path;

      // Skip the Win7 case.
      if (locations[i].location_id == base::PATH_START)
        continue;

      if (!PathService::Get(locations[i].location_id, &path)) {
        continue;
      }

      if (locations[i].subdir != NULL)
        path = path.Append(locations[i].subdir);
      shortcut_paths.push_back(path);
    }
  }
  return shortcut_paths;
}

}  // namespace internals

}  // namespace web_app
