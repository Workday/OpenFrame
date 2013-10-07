// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_RULES_REGISTRY_WITH_CACHE_H__
#define CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_RULES_REGISTRY_WITH_CACHE_H__

#include "chrome/browser/extensions/api/declarative/rules_registry.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/callback_forward.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "extensions/common/one_shot_event.h"

class Profile;

namespace base {
class Value;
}  // namespace base

namespace extensions {

// A base class for RulesRegistries that takes care of storing the
// RulesRegistry::Rule objects. It contains all the methods that need to run on
// the registry thread; methods that need to run on the UI thread are separated
// in the RuleStorageOnUI object.
class RulesRegistryWithCache : public RulesRegistry {
 public:
  // RuleStorageOnUI implements the part of the RulesRegistry which works on
  // the UI thread. It should only be used on the UI thread. It gets created
  // by the RulesRegistry, but right after that it changes owner to the
  // RulesRegistryService, and is deleted by the service.
  // If |log_storage_init_delay| is set, the delay caused by loading and
  // registering rules on initialization will be logged with UMA.
  class RuleStorageOnUI : public content::NotificationObserver {
   public:
    // This key is used to store in preferences whether there are some
    // declarative rules stored in the rule store.
    static const char kRulesStoredKey[];

    RuleStorageOnUI(Profile* profile,
                    const std::string& storage_key,
                    content::BrowserThread::ID rules_registry_thread,
                    base::WeakPtr<RulesRegistryWithCache> registry,
                    bool log_storage_init_delay);

    virtual ~RuleStorageOnUI();

    // Initialize the storage functionality.
    void Init();

    void WriteToStorage(const std::string& extension_id,
                        scoped_ptr<base::Value> value);

    base::WeakPtr<RuleStorageOnUI> GetWeakPtr() {
      DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
      return weak_ptr_factory_.GetWeakPtr();
    }

   private:
    FRIEND_TEST_ALL_PREFIXES(RulesRegistryWithCacheTest,
                             DeclarativeRulesStored);

    // NotificationObserver
    virtual void Observe(int type,
                         const content::NotificationSource& source,
                         const content::NotificationDetails& details) OVERRIDE;

    // Check if we are done reading all data from storage on startup, and notify
    // the RulesRegistry on its thread if so. The notification is delivered
    // exactly once.
    void CheckIfReady();

    // Read/write a list of rules serialized to Values.
    void ReadFromStorage(const std::string& extension_id);
    void ReadFromStorageCallback(const std::string& extension_id,
                                 scoped_ptr<base::Value> value);

    // Check the preferences whether the extension with |extension_id| has some
    // rules stored on disk. If this information is not in the preferences, true
    // is returned as a safe default value.
    bool GetDeclarativeRulesStored(const std::string& extension_id) const;
    // Modify the preference to |rules_stored|.
    void SetDeclarativeRulesStored(const std::string& extension_id,
                                   bool rules_stored);

    content::NotificationRegistrar registrar_;

    Profile* profile_;

    // The key under which rules are stored.
    const std::string storage_key_;

    // A set of extension IDs that have rules we are reading from storage.
    std::set<std::string> waiting_for_extensions_;

    // We measure the time spent on loading rules on init. The result is logged
    // with UMA once per each RuleStorageOnUI instance, unless in Incognito.
    base::Time storage_init_time_;
    bool log_storage_init_delay_;

    // Weak pointer to post tasks to the owning rules registry.
    const base::WeakPtr<RulesRegistryWithCache> registry_;

    // The thread |registry_| lives on.
    const content::BrowserThread::ID rules_registry_thread_;

    // We notified the RulesRegistry that the rules are loaded.
    bool notified_registry_;

    // Use this factory to generate weak pointers bound to the UI thread.
    base::WeakPtrFactory<RuleStorageOnUI> weak_ptr_factory_;
  };

  // After the RuleStorageOnUI object (the part of the registry which runs on
  // the UI thread) is created, a pointer to it is passed to |*ui_part|.
  // If |log_storage_init_delay| is set, the delay caused by loading and
  // registering rules on initialization will be logged with UMA.
  // In tests, |profile| and |ui_part| can be NULL (at the same time). In that
  // case the storage functionality disabled (no RuleStorageOnUI object
  // created) and the |log_storage_init_delay| flag is ignored.
  RulesRegistryWithCache(Profile* profile,
                         const char* event_name,
                         content::BrowserThread::ID owner_thread,
                         bool log_storage_init_delay,
                         scoped_ptr<RuleStorageOnUI>* ui_part);

