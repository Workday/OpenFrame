// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICE_CLOUD_PRINT_CLOUD_PRINT_PROXY_BACKEND_H_
#define CHROME_SERVICE_CLOUD_PRINT_CLOUD_PRINT_PROXY_BACKEND_H_

#include <list>
#include <string>

#include "base/threading/thread.h"
#include "chrome/service/cloud_print/connector_settings.h"
#include "printing/backend/print_backend.h"

namespace base {
class DictionaryValue;
}

namespace gaia {
struct OAuthClientInfo;
}

namespace cloud_print {

// CloudPrintProxyFrontend is the interface used by CloudPrintProxyBackend to
// communicate with the entity that created it and, presumably, is interested in
// cloud print proxy related activity.
// NOTE: All methods will be invoked by a CloudPrintProxyBackend on the same
// thread used to create that CloudPrintProxyBackend.
class CloudPrintProxyFrontend {
 public:
  CloudPrintProxyFrontend() {}

  // We successfully authenticated with the cloud print server. This callback
  // allows the frontend to persist the tokens.
  virtual void OnAuthenticated(const std::string& robot_oauth_refresh_token,
                               const std::string& robot_email,
                               const std::string& user_email) = 0;
  // We have invalid/expired credentials.
  virtual void OnAuthenticationFailed() = 0;
  // The print system could not be initialized.
  virtual void OnPrintSystemUnavailable() = 0;
  // Receive auth token and list of printers.
  virtual void OnUnregisterPrinters(
      const std::string& auth_token,
      const std::list<std::string>& printer_ids) = 0;

 protected:
  // Don't delete through SyncFrontend interface.
  virtual ~CloudPrintProxyFrontend() {
  }
 private:
  DISALLOW_COPY_AND_ASSIGN(CloudPrintProxyFrontend);
};

class CloudPrintProxyBackend {
 public:
  // It is OK for print_system_settings to be NULL. In this case system should
  // use system default settings.
  CloudPrintProxyBackend(CloudPrintProxyFrontend* frontend,
                         const ConnectorSettings& settings,
                         const gaia::OAuthClientInfo& oauth_client_info,
                         bool enable_job_poll);
  ~CloudPrintProxyBackend();

  // Legacy mechanism when we have saved user credentials but no saved robot
  // credentials.
  bool InitializeWithToken(const std::string& cloud_print_token);
  // Called when we have saved robot credentials.
  bool InitializeWithRobotToken(const std::string& robot_oauth_refresh_token,
                                const std::string& robot_email);
  // Called when an external entity passed in the auth code for the robot.
  bool InitializeWithRobotAuthCode(const std::string& robot_oauth_auth_code,
                                   const std::string& robot_email);
  void Shutdown();
  void RegisterPrinters(const printing::PrinterList& printer_list);
  void UnregisterPrinters();

 private:
  // The real guts of SyncBackendHost, to keep the public client API clean.
  class Core;
  // A thread we dedicate for use to perform initialization and
  // authentication.
  base::Thread core_thread_;
  // Our core, which communicates with AuthWatcher for GAIA authentication and
  // which contains printer registration code.
  scoped_refptr<Core> core_;
  // A reference to the MessageLoop used to construct |this|, so we know how
  // to safely talk back to the SyncFrontend.
  base::MessageLoop* const frontend_loop_;
  // The frontend which is responsible for displaying UI and updating Prefs
  CloudPrintProxyFrontend* frontend_;

  friend class base::RefCountedThreadSafe<CloudPrintProxyBackend::Core>;

  DISALLOW_COPY_AND_ASSIGN(CloudPrintProxyBackend);
};

}  // namespace cloud_print

#endif  // CHROME_SERVICE_CLOUD_PRINT_CLOUD_PRINT_PROXY_BACKEND_H_
