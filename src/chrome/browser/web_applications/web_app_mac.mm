// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/web_applications/web_app_mac.h"

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>

#include "apps/app_shim/app_shim_mac.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/file_enumerator.h"
#include "base/files/scoped_temp_dir.h"
#include "base/mac/bundle_locations.h"
#include "base/mac/foundation_util.h"
#include "base/mac/launch_services_util.h"
#include "base/mac/mac_logging.h"
#include "base/mac/mac_util.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/mac/dock.h"
#include "chrome/browser/ui/web_applications/web_app_ui.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/mac/app_mode_common.h"
#include "content/public/browser/browser_thread.h"
#include "grit/chromium_strings.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/gfx/image/image_family.h"

namespace {

// Launch Services Key to run as an agent app, which doesn't launch in the dock.
NSString* const kLSUIElement = @"LSUIElement";

class ScopedCarbonHandle {
 public:
  ScopedCarbonHandle(size_t initial_size) : handle_(NewHandle(initial_size)) {
    DCHECK(handle_);
    DCHECK_EQ(noErr, MemError());
  }
  ~ScopedCarbonHandle() { DisposeHandle(handle_); }

  Handle Get() { return handle_; }
  char* Data() { return *handle_; }
  size_t HandleSize() const { return GetHandleSize(handle_); }

  IconFamilyHandle GetAsIconFamilyHandle() {
    return reinterpret_cast<IconFamilyHandle>(handle_);
  }

  bool WriteDataToFile(const base::FilePath& path) {
    NSData* data = [NSData dataWithBytes:Data()
                                  length:HandleSize()];
    return [data writeToFile:base::mac::FilePathToNSString(path)
                  atomically:NO];
  }

