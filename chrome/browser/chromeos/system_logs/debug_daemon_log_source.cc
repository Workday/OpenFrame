// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/system_logs/debug_daemon_log_source.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/debug_daemon_client.h"
#include "content/public/browser/browser_thread.h"

const char kNotAvailable[] = "<not available>";
const char kRoutesKeyName[] = "routes";
const char kNetworkStatusKeyName[] = "network-status";
const char kModemStatusKeyName[] = "modem-status";
const char kWiMaxStatusKeyName[] = "wimax-status";
const char kUserLogFileKeyName[] = "user_log_files";

namespace chromeos {

DebugDaemonLogSource::DebugDaemonLogSource(bool scrub)
    : response_(new SystemLogsResponse()),
      num_pending_requests_(0),
      scrub_(scrub),
      weak_ptr_factory_(this) {}

DebugDaemonLogSource::~DebugDaemonLogSource() {}

void DebugDaemonLogSource::Fetch(const SysLogsSourceCallback& callback) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  DCHECK(!callback.is_null());
  DCHECK(callback_.is_null());

  callback_ = callback;
  DebugDaemonClient* client = DBusThreadManager::Get()->GetDebugDaemonClient();

  client->GetRoutes(true,   // Numeric
                    false,  // No IPv6
                    base::Bind(&DebugDaemonLogSource::OnGetRoutes,
                               weak_ptr_factory_.GetWeakPtr()));
  ++num_pending_requests_;
  client->GetNetworkStatus(base::Bind(&DebugDaemonLogSource::OnGetNetworkStatus,
                                      weak_ptr_factory_.GetWeakPtr()));
  ++num_pending_requests_;
  client->GetModemStatus(base::Bind(&DebugDaemonLogSource::OnGetModemStatus,
                                    weak_ptr_factory_.GetWeakPtr()));
  ++num_pending_requests_;
  client->GetWiMaxStatus(base::Bind(&DebugDaemonLogSource::OnGetWiMaxStatus,
                                    weak_ptr_factory_.GetWeakPtr()));
  ++num_pending_requests_;
  client->GetUserLogFiles(base::Bind(&DebugDaemonLogSource::OnGetUserLogFiles,
                                     weak_ptr_factory_.GetWeakPtr()));
  ++num_pending_requests_;

  if (scrub_) {
    client->GetScrubbedLogs(base::Bind(&DebugDaemonLogSource::OnGetLogs,
                                       weak_ptr_factory_.GetWeakPtr()));
  } else {
    client->GetAllLogs(base::Bind(&DebugDaemonLogSource::OnGetLogs,
                                  weak_ptr_factory_.GetWeakPtr()));
  }
  ++num_pending_requests_;
}

void DebugDaemonLogSource::OnGetRoutes(bool succeeded,
                                       const std::vector<std::string>& routes) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  if (succeeded)
    (*response_)[kRoutesKeyName] = JoinString(routes, '\n');
  else
    (*response_)[kRoutesKeyName] = kNotAvailable;
  RequestCompleted();
}

void DebugDaemonLogSource::OnGetNetworkStatus(bool succeeded,
                                              const std::string& status) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  if (succeeded)
    (*response_)[kNetworkStatusKeyName] = status;
  else
    (*response_)[kNetworkStatusKeyName] = kNotAvailable;
  RequestCompleted();
}

void DebugDaemonLogSource::OnGetModemStatus(bool succeeded,
                                            const std::string& status) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  if (succeeded)
    (*response_)[kModemStatusKeyName] = status;
  else
    (*response_)[kModemStatusKeyName] = kNotAvailable;
  RequestCompleted();
}

void DebugDaemonLogSource::OnGetWiMaxStatus(bool succeeded,
                                            const std::string& status) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  if (succeeded)
    (*response_)[kWiMaxStatusKeyName] = status;
  else
    (*response_)[kWiMaxStatusKeyName] = kNotAvailable;
  RequestCompleted();
}

void DebugDaemonLogSource::OnGetLogs(bool /* succeeded */,
                                     const KeyValueMap& logs) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  // We ignore 'succeeded' for this callback - we want to display as much of the
  // debug info as we can even if we failed partway through parsing, and if we
  // couldn't fetch any of it, none of the fields will even appear.
  response_->insert(logs.begin(), logs.end());
  RequestCompleted();
}

void DebugDaemonLogSource::OnGetUserLogFiles(
    bool succeeded,
    const KeyValueMap& user_log_files) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (succeeded) {
    content::BrowserThread::PostBlockingPoolTaskAndReply(
        FROM_HERE,
        base::Bind(
            &DebugDaemonLogSource::ReadUserLogFiles,
            weak_ptr_factory_.GetWeakPtr(),
            user_log_files),
        base::Bind(&DebugDaemonLogSource::RequestCompleted,
                   weak_ptr_factory_.GetWeakPtr()));
  } else {
    (*response_)[kUserLogFileKeyName] = kNotAvailable;
    RequestCompleted();
  }
}

void DebugDaemonLogSource::ReadUserLogFiles(const KeyValueMap& user_log_files) {
  std::vector<Profile*> last_used = ProfileManager::GetLastOpenedProfiles();

  for (size_t i = 0; i < last_used.size(); ++i) {
    std::string profile_prefix = "Profile[" + base::UintToString(i) + "] ";
    for (KeyValueMap::const_iterator it = user_log_files.begin();
         it != user_log_files.end();
         ++it) {
      std::string key = it->first;
      std::string value;
      std::string filename = it->second;
      base::FilePath profile_dir = last_used[i]->GetPath();
      bool read_success = file_util::ReadFileToString(
          profile_dir.Append(filename), &value);

      if (read_success && !value.empty())
        (*response_)[profile_prefix + key] = value;
      else
        (*response_)[profile_prefix + filename] = kNotAvailable;
    }
  }
}

void DebugDaemonLogSource::RequestCompleted() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  DCHECK(!callback_.is_null());

  --num_pending_requests_;
  if (num_pending_requests_ > 0)
    return;
  callback_.Run(response_.get());
}

}  // namespace chromeos
