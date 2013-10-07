// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_RECOVERY_PRIVATE_RECOVERY_OPERATION_MANAGER_H_
#define CHROME_BROWSER_EXTENSIONS_API_RECOVERY_PRIVATE_RECOVERY_OPERATION_MANAGER_H_

#include <map>

#include "base/bind.h"
#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/stl_util.h"
#include "base/timer/timer.h"
#include "chrome/browser/extensions/api/profile_keyed_api_factory.h"
#include "chrome/browser/extensions/api/recovery_private/recovery_private_api.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/recovery_private.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "url/gurl.h"

namespace recovery_api = extensions::api::recovery_private;

namespace extensions {
namespace recovery {

class RecoveryOperation;

// Manages recovery operations for the current profile.  Including clean-up and
// message routing.
class RecoveryOperationManager
    : public ProfileKeyedAPI,
      public content::NotificationObserver,
      public base::SupportsWeakPtr<RecoveryOperationManager> {
 public:
  typedef base::Callback<void(bool)> StartWriteCallback;
  typedef base::Callback<void(bool)> CancelWriteCallback;
  typedef std::string ExtensionId;

  explicit RecoveryOperationManager(Profile* profile);

  virtual void Shutdown() OVERRIDE;

  // Starts a WriteFromUrl operation.
  void StartWriteFromUrl(const ExtensionId& extension_id,
                         const GURL& url,
                         scoped_ptr<std::string> hash,
                         bool saveImageAsDownload,
                         const std::string& storage_unit,
                         const StartWriteCallback& callback);

  // Starts a WriteFromFile operation.
  void StartWriteFromFile(const ExtensionId& extension_id,
                          const std::string& storage_unit_id,
                          const StartWriteCallback& callback);

  // Cancels the extensions current operation if any.
  void CancelWrite(const ExtensionId& extension_id,
                   const CancelWriteCallback& callback);

  // Callback for progress events.
  void OnProgress(const ExtensionId& extension_id,
                  recovery_api::Stage stage,
                  int progress);
  // Callback for completion events.
  void OnComplete(const ExtensionId& extension_id);

  // Callback for error events.
  // TODO: Add error codes.
  void OnError(const ExtensionId& extension_id,
               recovery_api::Stage stage,
               int progress);

  // ProfileKeyedAPI
  static ProfileKeyedAPIFactory<RecoveryOperationManager>* GetFactoryInstance();
  static RecoveryOperationManager* Get(Profile* profile);

 private:
  friend class ProfileKeyedAPIFactory<RecoveryOperationManager>;
  typedef std::map<ExtensionId, linked_ptr<RecoveryOperation> > OperationMap;

  Profile* profile_;
  OperationMap operations_;
  content::NotificationRegistrar registrar_;

  // NotificationObserver
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  virtual ~RecoveryOperationManager();

  static const char* service_name() {
    return "RecoveryOperationManager";
  }

  RecoveryOperation* GetOperation(const ExtensionId& extension_id);
  void DeleteOperation(const ExtensionId& extension_id);

  DISALLOW_COPY_AND_ASSIGN(RecoveryOperationManager);
};

} // namespace recovery
} // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_RECOVERY_PRIVATE_RECOVERY_OPERATION_MANAGER_H_
