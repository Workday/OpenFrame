// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/service/cloud_print/cloud_print_proxy.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/path_service.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/values.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/cloud_print/cloud_print_constants.h"
#include "chrome/common/cloud_print/cloud_print_proxy_info.h"
#include "chrome/common/pref_names.h"
#include "chrome/service/cloud_print/print_system.h"
#include "chrome/service/service_process.h"
#include "chrome/service/service_process_prefs.h"
#include "google_apis/gaia/gaia_oauth_client.h"
#include "google_apis/google_api_keys.h"
#include "url/gurl.h"

namespace {

void LaunchBrowserProcessWithSwitch(const std::string& switch_string) {
  DCHECK(g_service_process->io_thread()->message_loop_proxy()->
      BelongsToCurrentThread());
  base::FilePath exe_path;
  PathService::Get(base::FILE_EXE, &exe_path);
  if (exe_path.empty()) {
    NOTREACHED() << "Unable to get browser process binary name.";
  }
  CommandLine cmd_line(exe_path);

  const CommandLine& process_command_line = *CommandLine::ForCurrentProcess();
  base::FilePath user_data_dir =
      process_command_line.GetSwitchValuePath(switches::kUserDataDir);
  if (!user_data_dir.empty())
    cmd_line.AppendSwitchPath(switches::kUserDataDir, user_data_dir);
  cmd_line.AppendSwitch(switch_string);

#if defined(OS_POSIX) && !defined(OS_MACOSX)
  base::ProcessHandle pid = 0;
  base::LaunchProcess(cmd_line, base::LaunchOptions(), &pid);
  base::EnsureProcessGetsReaped(pid);
#else
  base::LaunchOptions launch_options;
#if defined(OS_WIN)
  launch_options.force_breakaway_from_job_ = true;
#endif  // OS_WIN
  base::LaunchProcess(cmd_line, launch_options, NULL);
#endif
}

void CheckCloudPrintProxyPolicyInBrowser() {
  LaunchBrowserProcessWithSwitch(switches::kCheckCloudPrintConnectorPolicy);
}

}  // namespace

