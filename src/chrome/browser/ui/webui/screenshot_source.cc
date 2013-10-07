// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/screenshot_source.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/i18n/time_formatting.h"
#include "base/memory/ref_counted_memory.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "url/url_canon.h"
#include "url/url_util.h"

#if defined(USE_ASH)
#include "ash/shell.h"
#include "ash/shell_delegate.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/drive/drive_integration_service.h"
#include "chrome/browser/chromeos/drive/file_system_interface.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chromeos/login/login_state.h"
#include "content/public/browser/browser_thread.h"
#endif

// static
const char ScreenshotSource::kScreenshotUrlRoot[] = "chrome://screenshots/";
// static
const char ScreenshotSource::kScreenshotCurrent[] = "current";
// static
const char ScreenshotSource::kScreenshotSaved[] = "saved/";
#if defined(OS_CHROMEOS)
// static
const char ScreenshotSource::kScreenshotPrefix[] = "Screenshot ";
// static
const char ScreenshotSource::kScreenshotSuffix[] = ".png";
#endif

bool ShouldUse24HourClock() {
#if defined(OS_CHROMEOS)
  Profile* profile = ProfileManager::GetDefaultProfileOrOffTheRecord();
  if (profile) {
    return profile->GetPrefs()->GetBoolean(prefs::kUse24HourClock);
  }
#endif
  return base::GetHourClockType() == base::k24HourClock;
}

ScreenshotSource::ScreenshotSource(
    std::vector<unsigned char>* current_screenshot,
    Profile* profile)
    : profile_(profile) {
  // Setup the last screenshot taken.
  if (current_screenshot)
    current_screenshot_.reset(new ScreenshotData(*current_screenshot));
  else
    current_screenshot_.reset(new ScreenshotData());
}

ScreenshotSource::~ScreenshotSource() {}

// static
std::string ScreenshotSource::GetScreenshotBaseFilename() {
  base::Time::Exploded now;
  base::Time::Now().LocalExplode(&now);

  // We don't use base/i18n/time_formatting.h here because it doesn't
  // support our format.  Don't use ICU either to avoid i18n file names
  // for non-English locales.
  // TODO(mukai): integrate this logic somewhere time_formatting.h
  std::string file_name = base::StringPrintf(
      "Screenshot %d-%02d-%02d at ", now.year, now.month, now.day_of_month);

  if (ShouldUse24HourClock()) {
    file_name.append(base::StringPrintf(
        "%02d.%02d.%02d", now.hour, now.minute, now.second));
  } else {
    int hour = now.hour;
    if (hour > 12) {
      hour -= 12;
    } else if (hour == 0) {
      hour = 12;
    }
    file_name.append(base::StringPrintf(
        "%d.%02d.%02d ", hour, now.minute, now.second));
    file_name.append((now.hour >= 12) ? "PM" : "AM");
  }

  return file_name;
}

#if defined(USE_ASH)

// static
bool ScreenshotSource::AreScreenshotsDisabled() {
  return g_browser_process->local_state()->GetBoolean(
      prefs::kDisableScreenshots);
}

// static
bool ScreenshotSource::GetScreenshotDirectory(base::FilePath* directory) {
  if (ScreenshotSource::AreScreenshotsDisabled())
    return false;

  bool is_logged_in = true;

#if defined(OS_CHROMEOS)
  is_logged_in = chromeos::LoginState::Get()->IsUserLoggedIn();
#endif

  if (is_logged_in) {
    DownloadPrefs* download_prefs = DownloadPrefs::FromBrowserContext(
        ash::Shell::GetInstance()->delegate()->GetCurrentBrowserContext());
    *directory = download_prefs->DownloadPath();
  } else  {
    if (!file_util::GetTempDir(directory)) {
      LOG(ERROR) << "Failed to find temporary directory.";
      return false;
    }
  }
  return true;
}

#endif

std::string ScreenshotSource::GetSource() const {
  return chrome::kChromeUIScreenshotPath;
}

void ScreenshotSource::StartDataRequest(
  const std::string& path,
  int render_process_id,
  int render_view_id,
  const content::URLDataSource::GotDataCallback& callback) {
  SendScreenshot(path, callback);
}

std::string ScreenshotSource::GetMimeType(const std::string&) const {
  // We need to explicitly return a mime type, otherwise if the user tries to
  // drag the image they get no extension.
  return "image/png";
}