 private:
  Handle handle_;
};

void ConvertSkiaToARGB(const SkBitmap& bitmap, ScopedCarbonHandle* handle) {
  CHECK_EQ(4u * bitmap.width() * bitmap.height(), handle->HandleSize());

  char* argb = handle->Data();
  SkAutoLockPixels lock(bitmap);
  for (int y = 0; y < bitmap.height(); ++y) {
    for (int x = 0; x < bitmap.width(); ++x) {
      SkColor pixel = bitmap.getColor(x, y);
      argb[0] = SkColorGetA(pixel);
      argb[1] = SkColorGetR(pixel);
      argb[2] = SkColorGetG(pixel);
      argb[3] = SkColorGetB(pixel);
      argb += 4;
    }
  }
}

// Adds |image| to |icon_family|. Returns true on success, false on failure.
bool AddGfxImageToIconFamily(IconFamilyHandle icon_family,
                             const gfx::Image& image) {
  // When called via ShowCreateChromeAppShortcutsDialog the ImageFamily will
  // have all the representations desired here for mac, from the kDesiredSizes
  // array in web_app_ui.cc.
  SkBitmap bitmap = image.AsBitmap();
  if (bitmap.config() != SkBitmap::kARGB_8888_Config ||
      bitmap.width() != bitmap.height()) {
    return false;
  }

  OSType icon_type;
  switch (bitmap.width()) {
    case 512:
      icon_type = kIconServices512PixelDataARGB;
      break;
    case 256:
      icon_type = kIconServices256PixelDataARGB;
      break;
    case 128:
      icon_type = kIconServices128PixelDataARGB;
      break;
    case 48:
      icon_type = kIconServices48PixelDataARGB;
      break;
    case 32:
      icon_type = kIconServices32PixelDataARGB;
      break;
    case 16:
      icon_type = kIconServices16PixelDataARGB;
      break;
    default:
      return false;
  }

  ScopedCarbonHandle raw_data(bitmap.getSize());
  ConvertSkiaToARGB(bitmap, &raw_data);
  OSErr result = SetIconFamilyData(icon_family, icon_type, raw_data.Get());
  DCHECK_EQ(noErr, result);
  return result == noErr;
}

base::FilePath GetWritableApplicationsDirectory() {
  base::FilePath path;
  if (base::mac::GetLocalDirectory(NSApplicationDirectory, &path) &&
      base::PathIsWritable(path)) {
    return path;
  }
  if (base::mac::GetUserDirectory(NSApplicationDirectory, &path))
    return path;
  return base::FilePath();
}

// Given the path to an app bundle, return the resources directory.
base::FilePath GetResourcesPath(const base::FilePath& app_path) {
  return app_path.Append("Contents").Append("Resources");
}

bool HasExistingExtensionShim(const base::FilePath& destination_directory,
                              const std::string& extension_id,
                              const base::FilePath& own_basename) {
  // Check if there any any other shims for the same extension.
  base::FileEnumerator enumerator(destination_directory,
                                  false /* recursive */,
                                  base::FileEnumerator::DIRECTORIES);
  for (base::FilePath shim_path = enumerator.Next();
       !shim_path.empty(); shim_path = enumerator.Next()) {
    if (shim_path.BaseName() != own_basename &&
        EndsWith(shim_path.RemoveExtension().value(),
                 extension_id,
                 true /* case_sensitive */)) {
      return true;
    }
  }

  return false;
}

// Given the path to an app bundle, return the path to the Info.plist file.
NSString* GetPlistPath(const base::FilePath& bundle_path) {
  return base::mac::FilePathToNSString(
      bundle_path.Append("Contents").Append("Info.plist"));
}

NSMutableDictionary* ReadPlist(NSString* plist_path) {
  return [NSMutableDictionary dictionaryWithContentsOfFile:plist_path];
}

// Takes the path to an app bundle and checks that the CrAppModeUserDataDir in
// the Info.plist starts with the current user_data_dir. This uses starts with
// instead of equals because the CrAppModeUserDataDir could be the user_data_dir
// or the app_data_path.
bool HasSameUserDataDir(const base::FilePath& bundle_path) {
  NSDictionary* plist = ReadPlist(GetPlistPath(bundle_path));
  base::FilePath user_data_dir;
  PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
  DCHECK(!user_data_dir.empty());
  return StartsWithASCII(
      base::SysNSStringToUTF8(
          [plist valueForKey:app_mode::kCrAppModeUserDataDirKey]),
      user_data_dir.value(),
      true /* case_sensitive */);
}

void LaunchShimOnFileThread(
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  base::FilePath shim_path = web_app::GetAppInstallPath(shortcut_info);

  if (shim_path.empty() ||
      !base::PathExists(shim_path) ||
      !HasSameUserDataDir(shim_path)) {
    // The user may have deleted the copy in the Applications folder, use the
    // one in the web app's app_data_path.
    base::FilePath app_data_path = web_app::GetWebAppDataDirectory(
        shortcut_info.profile_path, shortcut_info.extension_id, GURL());
    shim_path = app_data_path.Append(shim_path.BaseName());
  }

  if (!base::PathExists(shim_path))
    return;

  CommandLine command_line(CommandLine::NO_PROGRAM);
  command_line.AppendSwitch(app_mode::kNoLaunchApp);
  // Launch without activating (kLSLaunchDontSwitch).
  base::mac::OpenApplicationWithPath(
      shim_path, command_line, kLSLaunchDefaults | kLSLaunchDontSwitch, NULL);
}

base::FilePath GetLocalizableAppShortcutsSubdirName() {
#if defined(GOOGLE_CHROME_BUILD)
  static const char kChromeAppDirName[] = "Chrome Apps.localized";
  static const char kChromeCanaryAppDirName[] = "Chrome Canary Apps.localized";

  chrome::VersionInfo::Channel channel = chrome::VersionInfo::GetChannel();
  if (channel == chrome::VersionInfo::CHANNEL_CANARY)
    return base::FilePath(kChromeCanaryAppDirName);

  return base::FilePath(kChromeAppDirName);
#else
  static const char kChromiumAppDirName[] = "Chromium Apps.localized";

  return base::FilePath(kChromiumAppDirName);
#endif
}

// Adds a localized strings file for the Chrome Apps directory using the current
// locale. OSX will use this for the display name.
// + Chrome Apps.localized (|apps_directory|)
// | + .localized
// | | en.strings
// | | de.strings
void UpdateAppShortcutsSubdirLocalizedName(
    const base::FilePath& apps_directory) {
  base::FilePath localized = apps_directory.Append(".localized");
  if (!file_util::CreateDirectory(localized))
    return;

  base::FilePath directory_name = apps_directory.BaseName().RemoveExtension();
  string16 localized_name = web_app::GetAppShortcutsSubdirName();
  NSDictionary* strings_dict = @{
      base::mac::FilePathToNSString(directory_name) :
          base::SysUTF16ToNSString(localized_name)
  };

  std::string locale = l10n_util::NormalizeLocale(
      l10n_util::GetApplicationLocale(std::string()));

  NSString* strings_path = base::mac::FilePathToNSString(
      localized.Append(locale + ".strings"));
  [strings_dict writeToFile:strings_path
                 atomically:YES];
}

void DeletePathAndParentIfEmpty(const base::FilePath& app_path) {
  DCHECK(!app_path.empty());
  base::DeleteFile(app_path, true);
  base::FilePath apps_folder = app_path.DirName();
  if (file_util::IsDirectoryEmpty(apps_folder))
    base::DeleteFile(apps_folder, false);
}

bool IsShimForProfile(const base::FilePath& base_name,
                      const std::string& profile_base_name) {
  if (!StartsWithASCII(base_name.value(), profile_base_name, true))
    return false;

  if (base_name.Extension() != ".app")
    return false;

  std::string app_id = base_name.RemoveExtension().value();
  // Strip (profile_base_name + " ") from the start.
  app_id = app_id.substr(profile_base_name.size() + 1);
  return extensions::Extension::IdIsValid(app_id);
}

std::vector<base::FilePath> GetAllAppBundlesInPath(
    const base::FilePath& internal_shortcut_path,
    const std::string& profile_base_name) {
  std::vector<base::FilePath> bundle_paths;

  base::FileEnumerator enumerator(internal_shortcut_path,
                                  true /* recursive */,
                                  base::FileEnumerator::DIRECTORIES);
  for (base::FilePath bundle_path = enumerator.Next();
       !bundle_path.empty(); bundle_path = enumerator.Next()) {
    if (IsShimForProfile(bundle_path.BaseName(), profile_base_name))
      bundle_paths.push_back(bundle_path);
  }

  return bundle_paths;
}

ShellIntegration::ShortcutInfo BuildShortcutInfoFromBundle(
    const base::FilePath& bundle_path) {
  NSDictionary* plist = ReadPlist(GetPlistPath(bundle_path));

  ShellIntegration::ShortcutInfo shortcut_info;
  shortcut_info.extension_id = base::SysNSStringToUTF8(
      [plist valueForKey:app_mode::kCrAppModeShortcutIDKey]);
  shortcut_info.is_platform_app = true;
  shortcut_info.url = GURL(base::SysNSStringToUTF8(
      [plist valueForKey:app_mode::kCrAppModeShortcutURLKey]));
  shortcut_info.title = base::SysNSStringToUTF16(
      [plist valueForKey:app_mode::kCrAppModeShortcutNameKey]);
  shortcut_info.profile_name = base::SysNSStringToUTF8(
      [plist valueForKey:app_mode::kCrAppModeProfileNameKey]);

  // Figure out the profile_path. Since the user_data_dir could contain the
  // path to the web app data dir.
  base::FilePath user_data_dir = base::mac::NSStringToFilePath(
      [plist valueForKey:app_mode::kCrAppModeUserDataDirKey]);
  base::FilePath profile_base_name = base::mac::NSStringToFilePath(
      [plist valueForKey:app_mode::kCrAppModeProfileDirKey]);
  if (user_data_dir.DirName().DirName().BaseName() == profile_base_name)
    shortcut_info.profile_path = user_data_dir.DirName().DirName();
  else
    shortcut_info.profile_path = user_data_dir.Append(profile_base_name);

  return shortcut_info;
}

}  // namespace

