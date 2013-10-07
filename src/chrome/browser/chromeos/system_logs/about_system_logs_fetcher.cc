// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/system_logs/about_system_logs_fetcher.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "chrome/browser/chromeos/system_logs/chrome_internal_log_source.h"
#include "chrome/browser/chromeos/system_logs/command_line_log_source.h"
#include "chrome/browser/chromeos/system_logs/dbus_log_source.h"
#include "chrome/browser/chromeos/system_logs/debug_daemon_log_source.h"
#include "chrome/browser/chromeos/system_logs/lsb_release_log_source.h"
#include "chrome/browser/chromeos/system_logs/memory_details_log_source.h"
#include "chrome/browser/chromeos/system_logs/network_event_log_source.h"
#include "chrome/browser/chromeos/system_logs/touch_log_source.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace chromeos {

AboutSystemLogsFetcher::AboutSystemLogsFetcher() {
  // Debug Daemon data source.
  const bool scrub_data = false;
  data_sources_.push_back(new DebugDaemonLogSource(scrub_data));

  // Chrome data sources.
  data_sources_.push_back(new ChromeInternalLogSource());
  data_sources_.push_back(new CommandLineLogSource());
  data_sources_.push_back(new DBusLogSource());
  data_sources_.push_back(new LsbReleaseLogSource());
  data_sources_.push_back(new MemoryDetailsLogSource());
  data_sources_.push_back(new NetworkEventLogSource());
  data_sources_.push_back(new TouchLogSource());

  num_pending_requests_ = data_sources_.size();
}

AboutSystemLogsFetcher::~AboutSystemLogsFetcher() {
}

}  // namespace chromeos