  const OneShotEvent& ready() const {
    return ready_;
  }

  // RulesRegistry implementation:
  virtual std::string AddRules(
      const std::string& extension_id,
      const std::vector<linked_ptr<RulesRegistry::Rule> >& rules) OVERRIDE;
  virtual std::string RemoveRules(
      const std::string& extension_id,
      const std::vector<std::string>& rule_identifiers) OVERRIDE;
  virtual std::string RemoveAllRules(
      const std::string& extension_id) OVERRIDE;
  virtual std::string GetRules(
      const std::string& extension_id,
      const std::vector<std::string>& rule_identifiers,
      std::vector<linked_ptr<RulesRegistry::Rule> >* out) OVERRIDE;
  virtual std::string GetAllRules(
      const std::string& extension_id,
      std::vector<linked_ptr<RulesRegistry::Rule> >* out) OVERRIDE;
  virtual void OnExtensionUnloaded(const std::string& extension_id) OVERRIDE;

 protected:
  virtual ~RulesRegistryWithCache();

  // These functions need to provide the same functionality as their
  // RulesRegistry counterparts. They need to be atomic.
  virtual std::string AddRulesImpl(
      const std::string& extension_id,
      const std::vector<linked_ptr<RulesRegistry::Rule> >& rules) = 0;
  virtual std::string RemoveRulesImpl(
      const std::string& extension_id,
      const std::vector<std::string>& rule_identifiers) = 0;
  virtual std::string RemoveAllRulesImpl(
      const std::string& extension_id) = 0;

 private:
  typedef std::string ExtensionId;
  typedef std::string RuleId;
  typedef std::pair<ExtensionId, RuleId> RulesDictionaryKey;
  typedef std::map<RulesDictionaryKey, linked_ptr<RulesRegistry::Rule> >
      RulesDictionary;
  enum ProcessChangedRulesState {
    // ProcessChangedRules can never be called, |storage_on_ui_| is NULL.
    NEVER_PROCESS,
    // A task to call ProcessChangedRules is scheduled for future execution.
    SCHEDULED_FOR_PROCESSING,
    // No task to call ProcessChangedRules is scheduled yet, but it is possible
    // to schedule one.
    NOT_SCHEDULED_FOR_PROCESSING
  };

  // Common processing after extension's rules have changed.
  void ProcessChangedRules(const std::string& extension_id);

  // Calls ProcessChangedRules if |process_changed_rules_requested_| ==
  // NOT_SCHEDULED_FOR_PROCESSING.
  void MaybeProcessChangedRules(const std::string& extension_id);

  // Process the callbacks once the registry gets ready.
  void MarkReady(base::Time storage_init_time);

  // Deserialize the rules from the given Value object and add them to the
  // RulesRegistry.
  void DeserializeAndAddRules(const std::string& extension_id,
                              scoped_ptr<base::Value> rules);


  RulesDictionary rules_;

  // Signaled when we have finished reading from storage for all extensions that
  // are loaded on startup.
  OneShotEvent ready_;

  // The factory needs to be declared before |storage_on_ui_|, so that it can
  // produce a pointer as a construction argument for |storage_on_ui_|.
  base::WeakPtrFactory<RulesRegistryWithCache> weak_ptr_factory_;

  // |storage_on_ui_| is owned by the registry service. If |storage_on_ui_| is
  // NULL, then the storage functionality is disabled (this is used in tests).
  // This registry cannot own |storage_on_ui_| because during the time after
  // rules registry service shuts down on UI thread, and the registry is
  // destroyed on its thread, the use of the |storage_on_ui_| would not be
  // safe. The registry only ever associates with one RuleStorageOnUI instance.
  const base::WeakPtr<RuleStorageOnUI> storage_on_ui_;

  ProcessChangedRulesState process_changed_rules_requested_;

  DISALLOW_COPY_AND_ASSIGN(RulesRegistryWithCache);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_DECLARATIVE_RULES_REGISTRY_WITH_CACHE_H__