namespace web_app {


WebAppShortcutCreator::WebAppShortcutCreator(
    const base::FilePath& app_data_path,
    const ShellIntegration::ShortcutInfo& shortcut_info,
    const std::string& chrome_bundle_id)
    : app_data_path_(app_data_path),
      info_(shortcut_info),
      chrome_bundle_id_(chrome_bundle_id) {
}

WebAppShortcutCreator::~WebAppShortcutCreator() {
}

base::FilePath WebAppShortcutCreator::GetShortcutName() const {
  std::string app_name;
  // Check if there should be a separate shortcut made for different profiles.
  // Such shortcuts will have a |profile_name| set on the ShortcutInfo,
  // otherwise it will be empty.
  if (!info_.profile_name.empty()) {
    app_name += info_.profile_path.BaseName().value();
    app_name += ' ';
  }
  app_name += info_.extension_id;
  return base::FilePath(app_name).ReplaceExtension("app");
}

bool WebAppShortcutCreator::BuildShortcut(
    const base::FilePath& staging_path) const {
  // Update the app's plist and icon in a temp directory. This works around
  // a Finder bug where the app's icon doesn't properly update.
  if (!base::CopyDirectory(GetAppLoaderPath(), staging_path, true)) {
    LOG(ERROR) << "Copying app to staging path: " << staging_path.value()
               << " failed.";
    return false;
  }

  if (!UpdatePlist(staging_path))
    return false;

  if (!UpdateDisplayName(staging_path))
    return false;

  if (!UpdateIcon(staging_path))
    return false;

  return true;
}

size_t WebAppShortcutCreator::CreateShortcutsIn(
    const std::vector<base::FilePath>& folders) const {
  size_t succeeded = 0;

  base::ScopedTempDir scoped_temp_dir;
  if (!scoped_temp_dir.CreateUniqueTempDir())
    return 0;

  base::FilePath app_name = GetShortcutName();
  base::FilePath staging_path = scoped_temp_dir.path().Append(app_name);
  if (!BuildShortcut(staging_path))
    return 0;

  for (std::vector<base::FilePath>::const_iterator it = folders.begin();
       it != folders.end(); ++it) {
    const base::FilePath& dst_path = *it;
    if (!file_util::CreateDirectory(dst_path)) {
      LOG(ERROR) << "Creating directory " << dst_path.value() << " failed.";
      return succeeded;
    }

    if (!base::CopyDirectory(staging_path, dst_path, true)) {
      LOG(ERROR) << "Copying app to dst path: " << dst_path.value()
                 << " failed";
      return succeeded;
    }

    base::mac::RemoveQuarantineAttribute(dst_path.Append(app_name));
    ++succeeded;
  }

  return succeeded;
}

bool WebAppShortcutCreator::CreateShortcuts(
    ShortcutCreationReason creation_reason) {
  const base::FilePath applications_path = GetDestinationPath();
  if (applications_path.empty() ||
      !base::DirectoryExists(applications_path.DirName())) {
    LOG(ERROR) << "Couldn't find an Applications directory to copy app to.";
    return false;
  }

  UpdateAppShortcutsSubdirLocalizedName(applications_path);

  // If non-nil, this path is added to the OSX Dock after creating shortcuts.
  NSString* path_to_add_to_dock = nil;

  std::vector<base::FilePath> paths;

  // For the app list shim, place a copy in Chrome's user data dir for use in
  // the OSX Dock, and do not create the copy in the profile dir. This is done
  // because the kAppLauncherHasBeenEnabled preference is tied to the local
  // state, rather than per-profile.
  const bool is_app_list = info_.extension_id == app_mode::kAppListModeId;
  if (is_app_list) {
    base::FilePath user_data_dir;
    CHECK(PathService::Get(chrome::DIR_USER_DATA, &user_data_dir));
    path_to_add_to_dock = base::SysUTF8ToNSString(
        user_data_dir.Append(GetShortcutName()).AsUTF8Unsafe());
    paths.push_back(user_data_dir);
  } else {
    paths.push_back(app_data_path_);
  }
  paths.push_back(applications_path);

  size_t success_count = CreateShortcutsIn(paths);
  if (success_count == 0)
    return false;

  if (!is_app_list)
    UpdateInternalBundleIdentifier();

  if (success_count != paths.size())
    return false;

  if (path_to_add_to_dock)
    dock::AddIcon(path_to_add_to_dock, nil);

  if (creation_reason == SHORTCUT_CREATION_BY_USER)
    RevealAppShimInFinder();

  return true;
}

void WebAppShortcutCreator::DeleteShortcuts() {
  base::FilePath dst_path = GetDestinationPath();
  if (!dst_path.empty()) {
    base::FilePath bundle_path = dst_path.Append(GetShortcutName());
    if (HasSameUserDataDir(bundle_path))
      DeletePathAndParentIfEmpty(bundle_path);
  }

  // In case the user has moved/renamed/copied the app bundle.
  base::FilePath bundle_path = GetAppBundleById(GetBundleIdentifier());
  if (!bundle_path.empty() && HasSameUserDataDir(bundle_path))
    base::DeleteFile(bundle_path, true);

  // Delete the internal one.
  DeletePathAndParentIfEmpty(app_data_path_.Append(GetShortcutName()));
}

bool WebAppShortcutCreator::UpdateShortcuts() {
  std::vector<base::FilePath> paths;
  base::DeleteFile(app_data_path_.Append(GetShortcutName()), true);
  paths.push_back(app_data_path_);

  base::FilePath dst_path = GetDestinationPath();
  base::FilePath app_path = dst_path.Append(GetShortcutName());

  // If the path does not exist, check if a matching bundle can be found
  // elsewhere.
  if (dst_path.empty() || !base::PathExists(app_path))
    app_path = GetAppBundleById(GetBundleIdentifier());

  if (!app_path.empty()) {
    base::DeleteFile(app_path, true);
    paths.push_back(app_path.DirName());
  }

  size_t success_count = CreateShortcutsIn(paths);
  if (success_count == 0)
    return false;

  UpdateInternalBundleIdentifier();
  return success_count == paths.size() && !app_path.empty();
}

base::FilePath WebAppShortcutCreator::GetAppLoaderPath() const {
  return base::mac::PathForFrameworkBundleResource(
      base::mac::NSToCFCast(@"app_mode_loader.app"));
}

base::FilePath WebAppShortcutCreator::GetDestinationPath() const {
  base::FilePath path = GetWritableApplicationsDirectory();
  if (path.empty())
    return path;
  return path.Append(GetLocalizableAppShortcutsSubdirName());
}

bool WebAppShortcutCreator::UpdatePlist(const base::FilePath& app_path) const {
  NSString* extension_id = base::SysUTF8ToNSString(info_.extension_id);
  NSString* extension_title = base::SysUTF16ToNSString(info_.title);
  NSString* extension_url = base::SysUTF8ToNSString(info_.url.spec());
  NSString* chrome_bundle_id = base::SysUTF8ToNSString(chrome_bundle_id_);
  NSDictionary* replacement_dict =
      [NSDictionary dictionaryWithObjectsAndKeys:
          extension_id, app_mode::kShortcutIdPlaceholder,
          extension_title, app_mode::kShortcutNamePlaceholder,
          extension_url, app_mode::kShortcutURLPlaceholder,
          chrome_bundle_id, app_mode::kShortcutBrowserBundleIDPlaceholder,
          nil];

  NSString* plist_path = GetPlistPath(app_path);
  NSMutableDictionary* plist = ReadPlist(plist_path);
  NSArray* keys = [plist allKeys];

  // 1. Fill in variables.
  for (id key in keys) {
    NSString* value = [plist valueForKey:key];
    if (![value isKindOfClass:[NSString class]] || [value length] < 2)
      continue;

    // Remove leading and trailing '@'s.
    NSString* variable =
        [value substringWithRange:NSMakeRange(1, [value length] - 2)];

    NSString* substitution = [replacement_dict valueForKey:variable];
    if (substitution)
      [plist setObject:substitution forKey:key];
  }

  // 2. Fill in other values.
  [plist setObject:base::SysUTF8ToNSString(GetBundleIdentifier())
            forKey:base::mac::CFToNSCast(kCFBundleIdentifierKey)];
  [plist setObject:base::mac::FilePathToNSString(app_data_path_)
            forKey:app_mode::kCrAppModeUserDataDirKey];
  [plist setObject:base::mac::FilePathToNSString(info_.profile_path.BaseName())
            forKey:app_mode::kCrAppModeProfileDirKey];
  [plist setObject:base::SysUTF8ToNSString(info_.profile_name)
            forKey:app_mode::kCrAppModeProfileNameKey];
  [plist setObject:[NSNumber numberWithBool:YES]
            forKey:app_mode::kLSHasLocalizedDisplayNameKey];
  if (info_.extension_id == app_mode::kAppListModeId) {
    // Prevent the app list from bouncing in the dock, and getting a run light.
    [plist setObject:[NSNumber numberWithBool:YES]
              forKey:kLSUIElement];
  }

  base::FilePath app_name = app_path.BaseName().RemoveExtension();
  [plist setObject:base::mac::FilePathToNSString(app_name)
            forKey:base::mac::CFToNSCast(kCFBundleNameKey)];

  return [plist writeToFile:plist_path
                 atomically:YES];
}

bool WebAppShortcutCreator::UpdateDisplayName(
    const base::FilePath& app_path) const {
  // OSX searches for the best language in the order of preferred languages.
  // Since we only have one localization directory, it will choose this one.
  base::FilePath localized_dir = GetResourcesPath(app_path).Append("en.lproj");
  if (!file_util::CreateDirectory(localized_dir))
    return false;

  NSString* bundle_name = base::SysUTF16ToNSString(info_.title);
  NSString* display_name = base::SysUTF16ToNSString(info_.title);
  if (HasExistingExtensionShim(GetDestinationPath(),
                               info_.extension_id,
                               app_path.BaseName())) {
    display_name = [bundle_name
        stringByAppendingString:base::SysUTF8ToNSString(
            " (" + info_.profile_name + ")")];
  }

  NSDictionary* strings_plist = @{
    base::mac::CFToNSCast(kCFBundleNameKey) : bundle_name,
    app_mode::kCFBundleDisplayNameKey : display_name
  };

  NSString* localized_path = base::mac::FilePathToNSString(
      localized_dir.Append("InfoPlist.strings"));
  return [strings_plist writeToFile:localized_path
                         atomically:YES];
}

bool WebAppShortcutCreator::UpdateIcon(const base::FilePath& app_path) const {
  if (info_.favicon.empty())
    return true;

  ScopedCarbonHandle icon_family(0);
  bool image_added = false;
  for (gfx::ImageFamily::const_iterator it = info_.favicon.begin();
       it != info_.favicon.end(); ++it) {
    if (it->IsEmpty())
      continue;

    // Missing an icon size is not fatal so don't fail if adding the bitmap
    // doesn't work.
    if (!AddGfxImageToIconFamily(icon_family.GetAsIconFamilyHandle(), *it))
      continue;

    image_added = true;
  }

  if (!image_added)
    return false;

  base::FilePath resources_path = GetResourcesPath(app_path);
  if (!file_util::CreateDirectory(resources_path))
    return false;

  return icon_family.WriteDataToFile(resources_path.Append("app.icns"));
}

bool WebAppShortcutCreator::UpdateInternalBundleIdentifier() const {
  NSString* plist_path = GetPlistPath(app_data_path_.Append(GetShortcutName()));
  NSMutableDictionary* plist = ReadPlist(plist_path);

  [plist setObject:base::SysUTF8ToNSString(GetInternalBundleIdentifier())
            forKey:base::mac::CFToNSCast(kCFBundleIdentifierKey)];
  return [plist writeToFile:plist_path
                 atomically:YES];
}

base::FilePath WebAppShortcutCreator::GetAppBundleById(
    const std::string& bundle_id) const {
  base::ScopedCFTypeRef<CFStringRef> bundle_id_cf(
      base::SysUTF8ToCFStringRef(bundle_id));
  CFURLRef url_ref = NULL;
  OSStatus status = LSFindApplicationForInfo(
      kLSUnknownCreator, bundle_id_cf.get(), NULL, NULL, &url_ref);
  if (status != noErr)
    return base::FilePath();

  base::ScopedCFTypeRef<CFURLRef> url(url_ref);
  NSString* path_string = [base::mac::CFToNSCast(url.get()) path];
  return base::FilePath([path_string fileSystemRepresentation]);
}

std::string WebAppShortcutCreator::GetBundleIdentifier() const {
  // Replace spaces in the profile path with hyphen.
  std::string normalized_profile_path;
  ReplaceChars(info_.profile_path.BaseName().value(),
               " ", "-", &normalized_profile_path);

  // This matches APP_MODE_APP_BUNDLE_ID in chrome/chrome.gyp.
  std::string bundle_id =
      chrome_bundle_id_ + std::string(".app.") +
      normalized_profile_path + "-" + info_.extension_id;

  return bundle_id;
}

std::string WebAppShortcutCreator::GetInternalBundleIdentifier() const {
  return GetBundleIdentifier() + "-internal";
}

void WebAppShortcutCreator::RevealAppShimInFinder() const {
  base::FilePath dst_path = GetDestinationPath();
  if (dst_path.empty())
    return;

  base::FilePath app_path = dst_path.Append(GetShortcutName());
  [[NSWorkspace sharedWorkspace]
                    selectFile:base::mac::FilePathToNSString(app_path)
      inFileViewerRootedAtPath:nil];
}

base::FilePath GetAppInstallPath(
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  WebAppShortcutCreator shortcut_creator(base::FilePath(),
                                         shortcut_info,
                                         std::string());
  base::FilePath dst_path = shortcut_creator.GetDestinationPath();
  return dst_path.empty() ?
      base::FilePath() : dst_path.Append(shortcut_creator.GetShortcutName());
}

void MaybeLaunchShortcut(const ShellIntegration::ShortcutInfo& shortcut_info) {
  if (!apps::IsAppShimsEnabled())
    return;

  content::BrowserThread::PostTask(
      content::BrowserThread::FILE, FROM_HERE,
      base::Bind(&LaunchShimOnFileThread, shortcut_info));
}

namespace internals {

bool CreatePlatformShortcuts(
    const base::FilePath& app_data_path,
    const ShellIntegration::ShortcutInfo& shortcut_info,
    const ShellIntegration::ShortcutLocations& creation_locations,
    ShortcutCreationReason creation_reason) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  WebAppShortcutCreator shortcut_creator(
      app_data_path, shortcut_info, base::mac::BaseBundleID());
  return shortcut_creator.CreateShortcuts(creation_reason);
}

void DeletePlatformShortcuts(
    const base::FilePath& app_data_path,
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  WebAppShortcutCreator shortcut_creator(
      app_data_path, shortcut_info, base::mac::BaseBundleID());
  shortcut_creator.DeleteShortcuts();
}

void UpdatePlatformShortcuts(
    const base::FilePath& app_data_path,
    const string16& old_app_title,
    const ShellIntegration::ShortcutInfo& shortcut_info) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  WebAppShortcutCreator shortcut_creator(
      app_data_path, shortcut_info, base::mac::BaseBundleID());
  shortcut_creator.UpdateShortcuts();
}

void DeleteAllShortcutsForProfile(const base::FilePath& profile_path) {
  const std::string profile_base_name = profile_path.BaseName().value();
  std::vector<base::FilePath> bundles = GetAllAppBundlesInPath(
      profile_path.Append(chrome::kWebAppDirname), profile_base_name);

  for (std::vector<base::FilePath>::const_iterator it = bundles.begin();
       it != bundles.end(); ++it) {
    ShellIntegration::ShortcutInfo shortcut_info =
        BuildShortcutInfoFromBundle(*it);
    WebAppShortcutCreator shortcut_creator(
        it->DirName(), shortcut_info, base::mac::BaseBundleID());
    shortcut_creator.DeleteShortcuts();
  }
}

}  // namespace internals

}  // namespace web_app
