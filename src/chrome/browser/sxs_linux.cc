// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sxs_linux.h"

#include <vector>

#include "base/logging.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/files/important_file_writer.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_version_info.h"
#include "content/public/browser/browser_thread.h"

namespace {

bool DoAddChannelMarkToUserDataDir(const base::FilePath& user_data_dir) {
  std::string product_channel_name;
  chrome::VersionInfo::Channel product_channel(
      chrome::VersionInfo::GetChannel());
  switch (product_channel) {
    case chrome::VersionInfo::CHANNEL_UNKNOWN: {
      // Add the channel mark even for Chromium builds (which do not have
      // channel) to better handle possibility of users using Chromium builds
      // with their Google Chrome profiles. Include version string modifier
      // as additional piece of information for debugging (it can't make
      // a meaningful difference for the code since unknown does not match any
      // real channel name).
      std::string version_string_modifier(
          chrome::VersionInfo::GetVersionStringModifier());
      product_channel_name = "unknown (" + version_string_modifier + ")";
      break;
    }
    case chrome::VersionInfo::CHANNEL_CANARY:
      product_channel_name = "canary";
      break;
    case chrome::VersionInfo::CHANNEL_DEV:
      product_channel_name = "dev";
      break;
    case chrome::VersionInfo::CHANNEL_BETA:
      product_channel_name = "beta";
      break;
    case chrome::VersionInfo::CHANNEL_STABLE:
      product_channel_name = "stable";
      break;
    // Rely on -Wswitch compiler warning to detect unhandled enum values.
  }

  base::FilePath channels_path(user_data_dir.AppendASCII("Channels"));
  std::vector<std::string> user_data_dir_channels;

  // Note: failure to read the channels file is not fatal. It's possible
  // and legitimate that it doesn't exist, e.g. for new profile or for profile
  // existing before channel marks have been introduced.
  std::string channels_contents;
  if (file_util::ReadFileToString(channels_path, &channels_contents))
    base::SplitString(channels_contents, '\n', &user_data_dir_channels);

  if (std::find(user_data_dir_channels.begin(),
                user_data_dir_channels.end(),
                product_channel_name) != user_data_dir_channels.end()) {
    // No need to do further disk writes if our channel mark is already present.
    return true;
  }

  user_data_dir_channels.push_back(product_channel_name);
  return base::ImportantFileWriter::WriteFileAtomically(
      channels_path,
      JoinString(user_data_dir_channels, "\n"));
}

}  // namespace

namespace sxs_linux {

void AddChannelMarkToUserDataDir() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));
  base::FilePath user_data_dir;
  if (!PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    LOG(ERROR) << "Failed to get user data dir path. The profile will not be "
               << "automatically migrated for updated Linux packages.";
    return;
  }

  if (!DoAddChannelMarkToUserDataDir(user_data_dir)) {
    LOG(ERROR) << "Failed to add channel mark to the user data dir ("
               << user_data_dir.value() << "). This profile will not be "
               << "automatically migrated for updated Linux packages.";
  }
}

}  // namespace sxs_linux
