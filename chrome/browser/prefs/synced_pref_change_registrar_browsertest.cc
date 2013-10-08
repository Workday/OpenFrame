// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/json/json_string_value_serializer.h"
#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/mock_configuration_policy_provider.h"
#include "chrome/browser/policy/policy_map.h"
#include "chrome/browser/prefs/synced_pref_change_registrar.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_pref_service_syncable.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_utils.h"
#include "policy/policy_constants.h"
#include "sync/api/sync_change.h"
#include "sync/api/sync_error_factory.h"
#include "sync/api/sync_error_factory_mock.h"
#include "sync/api/syncable_service.h"
#include "sync/protocol/sync.pb.h"

namespace {

using testing::AnyNumber;
using testing::Return;
using testing::_;

class TestSyncProcessorStub : public syncer::SyncChangeProcessor {
  virtual syncer::SyncError ProcessSyncChanges(
      const tracked_objects::Location& from_here,
      const syncer::SyncChangeList& change_list) OVERRIDE {
    return syncer::SyncError();
  }
};

class SyncedPrefChangeRegistrarTest : public InProcessBrowserTest {
 public:
  SyncedPrefChangeRegistrarTest() : next_sync_data_id_(0) {}
  virtual ~SyncedPrefChangeRegistrarTest() {}

  void UpdateChromePolicy(const policy::PolicyMap& policies) {
    policy_provider_.UpdateChromePolicy(policies);
    DCHECK(base::MessageLoop::current());
    base::RunLoop loop;
    loop.RunUntilIdle();
  }

  void SetBooleanPrefValueFromSync(const std::string& name, bool value) {
    std::string serialized_value;
    JSONStringValueSerializer json(&serialized_value);
    json.Serialize(base::FundamentalValue(value));

    sync_pb::EntitySpecifics specifics;
    sync_pb::PreferenceSpecifics* pref_specifics =
        specifics.mutable_preference();
    pref_specifics->set_name(name);
    pref_specifics->set_value(serialized_value);

    syncer::SyncData change_data = syncer::SyncData::CreateRemoteData(
        ++next_sync_data_id_, specifics, base::Time());
    syncer::SyncChange change(
        FROM_HERE, syncer::SyncChange::ACTION_UPDATE, change_data);

    syncer::SyncChangeList change_list;
    change_list.push_back(change);

    syncer_->ProcessSyncChanges(FROM_HERE, change_list);
  }

  void SetBooleanPrefValueFromLocal(const std::string& name, bool value) {
    prefs_->SetBoolean(name.c_str(), value);
  }

  bool GetBooleanPrefValue(const std::string& name) {
    return prefs_->GetBoolean(name.c_str());
  }

  PrefServiceSyncable* prefs() const {
    return prefs_;
  }

  SyncedPrefChangeRegistrar* registrar() const {
    return registrar_.get();
  }

 private:
  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    EXPECT_CALL(policy_provider_, IsInitializationComplete(_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(policy_provider_, RegisterPolicyDomain(_)).Times(AnyNumber());
    policy::BrowserPolicyConnector::SetPolicyProviderForTesting(
        &policy_provider_);
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    prefs_ = PrefServiceSyncable::FromProfile(browser()->profile());
    syncer_ = prefs_->GetSyncableService(syncer::PREFERENCES);
    syncer_->MergeDataAndStartSyncing(
        syncer::PREFERENCES,
        syncer::SyncDataList(),
        scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
        scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock));
    registrar_.reset(new SyncedPrefChangeRegistrar(prefs_));
  }

  virtual void CleanUpOnMainThread() OVERRIDE {
    registrar_.reset();
  }

  PrefServiceSyncable* prefs_;
  syncer::SyncableService* syncer_;
  int next_sync_data_id_;

  scoped_ptr<SyncedPrefChangeRegistrar> registrar_;
  policy::MockConfigurationPolicyProvider policy_provider_;
};

struct TestSyncedPrefObserver {
  bool last_seen_value;
  bool last_update_is_from_sync;
  bool has_been_notified;
};

void TestPrefChangeCallback(PrefService* prefs,
                            TestSyncedPrefObserver* observer,
                            const std::string& path,
                            bool from_sync) {
  observer->last_seen_value = prefs->GetBoolean(path.c_str());
  observer->last_update_is_from_sync = from_sync;
  observer->has_been_notified = true;
}

}  // namespace

