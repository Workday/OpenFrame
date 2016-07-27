// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/syncable_prefs/pref_service_syncable.h"

#include "base/json/json_reader.h"
#include "base/json/json_string_value_serializer.h"
#include "base/json/json_writer.h"
#include "base/message_loop/message_loop.h"
#include "base/prefs/scoped_user_pref_update.h"
#include "base/strings/utf_string_conversions.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/syncable_prefs/pref_model_associator.h"
#include "components/syncable_prefs/pref_model_associator_client.h"
#include "components/syncable_prefs/testing_pref_service_syncable.h"
#include "sync/api/attachments/attachment_id.h"
#include "sync/api/sync_change.h"
#include "sync/api/sync_data.h"
#include "sync/api/sync_error_factory_mock.h"
#include "sync/api/syncable_service.h"
#include "sync/internal_api/public/attachments/attachment_service_proxy_for_test.h"
#include "sync/protocol/preference_specifics.pb.h"
#include "sync/protocol/sync.pb.h"
#include "testing/gtest/include/gtest/gtest.h"

using syncer::SyncChange;
using syncer::SyncData;

namespace syncable_prefs {

namespace {

const char kExampleUrl0[] = "http://example.com/0";
const char kExampleUrl1[] = "http://example.com/1";
const char kExampleUrl2[] = "http://example.com/2";
const char kStringPrefName[] = "string_pref_name";
const char kListPrefName[] = "new_list_pref_name";
const char kListOldPrefName[] = "list_pref_name";
const char kUnsyncedPreferenceName[] = "nonsense_pref_name";
const char kUnsyncedPreferenceDefaultValue[] = "default";
const char kDefaultCharsetPrefName[] = "default_charset";
const char kNonDefaultCharsetValue[] = "foo";
const char kDefaultCharsetValue[] = "utf-8";

class TestPrefModelAssociatorClient : public PrefModelAssociatorClient {
 public:
  TestPrefModelAssociatorClient() {}
  ~TestPrefModelAssociatorClient() override {}

  // PrefModelAssociatorClient implementation.
  bool IsMergeableListPreference(const std::string& pref_name) const override {
    return pref_name == kListPrefName;
  }

  bool IsMergeableDictionaryPreference(
      const std::string& pref_name) const override {
    return false;
  }

  bool IsMigratedPreference(const std::string& new_pref_name,
                            std::string* old_pref_name) const override {
    if (new_pref_name != kListPrefName)
      return false;
    old_pref_name->assign(kListOldPrefName);
    return true;
  }

  bool IsOldMigratedPreference(const std::string& old_pref_name,
                               std::string* new_pref_name) const override {
    if (old_pref_name != kListOldPrefName)
      return false;
    new_pref_name->assign(kListPrefName);
    return true;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestPrefModelAssociatorClient);
};

class TestSyncProcessorStub : public syncer::SyncChangeProcessor {
 public:
  explicit TestSyncProcessorStub(syncer::SyncChangeList* output)
      : output_(output), fail_next_(false) {}
  syncer::SyncError ProcessSyncChanges(
      const tracked_objects::Location& from_here,
      const syncer::SyncChangeList& change_list) override {
    if (output_)
      output_->insert(output_->end(), change_list.begin(), change_list.end());
    if (fail_next_) {
      fail_next_ = false;
      return syncer::SyncError(
          FROM_HERE, syncer::SyncError::DATATYPE_ERROR, "Error",
          syncer::PREFERENCES);
    }
    return syncer::SyncError();
  }

  void FailNextProcessSyncChanges() {
    fail_next_ = true;
  }

  syncer::SyncDataList GetAllSyncData(syncer::ModelType type) const override {
    return syncer::SyncDataList();
  }
 private:
  syncer::SyncChangeList* output_;
  bool fail_next_;
};

class PrefServiceSyncableTest : public testing::Test {
 public:
  PrefServiceSyncableTest()
      : pref_sync_service_(NULL),
        test_processor_(NULL),
        next_pref_remote_sync_node_id_(0) {}

