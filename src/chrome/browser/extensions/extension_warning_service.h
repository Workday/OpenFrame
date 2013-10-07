// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_WARNING_SERVICE_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_WARNING_SERVICE_H_

#include <set>
#include <string>
#include <vector>

#include "base/observer_list.h"
#include "base/threading/non_thread_safe.h"
#include "chrome/browser/extensions/extension_warning_set.h"
#include "chrome/common/extensions/extension_set.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

// TODO(battre) Remove the Extension prefix.

class Profile;

namespace content {
class NotificationDetails;
class NotificationSource;
}

namespace extensions {

// Manages a set of warnings caused by extensions. These warnings (e.g.
// conflicting modifications of network requests by extensions, slow extensions,
// etc.) trigger a warning badge in the UI and and provide means to resolve
// them. This class must be used on the UI thread only.
class ExtensionWarningService : public content::NotificationObserver,
                                public base::NonThreadSafe {
 public:
  class Observer {
   public:
    virtual void ExtensionWarningsChanged() = 0;
  };

  // |profile| may be NULL for testing. In this case, be sure to not insert
  // any warnings.
  explicit ExtensionWarningService(Profile* profile);
  virtual ~ExtensionWarningService();

  // Clears all warnings of types contained in |types| and notifies observers
  // of the changed warnings.
  void ClearWarnings(const std::set<ExtensionWarning::WarningType>& types);

  // Returns all types of warnings effecting extension |extension_id|.
  std::set<ExtensionWarning::WarningType> GetWarningTypesAffectingExtension(
      const std::string& extension_id) const;

  // Returns all localized warnings for extension |extension_id| in |result|.
  std::vector<std::string> GetWarningMessagesForExtension(
      const std::string& extension_id) const;

  const ExtensionWarningSet& warnings() const { return warnings_; }

  // Adds a set of warnings and notifies observers if any warning is new.
  void AddWarnings(const ExtensionWarningSet& warnings);

  // Notifies the ExtensionWarningService of profile |profile_id| that new
  // |warnings| occurred and triggers a warning badge.
  static void NotifyWarningsOnUI(void* profile_id,
                                 const ExtensionWarningSet& warnings);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  void NotifyWarningsChanged();

  // Implementation for content::NotificationObserver.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Currently existing warnings.
  ExtensionWarningSet warnings_;

  content::NotificationRegistrar registrar_;

  Profile* profile_;

  ObserverList<Observer> observer_list_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_WARNING_SERVICE_H_