namespace cloud_print {

CloudPrintProxy::CloudPrintProxy()
    : service_prefs_(NULL),
      client_(NULL),
      enabled_(false) {
}

CloudPrintProxy::~CloudPrintProxy() {
  DCHECK(CalledOnValidThread());
  ShutdownBackend();
}

void CloudPrintProxy::Initialize(ServiceProcessPrefs* service_prefs,
                                 Client* client) {
  DCHECK(CalledOnValidThread());
  service_prefs_ = service_prefs;
  client_ = client;
}

void CloudPrintProxy::EnableForUser() {
  DCHECK(CalledOnValidThread());
  if (!CreateBackend())
    return;
  DCHECK(backend_.get());
  // Read persisted robot credentials because we may decide to reuse it if the
  // passed in LSID belongs the same user.
  std::string robot_refresh_token = service_prefs_->GetString(
      prefs::kCloudPrintRobotRefreshToken, std::string());
  std::string robot_email =
      service_prefs_->GetString(prefs::kCloudPrintRobotEmail, std::string());
  user_email_ = service_prefs_->GetString(prefs::kCloudPrintEmail, user_email_);

  // See if we have persisted robot credentials.
  if (!robot_refresh_token.empty()) {
    DCHECK(!robot_email.empty());
    backend_->InitializeWithRobotToken(robot_refresh_token, robot_email);
  } else {
    // Finally see if we have persisted user credentials (legacy case).
    std::string cloud_print_token =
        service_prefs_->GetString(prefs::kCloudPrintAuthToken, std::string());
    DCHECK(!cloud_print_token.empty());
    backend_->InitializeWithToken(cloud_print_token);
  }
  if (client_) {
    client_->OnCloudPrintProxyEnabled(true);
  }
}

void CloudPrintProxy::EnableForUserWithRobot(
    const std::string& robot_auth_code,
    const std::string& robot_email,
    const std::string& user_email,
    const base::DictionaryValue& user_settings) {
  DCHECK(CalledOnValidThread());

  ShutdownBackend();
  std::string proxy_id(
      service_prefs_->GetString(prefs::kCloudPrintProxyId, std::string()));
  service_prefs_->RemovePref(prefs::kCloudPrintRoot);
  if (!proxy_id.empty()) {
    // Keep only proxy id;
    service_prefs_->SetString(prefs::kCloudPrintProxyId, proxy_id);
  }
  service_prefs_->SetValue(prefs::kCloudPrintUserSettings,
                           user_settings.DeepCopy());
  service_prefs_->WritePrefs();

  if (!CreateBackend())
    return;
  DCHECK(backend_.get());
  user_email_ = user_email;
  backend_->InitializeWithRobotAuthCode(robot_auth_code, robot_email);
  if (client_) {
    client_->OnCloudPrintProxyEnabled(true);
  }
}

bool CloudPrintProxy::CreateBackend() {
  DCHECK(CalledOnValidThread());
  if (backend_.get())
    return false;

  settings_.InitFrom(service_prefs_);

  // By default we don't poll for jobs when we lose XMPP connection. But this
  // behavior can be overridden by a preference.
  bool enable_job_poll =
    service_prefs_->GetBoolean(prefs::kCloudPrintEnableJobPoll, false);

  gaia::OAuthClientInfo oauth_client_info;
  oauth_client_info.client_id =
    google_apis::GetOAuth2ClientID(google_apis::CLIENT_CLOUD_PRINT);
  oauth_client_info.client_secret =
    google_apis::GetOAuth2ClientSecret(google_apis::CLIENT_CLOUD_PRINT);
  oauth_client_info.redirect_uri = "oob";
  backend_.reset(new CloudPrintProxyBackend(this, settings_, oauth_client_info,
                                            enable_job_poll));
  return true;
}

void CloudPrintProxy::UnregisterPrintersAndDisableForUser() {
  DCHECK(CalledOnValidThread());
  if (backend_.get()) {
    // Try getting auth and printers info from the backend.
    // We'll get notified in this case.
    backend_->UnregisterPrinters();
  } else {
    // If no backend avaialble, disable connector immidiately.
    DisableForUser();
  }
}

void CloudPrintProxy::DisableForUser() {
  DCHECK(CalledOnValidThread());
  user_email_.clear();
  enabled_ = false;
  if (client_) {
    client_->OnCloudPrintProxyDisabled(true);
  }
  ShutdownBackend();
}

void CloudPrintProxy::GetProxyInfo(CloudPrintProxyInfo* info) {
  info->enabled = enabled_;
  info->email.clear();
  if (enabled_)
    info->email = user_email();
  info->proxy_id = settings_.proxy_id();
  // If the Cloud Print service is not enabled, we may need to read the old
  // value of proxy_id from prefs.
  if (info->proxy_id.empty())
    info->proxy_id =
        service_prefs_->GetString(prefs::kCloudPrintProxyId, std::string());
}

void CloudPrintProxy::CheckCloudPrintProxyPolicy() {
  g_service_process->io_thread()->message_loop_proxy()->PostTask(
      FROM_HERE, base::Bind(&CheckCloudPrintProxyPolicyInBrowser));
}

void CloudPrintProxy::OnAuthenticated(
    const std::string& robot_oauth_refresh_token,
    const std::string& robot_email,
    const std::string& user_email) {
  DCHECK(CalledOnValidThread());
  service_prefs_->SetString(prefs::kCloudPrintRobotRefreshToken,
                            robot_oauth_refresh_token);
  service_prefs_->SetString(prefs::kCloudPrintRobotEmail,
                            robot_email);
  // If authenticating from a robot, the user email will be empty.
  if (!user_email.empty()) {
    user_email_ = user_email;
  }
  service_prefs_->SetString(prefs::kCloudPrintEmail, user_email_);
  enabled_ = true;
  DCHECK(!user_email_.empty());
  service_prefs_->WritePrefs();
  // When this switch used we don't want connector continue running, we just
  // need authentication.
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kCloudPrintSetupProxy)) {
    ShutdownBackend();
    if (client_) {
      client_->OnCloudPrintProxyDisabled(false);
    }
  }
}

void CloudPrintProxy::OnAuthenticationFailed() {
  DCHECK(CalledOnValidThread());
  // Don't disable permanently. Could be just connection issue.
  ShutdownBackend();
  if (client_) {
    client_->OnCloudPrintProxyDisabled(false);
  }
}

void CloudPrintProxy::OnPrintSystemUnavailable() {
  // If the print system is unavailable, we want to shutdown the proxy and
  // disable it non-persistently.
  ShutdownBackend();
  if (client_) {
    client_->OnCloudPrintProxyDisabled(false);
  }
}

void CloudPrintProxy::OnUnregisterPrinters(
    const std::string& auth_token,
    const std::list<std::string>& printer_ids) {
  ShutdownBackend();
  wipeout_.reset(new CloudPrintWipeout(this, settings_.server_url()));
  wipeout_->UnregisterPrinters(auth_token, printer_ids);
}

void CloudPrintProxy::OnUnregisterPrintersComplete() {
  wipeout_.reset();
  // Finish disabling cloud print for this user.
  DisableForUser();
}

void CloudPrintProxy::ShutdownBackend() {
  DCHECK(CalledOnValidThread());
  if (backend_.get())
    backend_->Shutdown();
  backend_.reset();
}

}  // namespace cloud_print