  void SetUp() override {
    prefs_.SetPrefModelAssociatorClientForTesting(&client_);
    prefs_.registry()->RegisterStringPref(kUnsyncedPreferenceName,
                                          kUnsyncedPreferenceDefaultValue);
    prefs_.registry()->RegisterStringPref(
        kStringPrefName,
        std::string(),
        user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
    prefs_.registry()->RegisterListPref(
        kListPrefName,
        user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
    prefs_.registry()->RegisterListPref(kListOldPrefName);
    prefs_.registry()->RegisterStringPref(
        kDefaultCharsetPrefName,
        kDefaultCharsetValue,
        user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);

    pref_sync_service_ = reinterpret_cast<PrefModelAssociator*>(
        prefs_.GetSyncableService(syncer::PREFERENCES));
    ASSERT_TRUE(pref_sync_service_);
    next_pref_remote_sync_node_id_ = 0;
  }

  syncer::SyncChange MakeRemoteChange(
      int64 id,
      const std::string& name,
      const base::Value& value,
      SyncChange::SyncChangeType type) {
    std::string serialized;
    JSONStringValueSerializer json(&serialized);
    if (!json.Serialize(value))
      return syncer::SyncChange();
    sync_pb::EntitySpecifics entity;
    sync_pb::PreferenceSpecifics* pref_one = entity.mutable_preference();
    pref_one->set_name(name);
    pref_one->set_value(serialized);
    return syncer::SyncChange(
        FROM_HERE,
        type,
        syncer::SyncData::CreateRemoteData(
            id,
            entity,
            base::Time(),
            syncer::AttachmentIdList(),
            syncer::AttachmentServiceProxyForTest::Create()));
  }

  void AddToRemoteDataList(const std::string& name,
                           const base::Value& value,
                           syncer::SyncDataList* out) {
    std::string serialized;
    JSONStringValueSerializer json(&serialized);
    ASSERT_TRUE(json.Serialize(value));
    sync_pb::EntitySpecifics one;
    sync_pb::PreferenceSpecifics* pref_one = one.mutable_preference();
    pref_one->set_name(name);
    pref_one->set_value(serialized);
    out->push_back(SyncData::CreateRemoteData(
        ++next_pref_remote_sync_node_id_,
        one,
        base::Time(),
        syncer::AttachmentIdList(),
        syncer::AttachmentServiceProxyForTest::Create()));
  }

  void InitWithSyncDataTakeOutput(const syncer::SyncDataList& initial_data,
                                  syncer::SyncChangeList* output) {
    test_processor_ = new TestSyncProcessorStub(output);
    syncer::SyncMergeResult r = pref_sync_service_->MergeDataAndStartSyncing(
        syncer::PREFERENCES, initial_data,
        scoped_ptr<syncer::SyncChangeProcessor>(test_processor_),
        scoped_ptr<syncer::SyncErrorFactory>(
            new syncer::SyncErrorFactoryMock()));
    EXPECT_FALSE(r.error().IsSet());
  }

  void InitWithNoSyncData() {
    InitWithSyncDataTakeOutput(syncer::SyncDataList(), NULL);
  }

  const base::Value& GetPreferenceValue(const std::string& name) {
    const PrefService::Preference* preference =
        prefs_.FindPreference(name.c_str());
    return *preference->GetValue();
  }

  scoped_ptr<base::Value> FindValue(const std::string& name,
      const syncer::SyncChangeList& list) {
    syncer::SyncChangeList::const_iterator it = list.begin();
    for (; it != list.end(); ++it) {
      if (syncer::SyncDataLocal(it->sync_data()).GetTag() == name) {
        return base::JSONReader::Read(
            it->sync_data().GetSpecifics().preference().value());
      }
    }
    return scoped_ptr<base::Value>();
  }

  bool IsSynced(const std::string& pref_name) {
    return pref_sync_service_->registered_preferences().count(pref_name) > 0;
  }

  bool HasSyncData(const std::string& pref_name) {
    return pref_sync_service_->IsPrefSynced(pref_name);
  }

  // Returns whether a given preference name is a new name of a migrated
  // preference. Exposed here for testing.
  bool IsMigratedPreference(const char* preference_name) {
    std::string old_pref_name;
    return client_.IsMigratedPreference(preference_name, &old_pref_name);
  }

  bool IsOldMigratedPreference(const char* old_preference_name) {
    std::string new_pref_name;
    return client_.IsOldMigratedPreference(old_preference_name, &new_pref_name);
  }

  PrefService* GetPrefs() { return &prefs_; }
  TestingPrefServiceSyncable* GetTestingPrefService() { return &prefs_; }

 protected:
  TestPrefModelAssociatorClient client_;
  TestingPrefServiceSyncable prefs_;

  PrefModelAssociator* pref_sync_service_;
  TestSyncProcessorStub* test_processor_;

  // TODO(tim): Remove this by fixing AttachmentServiceProxyForTest.
  base::MessageLoop loop_;

  int next_pref_remote_sync_node_id_;
};

TEST_F(PrefServiceSyncableTest, CreatePrefSyncData) {
  prefs_.SetString(kStringPrefName, kExampleUrl0);

  const PrefService::Preference* pref = prefs_.FindPreference(kStringPrefName);
  syncer::SyncData sync_data;
  EXPECT_TRUE(pref_sync_service_->CreatePrefSyncData(pref->name(),
      *pref->GetValue(), &sync_data));
  EXPECT_EQ(std::string(kStringPrefName),
            syncer::SyncDataLocal(sync_data).GetTag());
  const sync_pb::PreferenceSpecifics& specifics(sync_data.GetSpecifics().
      preference());
  EXPECT_EQ(std::string(kStringPrefName), specifics.name());

  scoped_ptr<base::Value> value = base::JSONReader::Read(specifics.value());
  EXPECT_TRUE(pref->GetValue()->Equals(value.get()));
}

TEST_F(PrefServiceSyncableTest, ModelAssociationDoNotSyncDefaults) {
  const PrefService::Preference* pref = prefs_.FindPreference(kStringPrefName);
  EXPECT_TRUE(pref->IsDefaultValue());
  syncer::SyncChangeList out;
  InitWithSyncDataTakeOutput(syncer::SyncDataList(), &out);

  EXPECT_TRUE(IsSynced(kStringPrefName));
  EXPECT_TRUE(pref->IsDefaultValue());
  EXPECT_FALSE(FindValue(kStringPrefName, out).get());
}

TEST_F(PrefServiceSyncableTest, ModelAssociationEmptyCloud) {
  prefs_.SetString(kStringPrefName, kExampleUrl0);
  {
    ListPrefUpdate update(GetPrefs(), kListPrefName);
    base::ListValue* url_list = update.Get();
    url_list->Append(new base::StringValue(kExampleUrl0));
    url_list->Append(new base::StringValue(kExampleUrl1));
  }
  syncer::SyncChangeList out;
  InitWithSyncDataTakeOutput(syncer::SyncDataList(), &out);

  scoped_ptr<base::Value> value(FindValue(kStringPrefName, out));
  ASSERT_TRUE(value.get());
  EXPECT_TRUE(GetPreferenceValue(kStringPrefName).Equals(value.get()));
  value = FindValue(kListPrefName, out).Pass();
  ASSERT_TRUE(value.get());
  EXPECT_TRUE(GetPreferenceValue(kListPrefName).Equals(value.get()));
}

TEST_F(PrefServiceSyncableTest, ModelAssociationCloudHasData) {
  prefs_.SetString(kStringPrefName, kExampleUrl0);
  {
    ListPrefUpdate update(GetPrefs(), kListPrefName);
    base::ListValue* url_list = update.Get();
    url_list->Append(new base::StringValue(kExampleUrl0));
    url_list->Append(new base::StringValue(kExampleUrl1));
  }

  syncer::SyncDataList in;
  syncer::SyncChangeList out;
  AddToRemoteDataList(kStringPrefName, base::StringValue(kExampleUrl1), &in);
  base::ListValue urls_to_restore;
  urls_to_restore.Append(new base::StringValue(kExampleUrl1));
  urls_to_restore.Append(new base::StringValue(kExampleUrl2));
  AddToRemoteDataList(kListPrefName, urls_to_restore, &in);
  AddToRemoteDataList(kDefaultCharsetPrefName,
                      base::StringValue(kNonDefaultCharsetValue),
                      &in);
  InitWithSyncDataTakeOutput(in, &out);

  ASSERT_FALSE(FindValue(kStringPrefName, out).get());
  ASSERT_FALSE(FindValue(kDefaultCharsetPrefName, out).get());

  EXPECT_EQ(kExampleUrl1, prefs_.GetString(kStringPrefName));

  scoped_ptr<base::ListValue> expected_urls(new base::ListValue);
  expected_urls->Append(new base::StringValue(kExampleUrl1));
  expected_urls->Append(new base::StringValue(kExampleUrl2));
  expected_urls->Append(new base::StringValue(kExampleUrl0));
  scoped_ptr<base::Value> value(FindValue(kListPrefName, out));
  ASSERT_TRUE(value.get());
  EXPECT_TRUE(value->Equals(expected_urls.get()));
  EXPECT_TRUE(GetPreferenceValue(kListPrefName).Equals(expected_urls.get()));
  EXPECT_EQ(kNonDefaultCharsetValue, prefs_.GetString(kDefaultCharsetPrefName));
}

TEST_F(PrefServiceSyncableTest, ModelAssociationMigrateOldData) {
  ASSERT_TRUE(IsMigratedPreference(kListPrefName));
  ASSERT_TRUE(IsOldMigratedPreference(kListOldPrefName));

  syncer::SyncDataList in;
  syncer::SyncChangeList out;
  base::ListValue urls_to_restore;
  urls_to_restore.Append(new base::StringValue(kExampleUrl1));
  urls_to_restore.Append(new base::StringValue(kExampleUrl2));
  AddToRemoteDataList(kListOldPrefName, urls_to_restore, &in);
  InitWithSyncDataTakeOutput(in, &out);

  // Expect that the new preference data contains the old pref's values.
  scoped_ptr<base::ListValue> expected_urls(new base::ListValue);
  expected_urls->Append(new base::StringValue(kExampleUrl1));
  expected_urls->Append(new base::StringValue(kExampleUrl2));

  ASSERT_TRUE(HasSyncData(kListPrefName));
  scoped_ptr<base::Value> value(FindValue(kListPrefName, out));
  ASSERT_TRUE(value.get());
  EXPECT_TRUE(value->Equals(expected_urls.get()));
  EXPECT_TRUE(GetPreferenceValue(kListPrefName).Equals(expected_urls.get()));

  // The old preference value should be the same.
  expected_urls.reset(new base::ListValue);
  ASSERT_FALSE(FindValue(kListOldPrefName, out).get());
  EXPECT_TRUE(GetPreferenceValue(kListOldPrefName).Equals(expected_urls.get()));
}

TEST_F(PrefServiceSyncableTest, ModelAssociationCloudHasOldMigratedData) {
  ASSERT_TRUE(IsMigratedPreference(kListPrefName));
  ASSERT_TRUE(IsOldMigratedPreference(kListOldPrefName));
  prefs_.SetString(kStringPrefName, kExampleUrl0);
  {
    ListPrefUpdate update(GetPrefs(), kListPrefName);
    base::ListValue* url_list = update.Get();
    url_list->Append(new base::StringValue(kExampleUrl0));
    url_list->Append(new base::StringValue(kExampleUrl1));
  }

  syncer::SyncDataList in;
  syncer::SyncChangeList out;
  base::ListValue urls_to_restore;
  urls_to_restore.Append(new base::StringValue(kExampleUrl1));
  urls_to_restore.Append(new base::StringValue(kExampleUrl2));
  AddToRemoteDataList(kListOldPrefName, urls_to_restore, &in);
  AddToRemoteDataList(kStringPrefName, base::StringValue(kExampleUrl1), &in);
  InitWithSyncDataTakeOutput(in, &out);

  ASSERT_FALSE(FindValue(kStringPrefName, out).get());

  // Expect that the new preference data contains the merged old prefs values.
  scoped_ptr<base::ListValue> expected_urls(new base::ListValue);
  expected_urls->Append(new base::StringValue(kExampleUrl1));
  expected_urls->Append(new base::StringValue(kExampleUrl2));
  expected_urls->Append(new base::StringValue(kExampleUrl0));

  ASSERT_TRUE(HasSyncData(kListPrefName));
  scoped_ptr<base::Value> value(FindValue(kListPrefName, out));
  ASSERT_TRUE(value.get());
  EXPECT_TRUE(value->Equals(expected_urls.get()));
  EXPECT_TRUE(GetPreferenceValue(kListPrefName).Equals(expected_urls.get()));

  expected_urls.reset(new base::ListValue);
  value = FindValue(kListOldPrefName, out).Pass();
  ASSERT_TRUE(value.get());
  EXPECT_TRUE(GetPreferenceValue(kListOldPrefName).Equals(expected_urls.get()));
}

TEST_F(PrefServiceSyncableTest, ModelAssociationCloudHasNewMigratedData) {
  ASSERT_TRUE(IsMigratedPreference(kListPrefName));
  ASSERT_TRUE(IsOldMigratedPreference(kListOldPrefName));
  prefs_.SetString(kStringPrefName, kExampleUrl0);
  {
    ListPrefUpdate update(GetPrefs(), kListOldPrefName);
    base::ListValue* url_list = update.Get();
    url_list->Append(new base::StringValue(kExampleUrl0));
    url_list->Append(new base::StringValue(kExampleUrl1));
  }

  syncer::SyncDataList in;
  syncer::SyncChangeList out;
  base::ListValue urls_to_restore;
  urls_to_restore.Append(new base::StringValue(kExampleUrl1));
  urls_to_restore.Append(new base::StringValue(kExampleUrl2));
  AddToRemoteDataList(kListOldPrefName, urls_to_restore, &in);
  AddToRemoteDataList(kStringPrefName, base::StringValue(kExampleUrl1), &in);
  InitWithSyncDataTakeOutput(in, &out);

  scoped_ptr<base::Value> value(FindValue(kStringPrefName, out));
  ASSERT_FALSE(value.get());

  // Expect that the cloud data under the new migrated preference name sticks.
  scoped_ptr<base::ListValue> expected_urls(new base::ListValue);
  expected_urls->Append(new base::StringValue(kExampleUrl1));
  expected_urls->Append(new base::StringValue(kExampleUrl2));

  ASSERT_TRUE(HasSyncData(kListPrefName));
  value = FindValue(kListPrefName, out).Pass();
  ASSERT_TRUE(value.get());
  EXPECT_TRUE(value->Equals(expected_urls.get()));
  EXPECT_TRUE(GetPreferenceValue(kListPrefName).Equals(expected_urls.get()));

  // The old preference data should still be here, though not synced.
  expected_urls.reset(new base::ListValue);
  expected_urls->Append(new base::StringValue(kExampleUrl0));
  expected_urls->Append(new base::StringValue(kExampleUrl1));

  value = FindValue(kListOldPrefName, out).Pass();
  ASSERT_FALSE(value.get());
  EXPECT_TRUE(GetPreferenceValue(kListOldPrefName).Equals(expected_urls.get()));
}

TEST_F(PrefServiceSyncableTest,
       ModelAssociationCloudAddsOldAndNewMigratedData) {
  ASSERT_TRUE(IsMigratedPreference(kListPrefName));
  ASSERT_TRUE(IsOldMigratedPreference(kListOldPrefName));
  prefs_.SetString(kStringPrefName, kExampleUrl0);
  {
    ListPrefUpdate update_old(GetPrefs(), kListOldPrefName);
    base::ListValue* url_list_old = update_old.Get();
    url_list_old->Append(new base::StringValue(kExampleUrl0));
    url_list_old->Append(new base::StringValue(kExampleUrl1));
    ListPrefUpdate update(GetPrefs(), kListPrefName);
    base::ListValue* url_list = update.Get();
    url_list->Append(new base::StringValue(kExampleUrl1));
    url_list->Append(new base::StringValue(kExampleUrl2));
  }

  syncer::SyncDataList in;
  syncer::SyncChangeList out;
  AddToRemoteDataList(kStringPrefName, base::StringValue(kExampleUrl1), &in);
  InitWithSyncDataTakeOutput(in, &out);

  scoped_ptr<base::Value> value(FindValue(kStringPrefName, out));
  ASSERT_FALSE(value.get());

  // Expect that the cloud data under the new migrated preference name sticks.
  scoped_ptr<base::ListValue> expected_urls(new base::ListValue);
  expected_urls->Append(new base::StringValue(kExampleUrl1));
  expected_urls->Append(new base::StringValue(kExampleUrl2));

  ASSERT_TRUE(HasSyncData(kListPrefName));
  value = FindValue(kListPrefName, out).Pass();
  ASSERT_TRUE(value.get());
  EXPECT_TRUE(value->Equals(expected_urls.get()));
  EXPECT_TRUE(GetPreferenceValue(kListPrefName).Equals(expected_urls.get()));

  // Should not have synced in the old startup url values.
  value = FindValue(kListOldPrefName, out).Pass();
  ASSERT_FALSE(value.get());
  EXPECT_FALSE(
      GetPreferenceValue(kListOldPrefName).Equals(expected_urls.get()));
}

TEST_F(PrefServiceSyncableTest, FailModelAssociation) {
  syncer::SyncChangeList output;
  TestSyncProcessorStub* stub = new TestSyncProcessorStub(&output);
  stub->FailNextProcessSyncChanges();
  syncer::SyncMergeResult r = pref_sync_service_->MergeDataAndStartSyncing(
      syncer::PREFERENCES, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(stub),
      scoped_ptr<syncer::SyncErrorFactory>(
          new syncer::SyncErrorFactoryMock()));
  EXPECT_TRUE(r.error().IsSet());
}

TEST_F(PrefServiceSyncableTest, UpdatedPreferenceWithDefaultValue) {
  const PrefService::Preference* pref = prefs_.FindPreference(kStringPrefName);
  EXPECT_TRUE(pref->IsDefaultValue());

  syncer::SyncChangeList out;
  InitWithSyncDataTakeOutput(syncer::SyncDataList(), &out);
  out.clear();

  base::StringValue expected(kExampleUrl0);
  GetPrefs()->Set(kStringPrefName, expected);

  scoped_ptr<base::Value> actual(FindValue(kStringPrefName, out));
  ASSERT_TRUE(actual.get());
  EXPECT_TRUE(expected.Equals(actual.get()));
}

TEST_F(PrefServiceSyncableTest, UpdatedPreferenceWithValue) {
  GetPrefs()->SetString(kStringPrefName, kExampleUrl0);
  syncer::SyncChangeList out;
  InitWithSyncDataTakeOutput(syncer::SyncDataList(), &out);
  out.clear();

  base::StringValue expected(kExampleUrl1);
  GetPrefs()->Set(kStringPrefName, expected);

  scoped_ptr<base::Value> actual(FindValue(kStringPrefName, out));
  ASSERT_TRUE(actual.get());
  EXPECT_TRUE(expected.Equals(actual.get()));
}

TEST_F(PrefServiceSyncableTest, UpdatedSyncNodeActionUpdate) {
  GetPrefs()->SetString(kStringPrefName, kExampleUrl0);
  InitWithNoSyncData();

  base::StringValue expected(kExampleUrl1);
  syncer::SyncChangeList list;
  list.push_back(MakeRemoteChange(
      1, kStringPrefName, expected, SyncChange::ACTION_UPDATE));
  pref_sync_service_->ProcessSyncChanges(FROM_HERE, list);

  const base::Value& actual = GetPreferenceValue(kStringPrefName);
  EXPECT_TRUE(expected.Equals(&actual));
}

TEST_F(PrefServiceSyncableTest, UpdatedSyncNodeActionAdd) {
  InitWithNoSyncData();

  base::StringValue expected(kExampleUrl0);
  syncer::SyncChangeList list;
  list.push_back(MakeRemoteChange(
      1, kStringPrefName, expected, SyncChange::ACTION_ADD));
  pref_sync_service_->ProcessSyncChanges(FROM_HERE, list);

  const base::Value& actual = GetPreferenceValue(kStringPrefName);
  EXPECT_TRUE(expected.Equals(&actual));
  EXPECT_EQ(1U,
      pref_sync_service_->registered_preferences().count(kStringPrefName));
}

TEST_F(PrefServiceSyncableTest, UpdatedSyncNodeUnknownPreference) {
  InitWithNoSyncData();
  syncer::SyncChangeList list;
  base::StringValue expected(kExampleUrl0);
  list.push_back(MakeRemoteChange(
      1, "unknown preference", expected, SyncChange::ACTION_UPDATE));
  pref_sync_service_->ProcessSyncChanges(FROM_HERE, list);
  // Nothing interesting happens on the client when it gets an update
  // of an unknown preference.  We just should not crash.
}

TEST_F(PrefServiceSyncableTest, ManagedPreferences) {
  // Make the homepage preference managed.
  base::StringValue managed_value("http://example.com");
  prefs_.SetManagedPref(kStringPrefName, managed_value.DeepCopy());

  syncer::SyncChangeList out;
  InitWithSyncDataTakeOutput(syncer::SyncDataList(), &out);
  out.clear();

  // Changing the homepage preference should not sync anything.
  base::StringValue user_value("http://chromium..com");
  prefs_.SetUserPref(kStringPrefName, user_value.DeepCopy());
  EXPECT_TRUE(out.empty());

  // An incoming sync transaction should change the user value, not the managed
  // value.
  base::StringValue sync_value("http://crbug.com");
  syncer::SyncChangeList list;
  list.push_back(MakeRemoteChange(
      1, kStringPrefName, sync_value, SyncChange::ACTION_UPDATE));
  pref_sync_service_->ProcessSyncChanges(FROM_HERE, list);

  EXPECT_TRUE(managed_value.Equals(prefs_.GetManagedPref(kStringPrefName)));
  EXPECT_TRUE(sync_value.Equals(prefs_.GetUserPref(kStringPrefName)));
}

// List preferences have special handling at association time due to our ability
// to merge the local and sync value. Make sure the merge logic doesn't merge
// managed preferences.
TEST_F(PrefServiceSyncableTest, ManagedListPreferences) {
  // Make the list of urls to restore on startup managed.
  base::ListValue managed_value;
  managed_value.Append(new base::StringValue(kExampleUrl0));
  managed_value.Append(new base::StringValue(kExampleUrl1));
  prefs_.SetManagedPref(kListPrefName, managed_value.DeepCopy());

  // Set a cloud version.
  syncer::SyncDataList in;
  syncer::SyncChangeList out;
  base::ListValue urls_to_restore;
  urls_to_restore.Append(new base::StringValue(kExampleUrl1));
  urls_to_restore.Append(new base::StringValue(kExampleUrl2));
  AddToRemoteDataList(kListPrefName, urls_to_restore, &in);

  // Start sync and verify the synced value didn't get merged.
  InitWithSyncDataTakeOutput(in, &out);
  EXPECT_FALSE(FindValue(kListPrefName, out).get());
  out.clear();

  // Changing the user's urls to restore on startup pref should not sync
  // anything.
  base::ListValue user_value;
  user_value.Append(new base::StringValue("http://chromium.org"));
  prefs_.SetUserPref(kListPrefName, user_value.DeepCopy());
  EXPECT_FALSE(FindValue(kListPrefName, out).get());

  // An incoming sync transaction should change the user value, not the managed
  // value.
  base::ListValue sync_value;
  sync_value.Append(new base::StringValue("http://crbug.com"));
  syncer::SyncChangeList list;
  list.push_back(MakeRemoteChange(
      1, kListPrefName, sync_value,
      SyncChange::ACTION_UPDATE));
  pref_sync_service_->ProcessSyncChanges(FROM_HERE, list);

  EXPECT_TRUE(managed_value.Equals(prefs_.GetManagedPref(kListPrefName)));
  EXPECT_TRUE(sync_value.Equals(prefs_.GetUserPref(kListPrefName)));
}

TEST_F(PrefServiceSyncableTest, DynamicManagedPreferences) {
  syncer::SyncChangeList out;
  InitWithSyncDataTakeOutput(syncer::SyncDataList(), &out);
  out.clear();
  base::StringValue initial_value("http://example.com/initial");
  GetPrefs()->Set(kStringPrefName, initial_value);
  scoped_ptr<base::Value> actual(FindValue(kStringPrefName, out));
  ASSERT_TRUE(actual.get());
  EXPECT_TRUE(initial_value.Equals(actual.get()));

  // Switch kHomePage to managed and set a different value.
  base::StringValue managed_value("http://example.com/managed");
  GetTestingPrefService()->SetManagedPref(kStringPrefName,
                                          managed_value.DeepCopy());

  // The pref value should be the one dictated by policy.
  EXPECT_TRUE(managed_value.Equals(&GetPreferenceValue(kStringPrefName)));

  // Switch kHomePage back to unmanaged.
  GetTestingPrefService()->RemoveManagedPref(kStringPrefName);

  // The original value should be picked up.
  EXPECT_TRUE(initial_value.Equals(&GetPreferenceValue(kStringPrefName)));
}

TEST_F(PrefServiceSyncableTest, DynamicManagedPreferencesWithSyncChange) {
  syncer::SyncChangeList out;
  InitWithSyncDataTakeOutput(syncer::SyncDataList(), &out);
  out.clear();

  base::StringValue initial_value("http://example.com/initial");
  GetPrefs()->Set(kStringPrefName, initial_value);
  scoped_ptr<base::Value> actual(FindValue(kStringPrefName, out));
  EXPECT_TRUE(initial_value.Equals(actual.get()));

  // Switch kHomePage to managed and set a different value.
  base::StringValue managed_value("http://example.com/managed");
  GetTestingPrefService()->SetManagedPref(kStringPrefName,
                                          managed_value.DeepCopy());

  // Change the sync value.
  base::StringValue sync_value("http://example.com/sync");
  syncer::SyncChangeList list;
  list.push_back(MakeRemoteChange(
      1, kStringPrefName, sync_value, SyncChange::ACTION_UPDATE));
  pref_sync_service_->ProcessSyncChanges(FROM_HERE, list);

  // The pref value should still be the one dictated by policy.
  EXPECT_TRUE(managed_value.Equals(&GetPreferenceValue(kStringPrefName)));

  // Switch kHomePage back to unmanaged.
  GetTestingPrefService()->RemoveManagedPref(kStringPrefName);

  // Sync value should be picked up.
  EXPECT_TRUE(sync_value.Equals(&GetPreferenceValue(kStringPrefName)));
}

TEST_F(PrefServiceSyncableTest, DynamicManagedDefaultPreferences) {
  const PrefService::Preference* pref = prefs_.FindPreference(kStringPrefName);
  EXPECT_TRUE(pref->IsDefaultValue());
  syncer::SyncChangeList out;
  InitWithSyncDataTakeOutput(syncer::SyncDataList(), &out);

  EXPECT_TRUE(IsSynced(kStringPrefName));
  EXPECT_TRUE(pref->IsDefaultValue());
  EXPECT_FALSE(FindValue(kStringPrefName, out).get());
  out.clear();

  // Switch kHomePage to managed and set a different value.
  base::StringValue managed_value("http://example.com/managed");
  GetTestingPrefService()->SetManagedPref(kStringPrefName,
                                          managed_value.DeepCopy());
  // The pref value should be the one dictated by policy.
  EXPECT_TRUE(managed_value.Equals(&GetPreferenceValue(kStringPrefName)));
  EXPECT_FALSE(pref->IsDefaultValue());
  // There should be no synced value.
  EXPECT_FALSE(FindValue(kStringPrefName, out).get());
  // Switch kHomePage back to unmanaged.
  GetTestingPrefService()->RemoveManagedPref(kStringPrefName);
  // The original value should be picked up.
  EXPECT_TRUE(pref->IsDefaultValue());
  // There should still be no synced value.
  EXPECT_FALSE(FindValue(kStringPrefName, out).get());
}

TEST_F(PrefServiceSyncableTest, DeletePreference) {
  prefs_.SetString(kStringPrefName, kExampleUrl0);
  const PrefService::Preference* pref = prefs_.FindPreference(kStringPrefName);
  EXPECT_FALSE(pref->IsDefaultValue());

  InitWithNoSyncData();

  scoped_ptr<base::Value> null_value = base::Value::CreateNullValue();
  syncer::SyncChangeList list;
  list.push_back(MakeRemoteChange(
      1, kStringPrefName, *null_value, SyncChange::ACTION_DELETE));
  pref_sync_service_->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_TRUE(pref->IsDefaultValue());
}

}  // namespace

}  // namespace syncable_prefs