IN_PROC_BROWSER_TEST_F(SyncedPrefChangeRegistrarTest,
                       DifferentiateRemoteAndLocalChanges) {
  TestSyncedPrefObserver observer = {};
  registrar()->Add(prefs::kShowHomeButton,
      base::Bind(&TestPrefChangeCallback, prefs(), &observer));

  EXPECT_FALSE(observer.has_been_notified);

  SetBooleanPrefValueFromSync(prefs::kShowHomeButton, true);
  EXPECT_TRUE(observer.has_been_notified);
  EXPECT_TRUE(GetBooleanPrefValue(prefs::kShowHomeButton));
  EXPECT_TRUE(observer.last_update_is_from_sync);
  EXPECT_TRUE(observer.last_seen_value);

  observer.has_been_notified = false;
  SetBooleanPrefValueFromLocal(prefs::kShowHomeButton, false);
  EXPECT_TRUE(observer.has_been_notified);
  EXPECT_FALSE(GetBooleanPrefValue(prefs::kShowHomeButton));
  EXPECT_FALSE(observer.last_update_is_from_sync);
  EXPECT_FALSE(observer.last_seen_value);

  observer.has_been_notified = false;
  SetBooleanPrefValueFromLocal(prefs::kShowHomeButton, true);
  EXPECT_TRUE(observer.has_been_notified);
  EXPECT_TRUE(GetBooleanPrefValue(prefs::kShowHomeButton));
  EXPECT_FALSE(observer.last_update_is_from_sync);
  EXPECT_TRUE(observer.last_seen_value);

  observer.has_been_notified = false;
  SetBooleanPrefValueFromSync(prefs::kShowHomeButton, false);
  EXPECT_TRUE(observer.has_been_notified);
  EXPECT_FALSE(GetBooleanPrefValue(prefs::kShowHomeButton));
  EXPECT_TRUE(observer.last_update_is_from_sync);
  EXPECT_FALSE(observer.last_seen_value);
}

IN_PROC_BROWSER_TEST_F(SyncedPrefChangeRegistrarTest,
                       IgnoreLocalChangesToManagedPrefs) {
  TestSyncedPrefObserver observer = {};
  registrar()->Add(prefs::kShowHomeButton,
      base::Bind(&TestPrefChangeCallback, prefs(), &observer));

  policy::PolicyMap policies;
  policies.Set(policy::key::kShowHomeButton,
               policy::POLICY_LEVEL_MANDATORY,
               policy::POLICY_SCOPE_USER,
               base::Value::CreateBooleanValue(true),
               NULL);
  UpdateChromePolicy(policies);

  EXPECT_TRUE(prefs()->IsManagedPreference(prefs::kShowHomeButton));

  SetBooleanPrefValueFromLocal(prefs::kShowHomeButton, false);
  EXPECT_FALSE(observer.has_been_notified);
  EXPECT_TRUE(GetBooleanPrefValue(prefs::kShowHomeButton));
}

IN_PROC_BROWSER_TEST_F(SyncedPrefChangeRegistrarTest,
                       IgnoreSyncChangesToManagedPrefs) {
  TestSyncedPrefObserver observer = {};
  registrar()->Add(prefs::kShowHomeButton,
      base::Bind(&TestPrefChangeCallback, prefs(), &observer));

  policy::PolicyMap policies;
  policies.Set(policy::key::kShowHomeButton,
               policy::POLICY_LEVEL_MANDATORY,
               policy::POLICY_SCOPE_USER,
               base::Value::CreateBooleanValue(true),
               NULL);
  UpdateChromePolicy(policies);

  EXPECT_TRUE(prefs()->IsManagedPreference(prefs::kShowHomeButton));
  SetBooleanPrefValueFromSync(prefs::kShowHomeButton, false);
  EXPECT_FALSE(observer.has_been_notified);
  EXPECT_TRUE(GetBooleanPrefValue(prefs::kShowHomeButton));
}
