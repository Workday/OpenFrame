// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/gcm_driver/gcm_desktop_utils.h"

#include "base/command_line.h"
#include "base/sequenced_task_runner.h"
#include "base/threading/sequenced_worker_pool.h"
#include "components/gcm_driver/gcm_client_factory.h"
#include "components/gcm_driver/gcm_driver.h"
#include "components/gcm_driver/gcm_driver_desktop.h"
#include "components/sync_driver/sync_util.h"
#include "url/gurl.h"

namespace gcm {

namespace {

const char kChannelStatusRelativePath[] = "/experimentstatus";

GCMClient::ChromePlatform GetPlatform() {
#if defined(OS_WIN)
  return GCMClient::PLATFORM_WIN;
#elif defined(OS_MACOSX)
  return GCMClient::PLATFORM_MAC;
#elif defined(OS_IOS)
  return GCMClient::PLATFORM_IOS;
#elif defined(OS_ANDROID)
  return GCMClient::PLATFORM_ANDROID;
#elif defined(OS_CHROMEOS)
  return GCMClient::PLATFORM_CROS;
#elif defined(OS_LINUX)
  return GCMClient::PLATFORM_LINUX;
#else
  // For all other platforms, return as LINUX.
  return GCMClient::PLATFORM_LINUX;
#endif
}

GCMClient::ChromeChannel GetChannel(version_info::Channel channel) {
  switch (channel) {
    case version_info::Channel::UNKNOWN:
      return GCMClient::CHANNEL_UNKNOWN;
    case version_info::Channel::CANARY:
      return GCMClient::CHANNEL_CANARY;
    case version_info::Channel::DEV:
      return GCMClient::CHANNEL_DEV;
    case version_info::Channel::BETA:
      return GCMClient::CHANNEL_BETA;
    case version_info::Channel::STABLE:
      return GCMClient::CHANNEL_STABLE;
    default:
      NOTREACHED();
      return GCMClient::CHANNEL_UNKNOWN;
  }
}

std::string GetVersion() {
  return version_info::GetVersionNumber();
}

GCMClient::ChromeBuildInfo GetChromeBuildInfo(version_info::Channel channel) {
  GCMClient::ChromeBuildInfo chrome_build_info;
  chrome_build_info.platform = GetPlatform();
  chrome_build_info.channel = GetChannel(channel);
  chrome_build_info.version = GetVersion();
  return chrome_build_info;
}

std::string GetChannelStatusRequestUrl(version_info::Channel channel) {
  GURL sync_url(GetSyncServiceURL(*base::CommandLine::ForCurrentProcess(),
                                  channel));
  return sync_url.spec() + kChannelStatusRelativePath;
}

std::string GetUserAgent(version_info::Channel channel) {
  return MakeDesktopUserAgentForSync(channel);
}

}  // namespace

scoped_ptr<GCMDriver> CreateGCMDriverDesktop(
    scoped_ptr<GCMClientFactory> gcm_client_factory,
    PrefService* prefs,
    const base::FilePath& store_path,
    const scoped_refptr<net::URLRequestContextGetter>& request_context,
    version_info::Channel channel,
    const scoped_refptr<base::SequencedTaskRunner>& ui_task_runner,
    const scoped_refptr<base::SequencedTaskRunner>& io_task_runner,
    const scoped_refptr<base::SequencedTaskRunner>& blocking_task_runner) {

  return scoped_ptr<GCMDriver>(new GCMDriverDesktop(
      gcm_client_factory.Pass(),
      GetChromeBuildInfo(channel),
      GetChannelStatusRequestUrl(channel),
      GetUserAgent(channel),
      prefs,
      store_path,
      request_context,
      ui_task_runner,
      io_task_runner,
      blocking_task_runner));
}

}  // namespace gcm