ScreenshotDataPtr ScreenshotSource::GetCachedScreenshot(
    const std::string& screenshot_path) {
  std::map<std::string, ScreenshotDataPtr>::iterator pos;
  std::string path = screenshot_path.substr(
      0, screenshot_path.find_first_of("?"));
  if ((pos = cached_screenshots_.find(path)) != cached_screenshots_.end()) {
    return pos->second;
  } else {
    return ScreenshotDataPtr(new ScreenshotData);
  }
}

void ScreenshotSource::SendScreenshot(
    const std::string& screenshot_path,
    const content::URLDataSource::GotDataCallback& callback) {
  // Strip the query param value - we only use it as a hack to ensure our
  // image gets reloaded instead of being pulled from the browser cache
  std::string path = screenshot_path.substr(
      0, screenshot_path.find_first_of("?"));
  if (path == ScreenshotSource::kScreenshotCurrent) {
    CacheAndSendScreenshot(path, callback, current_screenshot_);
#if defined(OS_CHROMEOS)
  } else if (path.compare(0, strlen(ScreenshotSource::kScreenshotSaved),
             ScreenshotSource::kScreenshotSaved) == 0) {
    using content::BrowserThread;

    std::string filename =
        path.substr(strlen(ScreenshotSource::kScreenshotSaved));

    url_canon::RawCanonOutputT<char16> decoded;
    url_util::DecodeURLEscapeSequences(
        filename.data(), filename.size(), &decoded);
    // Screenshot filenames don't use non-ascii characters.
    std::string decoded_filename = UTF16ToASCII(string16(
        decoded.data(), decoded.length()));

    base::FilePath download_path;
    GetScreenshotDirectory(&download_path);
    if (drive::util::IsUnderDriveMountPoint(download_path)) {
      drive::FileSystemInterface* file_system =
          drive::DriveIntegrationServiceFactory::GetForProfile(
              profile_)->file_system();
      file_system->GetFileByPath(
          drive::util::ExtractDrivePath(download_path).Append(decoded_filename),
          base::Bind(&ScreenshotSource::GetSavedScreenshotCallback,
                     base::Unretained(this), screenshot_path, callback));
    } else {
      BrowserThread::PostTask(
          BrowserThread::FILE, FROM_HERE,
          base::Bind(&ScreenshotSource::SendSavedScreenshot,
                     base::Unretained(this),
                     screenshot_path,
                     callback, download_path.Append(decoded_filename)));
    }
#endif
  } else {
    CacheAndSendScreenshot(
        path, callback, ScreenshotDataPtr(new ScreenshotData()));
  }
}

#if defined(OS_CHROMEOS)
void ScreenshotSource::SendSavedScreenshot(
    const std::string& screenshot_path,
    const content::URLDataSource::GotDataCallback& callback,
    const base::FilePath& file) {
  ScreenshotDataPtr read_bytes(new ScreenshotData);
  int64 file_size = 0;

  if (!file_util::GetFileSize(file, &file_size)) {
    CacheAndSendScreenshot(screenshot_path, callback, read_bytes);
    return;
  }

  read_bytes->resize(file_size);
  if (!file_util::ReadFile(file, reinterpret_cast<char*>(&read_bytes->front()),
                           static_cast<int>(file_size)))
    read_bytes->clear();

  CacheAndSendScreenshot(screenshot_path, callback, read_bytes);
}

void ScreenshotSource::GetSavedScreenshotCallback(
    const std::string& screenshot_path,
    const content::URLDataSource::GotDataCallback& callback,
    drive::FileError error,
    const base::FilePath& file,
    scoped_ptr<drive::ResourceEntry> entry) {
  if (error != drive::FILE_ERROR_OK) {
    ScreenshotDataPtr read_bytes(new ScreenshotData);
    CacheAndSendScreenshot(screenshot_path, callback, read_bytes);
    return;
  }

  content::BrowserThread::PostTask(
      content::BrowserThread::FILE, FROM_HERE,
      base::Bind(&ScreenshotSource::SendSavedScreenshot,
                 base::Unretained(this), screenshot_path, callback, file));
}
#endif

void ScreenshotSource::CacheAndSendScreenshot(
    const std::string& screenshot_path,
    const content::URLDataSource::GotDataCallback& callback,
    ScreenshotDataPtr bytes) {
  // Strip the query from the screenshot path.
  std::string path = screenshot_path.substr(
      0, screenshot_path.find_first_of("?"));
  cached_screenshots_[path] = bytes;
  callback.Run(new base::RefCountedBytes(*bytes));
}
