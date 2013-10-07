// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/search_engines/search_terms_data.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_prepopulate_data.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/search_engines/template_url_service_test_util.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/testing_pref_service_syncable.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/notification_service.h"
#include "extensions/common/constants.h"
#include "net/base/net_util.h"
#include "sync/api/sync_error_factory.h"
#include "sync/api/sync_error_factory_mock.h"
#include "sync/protocol/search_engine_specifics.pb.h"
#include "sync/protocol/sync.pb.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Time;

namespace {

// Extract the GUID from a search engine syncer::SyncData.
std::string GetGUID(const syncer::SyncData& sync_data) {
  return sync_data.GetSpecifics().search_engine().sync_guid();
}

// Extract the URL from a search engine syncer::SyncData.
std::string GetURL(const syncer::SyncData& sync_data) {
  return sync_data.GetSpecifics().search_engine().url();
}

// Extract the keyword from a search engine syncer::SyncData.
std::string GetKeyword(const syncer::SyncData& sync_data) {
  return sync_data.GetSpecifics().search_engine().keyword();
}

// Much like TemplateURLService::CreateSyncDataFromTemplateURL(), but allows the
// caller to override the keyword, URL, or GUID fields with empty strings, in
// order to create custom data that should be handled specially when synced to a
// client.
syncer::SyncData CreateCustomSyncData(const TemplateURL& turl,
                              bool autogenerate_keyword,
                              const std::string& url,
                              const std::string& sync_guid) {
  sync_pb::EntitySpecifics specifics;
  sync_pb::SearchEngineSpecifics* se_specifics =
      specifics.mutable_search_engine();
  se_specifics->set_short_name(UTF16ToUTF8(turl.short_name()));
  se_specifics->set_keyword(
      autogenerate_keyword ? std::string() : UTF16ToUTF8(turl.keyword()));
  se_specifics->set_favicon_url(turl.favicon_url().spec());
  se_specifics->set_url(url);
  se_specifics->set_safe_for_autoreplace(turl.safe_for_autoreplace());
  se_specifics->set_originating_url(turl.originating_url().spec());
  se_specifics->set_date_created(turl.date_created().ToInternalValue());
  se_specifics->set_input_encodings(JoinString(turl.input_encodings(), ';'));
  se_specifics->set_show_in_default_list(turl.show_in_default_list());
  se_specifics->set_suggestions_url(turl.suggestions_url());
  se_specifics->set_prepopulate_id(turl.prepopulate_id());
  se_specifics->set_autogenerate_keyword(autogenerate_keyword);
  se_specifics->set_instant_url(turl.instant_url());
  se_specifics->set_last_modified(turl.last_modified().ToInternalValue());
  se_specifics->set_sync_guid(sync_guid);
  return syncer::SyncData::CreateLocalData(turl.sync_guid(),  // Must be valid!
                                   se_specifics->keyword(), specifics);
}


// TestChangeProcessor --------------------------------------------------------

// Dummy SyncChangeProcessor used to help review what SyncChanges are pushed
// back up to Sync.
class TestChangeProcessor : public syncer::SyncChangeProcessor {
 public:
  TestChangeProcessor();
  virtual ~TestChangeProcessor();

  // Store a copy of all the changes passed in so we can examine them later.
  virtual syncer::SyncError ProcessSyncChanges(
      const tracked_objects::Location& from_here,
      const syncer::SyncChangeList& change_list) OVERRIDE;

  bool contains_guid(const std::string& guid) const {
    return change_map_.count(guid) != 0;
  }

  syncer::SyncChange change_for_guid(const std::string& guid) const {
    DCHECK(contains_guid(guid));
    return change_map_.find(guid)->second;
  }

  size_t change_list_size() { return change_map_.size(); }

  void set_erroneous(bool erroneous) { erroneous_ = erroneous; }

 private:
  // Track the changes received in ProcessSyncChanges.
  std::map<std::string, syncer::SyncChange> change_map_;
  bool erroneous_;

  DISALLOW_COPY_AND_ASSIGN(TestChangeProcessor);
};

TestChangeProcessor::TestChangeProcessor() : erroneous_(false) {
}

TestChangeProcessor::~TestChangeProcessor() {
}

syncer::SyncError TestChangeProcessor::ProcessSyncChanges(
    const tracked_objects::Location& from_here,
    const syncer::SyncChangeList& change_list) {
  if (erroneous_)
    return syncer::SyncError(
        FROM_HERE,
        syncer::SyncError::DATATYPE_ERROR,
        "Some error.",
        syncer::SEARCH_ENGINES);

  change_map_.erase(change_map_.begin(), change_map_.end());
  for (syncer::SyncChangeList::const_iterator iter = change_list.begin();
      iter != change_list.end(); ++iter)
    change_map_[GetGUID(iter->sync_data())] = *iter;
  return syncer::SyncError();
}


// SyncChangeProcessorDelegate ------------------------------------------------

class SyncChangeProcessorDelegate : public syncer::SyncChangeProcessor {
 public:
  explicit SyncChangeProcessorDelegate(syncer::SyncChangeProcessor* recipient);
  virtual ~SyncChangeProcessorDelegate();

  // syncer::SyncChangeProcessor implementation.
  virtual syncer::SyncError ProcessSyncChanges(
      const tracked_objects::Location& from_here,
      const syncer::SyncChangeList& change_list) OVERRIDE;

 private:
  // The recipient of all sync changes.
  syncer::SyncChangeProcessor* recipient_;

  DISALLOW_COPY_AND_ASSIGN(SyncChangeProcessorDelegate);
};

SyncChangeProcessorDelegate::SyncChangeProcessorDelegate(
    syncer::SyncChangeProcessor* recipient)
    : recipient_(recipient) {
  DCHECK(recipient_);
}

SyncChangeProcessorDelegate::~SyncChangeProcessorDelegate() {
}

syncer::SyncError SyncChangeProcessorDelegate::ProcessSyncChanges(
    const tracked_objects::Location& from_here,
    const syncer::SyncChangeList& change_list) {
  return recipient_->ProcessSyncChanges(from_here, change_list);
}

}  // namespace


// TemplateURLServiceSyncTest -------------------------------------------------

class TemplateURLServiceSyncTest : public testing::Test {
 public:
  typedef TemplateURLService::SyncDataMap SyncDataMap;

  TemplateURLServiceSyncTest();

  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  TemplateURLService* model() { return test_util_a_.model(); }
  // For readability, we redefine an accessor for Model A for use in tests that
  // involve syncing two models.
  TemplateURLService* model_a() { return test_util_a_.model(); }
  TemplateURLService* model_b() { return model_b_.get(); }
  TestingProfile* profile_a() { return test_util_a_.profile(); }
  TestChangeProcessor* processor() { return sync_processor_.get(); }
  scoped_ptr<syncer::SyncChangeProcessor> PassProcessor();
  scoped_ptr<syncer::SyncErrorFactory> CreateAndPassSyncErrorFactory();

  // Create a TemplateURL with some test values. The caller owns the returned
  // TemplateURL*.
  TemplateURL* CreateTestTemplateURL(const string16& keyword,
                                     const std::string& url,
                                     const std::string& guid = std::string(),
                                     time_t last_mod = 100,
                                     bool safe_for_autoreplace = false,
                                     bool created_by_policy = false) const;

  // Verifies the two TemplateURLs are equal.
  // TODO(stevet): Share this with TemplateURLServiceTest.
  void AssertEquals(const TemplateURL& expected,
                    const TemplateURL& actual) const;

  // Expect that two syncer::SyncDataLists have equal contents, in terms of the
  // sync_guid, keyword, and url fields.
  void AssertEquals(const syncer::SyncDataList& data1,
                    const syncer::SyncDataList& data2) const;

  // Convenience helper for creating SyncChanges. Takes ownership of |turl|.
  syncer::SyncChange CreateTestSyncChange(
      syncer::SyncChange::SyncChangeType type,
      TemplateURL* turl) const;

  // Helper that creates some initial sync data. We cheat a little by specifying
  // GUIDs for easy identification later. We also make the last_modified times
  // slightly older than CreateTestTemplateURL's default, to test conflict
  // resolution.
  syncer::SyncDataList CreateInitialSyncData() const;

  // Syntactic sugar.
  TemplateURL* Deserialize(const syncer::SyncData& sync_data);

  // Creates a new TemplateURL copying the fields of |turl| but replacing
  // the |url| and |guid| and initializing the date_created and last_modified
  // timestamps to a default value of 100. The caller owns the returned
  // TemplateURL*.
  TemplateURL* CopyTemplateURL(const TemplateURL* turl,
                               const std::string& url,
                               const std::string& guid);

 protected:
  // We keep two TemplateURLServices to test syncing between them.
  TemplateURLServiceTestUtil test_util_a_;
  scoped_ptr<TestingProfile> profile_b_;
  scoped_ptr<TemplateURLService> model_b_;

  // Our dummy ChangeProcessor used to inspect changes pushed to Sync.
  scoped_ptr<TestChangeProcessor> sync_processor_;
  scoped_ptr<SyncChangeProcessorDelegate> sync_processor_delegate_;

  DISALLOW_COPY_AND_ASSIGN(TemplateURLServiceSyncTest);
};

TemplateURLServiceSyncTest::TemplateURLServiceSyncTest()
    : sync_processor_(new TestChangeProcessor),
      sync_processor_delegate_(new SyncChangeProcessorDelegate(
          sync_processor_.get())) {
}

void TemplateURLServiceSyncTest::SetUp() {
  test_util_a_.SetUp();
  // Use ChangeToLoadState() instead of VerifyLoad() so we don't actually pull
  // in the prepopulate data, which the sync tests don't care about (and would
  // just foul them up).
  test_util_a_.ChangeModelToLoadState();
  profile_b_.reset(new TestingProfile);
  TemplateURLServiceFactory::GetInstance()->RegisterUserPrefsOnBrowserContext(
      profile_b_.get());
  model_b_.reset(new TemplateURLService(profile_b_.get()));
  model_b_->Load();
}

void TemplateURLServiceSyncTest::TearDown() {
  test_util_a_.TearDown();
}

scoped_ptr<syncer::SyncChangeProcessor>
TemplateURLServiceSyncTest::PassProcessor() {
  return sync_processor_delegate_.PassAs<syncer::SyncChangeProcessor>();
}

scoped_ptr<syncer::SyncErrorFactory> TemplateURLServiceSyncTest::
    CreateAndPassSyncErrorFactory() {
  return scoped_ptr<syncer::SyncErrorFactory>(
      new syncer::SyncErrorFactoryMock());
}

TemplateURL* TemplateURLServiceSyncTest::CreateTestTemplateURL(
    const string16& keyword,
    const std::string& url,
    const std::string& guid,
    time_t last_mod,
    bool safe_for_autoreplace,
    bool created_by_policy) const {
  TemplateURLData data;
  data.short_name = ASCIIToUTF16("unittest");
  data.SetKeyword(keyword);
  data.SetURL(url);
  data.favicon_url = GURL("http://favicon.url");
  data.safe_for_autoreplace = safe_for_autoreplace;
  data.date_created = Time::FromTimeT(100);
  data.last_modified = Time::FromTimeT(last_mod);
  data.created_by_policy = created_by_policy;
  data.prepopulate_id = 999999;
  if (!guid.empty())
    data.sync_guid = guid;
  return new TemplateURL(NULL, data);
}

void TemplateURLServiceSyncTest::AssertEquals(const TemplateURL& expected,
                                              const TemplateURL& actual) const {
  ASSERT_EQ(expected.short_name(), actual.short_name());
  ASSERT_EQ(expected.keyword(), actual.keyword());
  ASSERT_EQ(expected.url(), actual.url());
  ASSERT_EQ(expected.suggestions_url(), actual.suggestions_url());
  ASSERT_EQ(expected.favicon_url(), actual.favicon_url());
  ASSERT_EQ(expected.show_in_default_list(), actual.show_in_default_list());
  ASSERT_EQ(expected.safe_for_autoreplace(), actual.safe_for_autoreplace());
  ASSERT_EQ(expected.input_encodings(), actual.input_encodings());
  ASSERT_EQ(expected.date_created(), actual.date_created());
  ASSERT_EQ(expected.last_modified(), actual.last_modified());
}

void TemplateURLServiceSyncTest::AssertEquals(
    const syncer::SyncDataList& data1,
    const syncer::SyncDataList& data2) const {
  SyncDataMap map1 = TemplateURLService::CreateGUIDToSyncDataMap(data1);
  SyncDataMap map2 = TemplateURLService::CreateGUIDToSyncDataMap(data2);

  for (SyncDataMap::const_iterator iter1 = map1.begin();
      iter1 != map1.end(); iter1++) {
    SyncDataMap::iterator iter2 = map2.find(iter1->first);
    if (iter2 != map2.end()) {
      ASSERT_EQ(GetKeyword(iter1->second), GetKeyword(iter2->second));
      ASSERT_EQ(GetURL(iter1->second), GetURL(iter2->second));
      map2.erase(iter2);
    }
  }
  EXPECT_EQ(0U, map2.size());
}

syncer::SyncChange TemplateURLServiceSyncTest::CreateTestSyncChange(
    syncer::SyncChange::SyncChangeType type,
    TemplateURL* turl) const {
  // We take control of the TemplateURL so make sure it's cleaned up after
  // we create data out of it.
  scoped_ptr<TemplateURL> scoped_turl(turl);
  return syncer::SyncChange(
      FROM_HERE,
      type,
      TemplateURLService::CreateSyncDataFromTemplateURL(*scoped_turl));
}

syncer::SyncDataList TemplateURLServiceSyncTest::CreateInitialSyncData() const {
  syncer::SyncDataList list;

  scoped_ptr<TemplateURL> turl(CreateTestTemplateURL(ASCIIToUTF16("key1"),
      "http://key1.com", "key1", 90));
  list.push_back(TemplateURLService::CreateSyncDataFromTemplateURL(*turl));
  turl.reset(CreateTestTemplateURL(ASCIIToUTF16("key2"), "http://key2.com",
                                   "key2", 90));
  list.push_back(TemplateURLService::CreateSyncDataFromTemplateURL(*turl));
  turl.reset(CreateTestTemplateURL(ASCIIToUTF16("key3"), "http://key3.com",
                                   "key3", 90));
  list.push_back(TemplateURLService::CreateSyncDataFromTemplateURL(*turl));

  return list;
}

TemplateURL* TemplateURLServiceSyncTest::Deserialize(
    const syncer::SyncData& sync_data) {
  syncer::SyncChangeList dummy;
  return TemplateURLService::CreateTemplateURLFromTemplateURLAndSyncData(NULL,
      NULL, sync_data, &dummy);
}

TemplateURL* TemplateURLServiceSyncTest::CopyTemplateURL(
    const TemplateURL* turl,
    const std::string& url,
    const std::string& guid) {
  TemplateURLData data (turl->data());
  data.SetURL(url);
  data.date_created = Time::FromTimeT(100);
  data.last_modified = Time::FromTimeT(100);
  data.sync_guid = guid;
  return new TemplateURL(NULL, data);
}

// Actual tests ---------------------------------------------------------------

TEST_F(TemplateURLServiceSyncTest, SerializeDeserialize) {
  // Create a TemplateURL and convert it into a sync specific type.
  scoped_ptr<TemplateURL> turl(
      CreateTestTemplateURL(
          ASCIIToUTF16("unittest"), "http://www.unittest.com/"));
  syncer::SyncData sync_data =
      TemplateURLService::CreateSyncDataFromTemplateURL(*turl);
  // Convert the specifics back to a TemplateURL.
  scoped_ptr<TemplateURL> deserialized(Deserialize(sync_data));
  EXPECT_TRUE(deserialized.get());
  // Ensure that the original and the deserialized TURLs are equal in values.
  AssertEquals(*turl, *deserialized);
}

TEST_F(TemplateURLServiceSyncTest, GetAllSyncDataBasic) {
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://key1.com"));
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key2"), "http://key2.com"));
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key3"), "http://key3.com"));
  syncer::SyncDataList all_sync_data =
      model()->GetAllSyncData(syncer::SEARCH_ENGINES);

  EXPECT_EQ(3U, all_sync_data.size());

  for (syncer::SyncDataList::const_iterator iter = all_sync_data.begin();
      iter != all_sync_data.end(); ++iter) {
    std::string guid = GetGUID(*iter);
    const TemplateURL* service_turl = model()->GetTemplateURLForGUID(guid);
    scoped_ptr<TemplateURL> deserialized(Deserialize(*iter));
    AssertEquals(*service_turl, *deserialized);
  }
}

TEST_F(TemplateURLServiceSyncTest, GetAllSyncDataWithExtension) {
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://key1.com"));
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key2"), "http://key2.com"));
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key3"),
      std::string(extensions::kExtensionScheme) + "://blahblahblah"));
  syncer::SyncDataList all_sync_data =
      model()->GetAllSyncData(syncer::SEARCH_ENGINES);

  EXPECT_EQ(3U, all_sync_data.size());

  for (syncer::SyncDataList::const_iterator iter = all_sync_data.begin();
      iter != all_sync_data.end(); ++iter) {
    std::string guid = GetGUID(*iter);
    const TemplateURL* service_turl = model()->GetTemplateURLForGUID(guid);
    scoped_ptr<TemplateURL> deserialized(Deserialize(*iter));
    AssertEquals(*service_turl, *deserialized);
  }
}

TEST_F(TemplateURLServiceSyncTest, GetAllSyncDataNoManagedEngines) {
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://key1.com"));
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key2"), "http://key2.com"));
  TemplateURL* managed_turl = CreateTestTemplateURL(ASCIIToUTF16("key3"),
      "http://key3.com", std::string(), 100, false, true);
  model()->Add(managed_turl);
  syncer::SyncDataList all_sync_data =
      model()->GetAllSyncData(syncer::SEARCH_ENGINES);

  EXPECT_EQ(2U, all_sync_data.size());

  for (syncer::SyncDataList::const_iterator iter = all_sync_data.begin();
      iter != all_sync_data.end(); ++iter) {
    std::string guid = GetGUID(*iter);
    TemplateURL* service_turl = model()->GetTemplateURLForGUID(guid);
    scoped_ptr<TemplateURL> deserialized(Deserialize(*iter));
    ASSERT_FALSE(service_turl->created_by_policy());
    AssertEquals(*service_turl, *deserialized);
  }
}

TEST_F(TemplateURLServiceSyncTest, UniquifyKeyword) {
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://key1.com"));
  // Create a key that conflicts with something in the model.
  scoped_ptr<TemplateURL> turl(CreateTestTemplateURL(ASCIIToUTF16("key1"),
                                                     "http://new.com", "xyz"));
  string16 new_keyword = model()->UniquifyKeyword(*turl, false);
  EXPECT_EQ(ASCIIToUTF16("new.com"), new_keyword);
  EXPECT_EQ(NULL, model()->GetTemplateURLForKeyword(new_keyword));
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("new.com"), "http://new.com",
                                     "xyz"));

  // Test a second collision. This time it should be resolved by actually
  // modifying the original keyword, since the autogenerated keyword is already
  // used.
  turl.reset(CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://new.com"));
  new_keyword = model()->UniquifyKeyword(*turl, false);
  EXPECT_EQ(ASCIIToUTF16("key1_"), new_keyword);
  EXPECT_EQ(NULL, model()->GetTemplateURLForKeyword(new_keyword));
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key1_"), "http://new.com"));

  // Test a third collision. This should collide on both the autogenerated
  // keyword and the first uniquification attempt.
  turl.reset(CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://new.com"));
  new_keyword = model()->UniquifyKeyword(*turl, false);
  EXPECT_EQ(ASCIIToUTF16("key1__"), new_keyword);
  EXPECT_EQ(NULL, model()->GetTemplateURLForKeyword(new_keyword));

  // If we force the method, it should uniquify the keyword even if it is
  // currently unique, and skip the host-based autogenerated keyword.
  turl.reset(
      CreateTestTemplateURL(ASCIIToUTF16("unique"), "http://unique.com"));
  new_keyword = model()->UniquifyKeyword(*turl, true);
  EXPECT_EQ(ASCIIToUTF16("unique_"), new_keyword);
  EXPECT_EQ(NULL, model()->GetTemplateURLForKeyword(new_keyword));
}

TEST_F(TemplateURLServiceSyncTest, IsLocalTemplateURLBetter) {
  // Test some edge cases of this function.
  const struct {
    time_t local_time;
    time_t sync_time;
    bool local_is_default;
    bool local_created_by_policy;
    bool expected_result;
  } test_cases[] = {
    // Sync is better by timestamp but local is Default.
    {10, 100, true, false, true},
    // Sync is better by timestamp but local is Create by Policy.
    {10, 100, false, true, true},
    // Tie. Sync wins.
    {100, 100, false, false, false},
  };

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(test_cases); ++i) {
    TemplateURL* local_turl = CreateTestTemplateURL(
        ASCIIToUTF16("localkey"), "www.local.com", "localguid",
        test_cases[i].local_time, true, test_cases[i].local_created_by_policy);
    model()->Add(local_turl);
    if (test_cases[i].local_is_default)
      model()->SetDefaultSearchProvider(local_turl);

    scoped_ptr<TemplateURL> sync_turl(CreateTestTemplateURL(
          ASCIIToUTF16("synckey"), "www.sync.com", "syncguid",
          test_cases[i].sync_time));
    EXPECT_EQ(test_cases[i].expected_result,
        model()->IsLocalTemplateURLBetter(local_turl, sync_turl.get()));

    // Undo the changes.
    if (test_cases[i].local_is_default)
      model()->SetDefaultSearchProvider(NULL);
    model()->Remove(local_turl);
  }
}

TEST_F(TemplateURLServiceSyncTest, ResolveSyncKeywordConflict) {
  // This tests cases where neither the sync nor the local TemplateURL are
  // marked safe_for_autoreplace.

  // Create a keyword that conflicts, and make it older.  Sync keyword is
  // uniquified, and a syncer::SyncChange is added.
  string16 original_turl_keyword = ASCIIToUTF16("key1");
  TemplateURL* original_turl = CreateTestTemplateURL(original_turl_keyword,
      "http://key1.com", std::string(), 9000);
  model()->Add(original_turl);
  scoped_ptr<TemplateURL> sync_turl(CreateTestTemplateURL(original_turl_keyword,
      "http://new.com", "remote", 8999));
  syncer::SyncChangeList changes;
  model()->ResolveSyncKeywordConflict(sync_turl.get(), original_turl, &changes);
  EXPECT_NE(original_turl_keyword, sync_turl->keyword());
  EXPECT_EQ(original_turl_keyword, original_turl->keyword());
  ASSERT_EQ(1U, changes.size());
  EXPECT_EQ("remote", GetGUID(changes[0].sync_data()));
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, changes[0].change_type());
  changes.clear();
  model()->Remove(original_turl);

  // Sync is newer.  Original TemplateURL keyword is uniquified.  A SyncChange
  // is added (which in a normal run would be deleted by PruneSyncChanges() when
  // the local GUID doesn't appear in the sync GUID list).  Also ensure that
  // this does not change the safe_for_autoreplace flag or the TemplateURLID in
  // the original.
  original_turl = CreateTestTemplateURL(original_turl_keyword,
                                        "http://key1.com", "local", 9000);
  model()->Add(original_turl);
  TemplateURLID original_id = original_turl->id();
  sync_turl.reset(CreateTestTemplateURL(original_turl_keyword, "http://new.com",
                                        std::string(), 9001));
  model()->ResolveSyncKeywordConflict(sync_turl.get(), original_turl, &changes);
  EXPECT_EQ(original_turl_keyword, sync_turl->keyword());
  EXPECT_NE(original_turl_keyword, original_turl->keyword());
  EXPECT_FALSE(original_turl->safe_for_autoreplace());
  EXPECT_EQ(original_id, original_turl->id());
  EXPECT_EQ(NULL, model()->GetTemplateURLForKeyword(original_turl_keyword));
  ASSERT_EQ(1U, changes.size());
  EXPECT_EQ("local", GetGUID(changes[0].sync_data()));
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, changes[0].change_type());
  changes.clear();
  model()->Remove(original_turl);

  // Equal times. Same result as above. Sync left alone, original uniquified so
  // sync_turl can fit.
  original_turl = CreateTestTemplateURL(original_turl_keyword,
                                        "http://key1.com", "local2", 9000);
  model()->Add(original_turl);
  sync_turl.reset(CreateTestTemplateURL(original_turl_keyword, "http://new.com",
                                        std::string(), 9000));
  model()->ResolveSyncKeywordConflict(sync_turl.get(), original_turl, &changes);
  EXPECT_EQ(original_turl_keyword, sync_turl->keyword());
  EXPECT_NE(original_turl_keyword, original_turl->keyword());
  EXPECT_EQ(NULL, model()->GetTemplateURLForKeyword(original_turl_keyword));
  ASSERT_EQ(1U, changes.size());
  EXPECT_EQ("local2", GetGUID(changes[0].sync_data()));
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, changes[0].change_type());
  changes.clear();
  model()->Remove(original_turl);

  // Sync is newer, but original TemplateURL is created by policy, so it wins.
  // Sync keyword is uniquified, and a syncer::SyncChange is added.
  original_turl = CreateTestTemplateURL(original_turl_keyword,
      "http://key1.com", std::string(), 9000, false, true);
  model()->Add(original_turl);
  sync_turl.reset(CreateTestTemplateURL(original_turl_keyword, "http://new.com",
                                        "remote2", 9999));
  model()->ResolveSyncKeywordConflict(sync_turl.get(), original_turl, &changes);
  EXPECT_NE(original_turl_keyword, sync_turl->keyword());
  EXPECT_EQ(original_turl_keyword, original_turl->keyword());
  EXPECT_EQ(NULL, model()->GetTemplateURLForKeyword(sync_turl->keyword()));
  ASSERT_EQ(1U, changes.size());
  EXPECT_EQ("remote2", GetGUID(changes[0].sync_data()));
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, changes[0].change_type());
  changes.clear();
  model()->Remove(original_turl);
}

TEST_F(TemplateURLServiceSyncTest, StartSyncEmpty) {
  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES, syncer::SyncDataList(),
      PassProcessor(), CreateAndPassSyncErrorFactory());

  EXPECT_EQ(0U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  EXPECT_EQ(0U, processor()->change_list_size());
  EXPECT_EQ(0, merge_result.num_items_added());
  EXPECT_EQ(0, merge_result.num_items_modified());
  EXPECT_EQ(0, merge_result.num_items_deleted());
  EXPECT_EQ(0, merge_result.num_items_before_association());
  EXPECT_EQ(0, merge_result.num_items_after_association());
}

TEST_F(TemplateURLServiceSyncTest, MergeIntoEmpty) {
  syncer::SyncDataList initial_data = CreateInitialSyncData();

  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  EXPECT_EQ(3U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  // We expect the model to have accepted all of the initial sync data. Search
  // through the model using the GUIDs to ensure that they're present.
  for (syncer::SyncDataList::const_iterator iter = initial_data.begin();
      iter != initial_data.end(); ++iter) {
    std::string guid = GetGUID(*iter);
    EXPECT_TRUE(model()->GetTemplateURLForGUID(guid));
  }

  EXPECT_EQ(0U, processor()->change_list_size());

  // Locally the three new TemplateURL's should have been added.
  EXPECT_EQ(3, merge_result.num_items_added());
  EXPECT_EQ(0, merge_result.num_items_modified());
  EXPECT_EQ(0, merge_result.num_items_deleted());
  EXPECT_EQ(0, merge_result.num_items_before_association());
  EXPECT_EQ(3, merge_result.num_items_after_association());
}

TEST_F(TemplateURLServiceSyncTest, MergeInAllNewData) {
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("abc.com"), "http://abc.com",
                                     "abc"));
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("def.com"), "http://def.com",
                                     "def"));
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("xyz.com"), "http://xyz.com",
                                     "xyz"));
  syncer::SyncDataList initial_data = CreateInitialSyncData();

  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  EXPECT_EQ(6U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  // We expect the model to have accepted all of the initial sync data. Search
  // through the model using the GUIDs to ensure that they're present.
  for (syncer::SyncDataList::const_iterator iter = initial_data.begin();
      iter != initial_data.end(); ++iter) {
    std::string guid = GetGUID(*iter);
    EXPECT_TRUE(model()->GetTemplateURLForGUID(guid));
  }
  // All the original TemplateURLs should also remain in the model.
  EXPECT_TRUE(model()->GetTemplateURLForKeyword(ASCIIToUTF16("abc.com")));
  EXPECT_TRUE(model()->GetTemplateURLForKeyword(ASCIIToUTF16("def.com")));
  EXPECT_TRUE(model()->GetTemplateURLForKeyword(ASCIIToUTF16("xyz.com")));
  // Ensure that Sync received the expected changes.
  EXPECT_EQ(3U, processor()->change_list_size());
  EXPECT_TRUE(processor()->contains_guid("abc"));
  EXPECT_TRUE(processor()->contains_guid("def"));
  EXPECT_TRUE(processor()->contains_guid("xyz"));

  // Locally the three new TemplateURL's should have been added.
  EXPECT_EQ(3, merge_result.num_items_added());
  EXPECT_EQ(0, merge_result.num_items_modified());
  EXPECT_EQ(0, merge_result.num_items_deleted());
  EXPECT_EQ(3, merge_result.num_items_before_association());
  EXPECT_EQ(6, merge_result.num_items_after_association());
}

TEST_F(TemplateURLServiceSyncTest, MergeSyncIsTheSame) {
  // The local data is the same as the sync data merged in. i.e. - There have
  // been no changes since the last time we synced. Even the last_modified
  // timestamps are the same.
  syncer::SyncDataList initial_data = CreateInitialSyncData();
  for (syncer::SyncDataList::const_iterator iter = initial_data.begin();
      iter != initial_data.end(); ++iter) {
    TemplateURL* converted = Deserialize(*iter);
    model()->Add(converted);
  }

  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  EXPECT_EQ(3U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  for (syncer::SyncDataList::const_iterator iter = initial_data.begin();
      iter != initial_data.end(); ++iter) {
    std::string guid = GetGUID(*iter);
    EXPECT_TRUE(model()->GetTemplateURLForGUID(guid));
  }
  EXPECT_EQ(0U, processor()->change_list_size());

  // Locally everything should remain the same.
  EXPECT_EQ(0, merge_result.num_items_added());
  EXPECT_EQ(0, merge_result.num_items_modified());
  EXPECT_EQ(0, merge_result.num_items_deleted());
  EXPECT_EQ(3, merge_result.num_items_before_association());
  EXPECT_EQ(3, merge_result.num_items_after_association());
}

TEST_F(TemplateURLServiceSyncTest, MergeUpdateFromSync) {
  // The local data is the same as the sync data merged in, but timestamps have
  // changed. Ensure the right fields are merged in.
  syncer::SyncDataList initial_data;
  TemplateURL* turl1 = CreateTestTemplateURL(ASCIIToUTF16("abc.com"),
                                             "http://abc.com", "abc", 9000);
  model()->Add(turl1);
  TemplateURL* turl2 = CreateTestTemplateURL(ASCIIToUTF16("xyz.com"),
                                             "http://xyz.com", "xyz", 9000);
  model()->Add(turl2);

  scoped_ptr<TemplateURL> turl1_newer(CreateTestTemplateURL(
      ASCIIToUTF16("abc.com"), "http://abc.ca", "abc", 9999));
  initial_data.push_back(
      TemplateURLService::CreateSyncDataFromTemplateURL(*turl1_newer));

  scoped_ptr<TemplateURL> turl2_older(CreateTestTemplateURL(
      ASCIIToUTF16("xyz.com"), "http://xyz.ca", "xyz", 8888));
  initial_data.push_back(
      TemplateURLService::CreateSyncDataFromTemplateURL(*turl2_older));

  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  // Both were local updates, so we expect the same count.
  EXPECT_EQ(2U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());

  // Check that the first replaced the initial abc TemplateURL.
  EXPECT_EQ(turl1, model()->GetTemplateURLForGUID("abc"));
  EXPECT_EQ("http://abc.ca", turl1->url());

  // Check that the second produced an upstream update to the xyz TemplateURL.
  EXPECT_EQ(1U, processor()->change_list_size());
  ASSERT_TRUE(processor()->contains_guid("xyz"));
  syncer::SyncChange change = processor()->change_for_guid("xyz");
  EXPECT_TRUE(change.change_type() == syncer::SyncChange::ACTION_UPDATE);
  EXPECT_EQ("http://xyz.com", GetURL(change.sync_data()));

  // Locally only the older item should have been modified.
  EXPECT_EQ(0, merge_result.num_items_added());
  EXPECT_EQ(1, merge_result.num_items_modified());
  EXPECT_EQ(0, merge_result.num_items_deleted());
  EXPECT_EQ(2, merge_result.num_items_before_association());
  EXPECT_EQ(2, merge_result.num_items_after_association());
}

TEST_F(TemplateURLServiceSyncTest, MergeAddFromOlderSyncData) {
  // GUIDs all differ, so this is data to be added from Sync, but the timestamps
  // from Sync are older. Set up the local data so that one is a dupe, one has a
  // conflicting keyword, and the last has no conflicts (a clean ADD).
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://key1.com",
                                     "aaa", 100));  // dupe

  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key2"),
      "http://expected.com", "bbb", 100));  // keyword conflict

  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("unique"),
                                     "http://unique.com", "ccc"));  // add

  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES,
      CreateInitialSyncData(), PassProcessor(),
      CreateAndPassSyncErrorFactory());

  // The dupe and conflict results in merges, as local values are always merged
  // with sync values if there is a keyword conflict. The unique keyword should
  // be added.
  EXPECT_EQ(4U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());

  // The key1 duplicate results in the local copy winning. Ensure that Sync's
  // copy was not added, and the local copy is pushed upstream to Sync as an
  // update. The local copy should have received the sync data's GUID.
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key1"));
  // Check changes for the UPDATE.
  ASSERT_TRUE(processor()->contains_guid("key1"));
  syncer::SyncChange key1_change = processor()->change_for_guid("key1");
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, key1_change.change_type());
  // The local sync_guid should no longer be found.
  EXPECT_FALSE(model()->GetTemplateURLForGUID("aaa"));

  // The key2 keyword conflict results in a merge, with the values of the local
  // copy winning, so ensure it retains the original URL, and that an update to
  // the sync guid is pushed upstream to Sync.
  const TemplateURL* key2 = model()->GetTemplateURLForGUID("key2");
  ASSERT_TRUE(key2);
  EXPECT_EQ(ASCIIToUTF16("key2"), key2->keyword());
  EXPECT_EQ("http://expected.com", key2->url());
  // Check changes for the UPDATE.
  ASSERT_TRUE(processor()->contains_guid("key2"));
  syncer::SyncChange key2_change = processor()->change_for_guid("key2");
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, key2_change.change_type());
  EXPECT_EQ("key2", GetKeyword(key2_change.sync_data()));
  EXPECT_EQ("http://expected.com", GetURL(key2_change.sync_data()));
  // The local sync_guid should no longer be found.
  EXPECT_FALSE(model()->GetTemplateURLForGUID("bbb"));

  // The last TemplateURL should have had no conflicts and was just added. It
  // should not have replaced the third local TemplateURL.
  EXPECT_TRUE(model()->GetTemplateURLForGUID("ccc"));
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key3"));

  // Two UPDATEs and one ADD.
  EXPECT_EQ(3U, processor()->change_list_size());
  // One ADDs should be pushed up to Sync.
  ASSERT_TRUE(processor()->contains_guid("ccc"));
  EXPECT_EQ(syncer::SyncChange::ACTION_ADD,
            processor()->change_for_guid("ccc").change_type());

  // All the sync items had new guids, but only one doesn't conflict and is
  // added. The other two conflicting cases result in local modifications
  // to override the local guids but preserve the local data.
  EXPECT_EQ(1, merge_result.num_items_added());
  EXPECT_EQ(2, merge_result.num_items_modified());
  EXPECT_EQ(0, merge_result.num_items_deleted());
  EXPECT_EQ(3, merge_result.num_items_before_association());
  EXPECT_EQ(4, merge_result.num_items_after_association());
}

TEST_F(TemplateURLServiceSyncTest, MergeAddFromNewerSyncData) {
  // GUIDs all differ, so Sync may overtake some entries, but the timestamps
  // from Sync are newer. Set up the local data so that one is a dupe, one has a
  // conflicting keyword, and the last has no conflicts (a clean ADD).
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://key1.com",
                                     "aaa", 10));  // dupe

  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key2"),
      "http://expected.com", "bbb", 10));  // keyword conflict

  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("unique"),
                                     "http://unique.com", "ccc", 10));  // add

  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES,
      CreateInitialSyncData(), PassProcessor(),
      CreateAndPassSyncErrorFactory());

  // The dupe and keyword conflict results in merges. The unique keyword be
  // added to the model.
  EXPECT_EQ(4U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());

  // The key1 duplicate results in Sync's copy winning. Ensure that Sync's
  // copy replaced the local copy.
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key1"));
  EXPECT_FALSE(model()->GetTemplateURLForGUID("aaa"));
  EXPECT_FALSE(processor()->contains_guid("key1"));
  EXPECT_FALSE(processor()->contains_guid("aaa"));

  // The key2 keyword conflict results in Sync's copy winning, so ensure it
  // retains the original keyword and is added. The local copy should be
  // removed.
  const TemplateURL* key2_sync = model()->GetTemplateURLForGUID("key2");
  ASSERT_TRUE(key2_sync);
  EXPECT_EQ(ASCIIToUTF16("key2"), key2_sync->keyword());
  EXPECT_FALSE(model()->GetTemplateURLForGUID("bbb"));

  // The last TemplateURL should have had no conflicts and was just added. It
  // should not have replaced the third local TemplateURL.
  EXPECT_TRUE(model()->GetTemplateURLForGUID("ccc"));
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key3"));

  // One ADD.
  EXPECT_EQ(1U, processor()->change_list_size());
  // One ADDs should be pushed up to Sync.
  ASSERT_TRUE(processor()->contains_guid("ccc"));
  EXPECT_EQ(syncer::SyncChange::ACTION_ADD,
            processor()->change_for_guid("ccc").change_type());

  // One of the sync items is added directly without conflict. The other two
  // conflict but are newer than the local items so are added while the local
  // is deleted.
  EXPECT_EQ(3, merge_result.num_items_added());
  EXPECT_EQ(0, merge_result.num_items_modified());
  EXPECT_EQ(2, merge_result.num_items_deleted());
  EXPECT_EQ(3, merge_result.num_items_before_association());
  EXPECT_EQ(4, merge_result.num_items_after_association());
}

TEST_F(TemplateURLServiceSyncTest, ProcessChangesEmptyModel) {
  // We initially have no data.
  model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES, syncer::SyncDataList(),
      PassProcessor(), CreateAndPassSyncErrorFactory());

  // Set up a bunch of ADDs.
  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
      CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://key1.com", "key1")));
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
      CreateTestTemplateURL(ASCIIToUTF16("key2"), "http://key2.com", "key2")));
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
      CreateTestTemplateURL(ASCIIToUTF16("key3"), "http://key3.com", "key3")));

  model()->ProcessSyncChanges(FROM_HERE, changes);

  EXPECT_EQ(3U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  EXPECT_EQ(0U, processor()->change_list_size());
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key1"));
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key2"));
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key3"));
}

TEST_F(TemplateURLServiceSyncTest, ProcessChangesNoConflicts) {
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES,
                                    CreateInitialSyncData(), PassProcessor(),
                                    CreateAndPassSyncErrorFactory());

  // Process different types of changes, without conflicts.
  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
      CreateTestTemplateURL(ASCIIToUTF16("key4"), "http://key4.com", "key4")));
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_UPDATE,
      CreateTestTemplateURL(ASCIIToUTF16("newkeyword"), "http://new.com",
                            "key2")));
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_DELETE,
      CreateTestTemplateURL(ASCIIToUTF16("key3"), "http://key3.com", "key3")));

  model()->ProcessSyncChanges(FROM_HERE, changes);

  // Add one, remove one, update one, so the number shouldn't change.
  EXPECT_EQ(3U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  EXPECT_EQ(0U, processor()->change_list_size());
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key1"));
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key2"));
  const TemplateURL* turl = model()->GetTemplateURLForGUID("key2");
  EXPECT_TRUE(turl);
  EXPECT_EQ(ASCIIToUTF16("newkeyword"), turl->keyword());
  EXPECT_EQ("http://new.com", turl->url());
  EXPECT_FALSE(model()->GetTemplateURLForGUID("key3"));
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key4"));
}

TEST_F(TemplateURLServiceSyncTest, ProcessChangesWithConflictsSyncWins) {
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES,
                                    CreateInitialSyncData(), PassProcessor(),
                                    CreateAndPassSyncErrorFactory());

  // Process different types of changes, with conflicts. Note that all this data
  // has a newer timestamp, so Sync will win in these scenarios.
  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
      CreateTestTemplateURL(ASCIIToUTF16("key2"), "http://new.com", "aaa")));
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_UPDATE,
      CreateTestTemplateURL(ASCIIToUTF16("key3"), "http://key3.com", "key1")));

  model()->ProcessSyncChanges(FROM_HERE, changes);

  // Add one, update one, so we're up to 4.
  EXPECT_EQ(4U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  // Sync is always newer here, so it should always win.  We should create
  // SyncChanges for the changes to the local entities, since they're synced
  // too.
  EXPECT_EQ(2U, processor()->change_list_size());
  ASSERT_TRUE(processor()->contains_guid("key2"));
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE,
            processor()->change_for_guid("key2").change_type());
  ASSERT_TRUE(processor()->contains_guid("key3"));
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE,
            processor()->change_for_guid("key3").change_type());

  // aaa conflicts with key2 and wins, forcing key2's keyword to update.
  EXPECT_TRUE(model()->GetTemplateURLForGUID("aaa"));
  EXPECT_EQ(model()->GetTemplateURLForGUID("aaa"),
            model()->GetTemplateURLForKeyword(ASCIIToUTF16("key2")));
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key2"));
  EXPECT_EQ(model()->GetTemplateURLForGUID("key2"),
            model()->GetTemplateURLForKeyword(ASCIIToUTF16("key2.com")));
  // key1 update conflicts with key3 and wins, forcing key3's keyword to update.
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key1"));
  EXPECT_EQ(model()->GetTemplateURLForGUID("key1"),
            model()->GetTemplateURLForKeyword(ASCIIToUTF16("key3")));
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key3"));
  EXPECT_EQ(model()->GetTemplateURLForGUID("key3"),
            model()->GetTemplateURLForKeyword(ASCIIToUTF16("key3.com")));
}

TEST_F(TemplateURLServiceSyncTest, ProcessChangesWithConflictsLocalWins) {
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES,
                                    CreateInitialSyncData(), PassProcessor(),
                                    CreateAndPassSyncErrorFactory());

  // Process different types of changes, with conflicts. Note that all this data
  // has an older timestamp, so the local data will win in these scenarios.
  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
      CreateTestTemplateURL(ASCIIToUTF16("key2"), "http://new.com", "aaa",
                            10)));
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_UPDATE,
      CreateTestTemplateURL(ASCIIToUTF16("key3"), "http://key3.com", "key1",
                            10)));

  model()->ProcessSyncChanges(FROM_HERE, changes);

  // Add one, update one, so we're up to 4.
  EXPECT_EQ(4U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  // Local data wins twice so two updates are pushed up to Sync.
  EXPECT_EQ(2U, processor()->change_list_size());

  // aaa conflicts with key2 and loses, forcing it's keyword to update.
  EXPECT_TRUE(model()->GetTemplateURLForGUID("aaa"));
  EXPECT_EQ(model()->GetTemplateURLForGUID("aaa"),
            model()->GetTemplateURLForKeyword(ASCIIToUTF16("new.com")));
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key2"));
  EXPECT_EQ(model()->GetTemplateURLForGUID("key2"),
            model()->GetTemplateURLForKeyword(ASCIIToUTF16("key2")));
  // key1 update conflicts with key3 and loses, forcing key1's keyword to
  // update.
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key1"));
  EXPECT_EQ(model()->GetTemplateURLForGUID("key1"),
            model()->GetTemplateURLForKeyword(ASCIIToUTF16("key3.com")));
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key3"));
  EXPECT_EQ(model()->GetTemplateURLForGUID("key3"),
            model()->GetTemplateURLForKeyword(ASCIIToUTF16("key3")));

  ASSERT_TRUE(processor()->contains_guid("aaa"));
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE,
            processor()->change_for_guid("aaa").change_type());
  ASSERT_TRUE(processor()->contains_guid("key1"));
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE,
            processor()->change_for_guid("key1").change_type());
}

TEST_F(TemplateURLServiceSyncTest, ProcessTemplateURLChange) {
  // Ensure that ProcessTemplateURLChange is called and pushes the correct
  // changes to Sync whenever local changes are made to TemplateURLs.
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES,
                                    CreateInitialSyncData(), PassProcessor(),
                                    CreateAndPassSyncErrorFactory());

  // Add a new search engine.
  TemplateURL* new_turl =
      CreateTestTemplateURL(ASCIIToUTF16("baidu"), "http://baidu.cn", "new");
  model()->Add(new_turl);
  EXPECT_EQ(1U, processor()->change_list_size());
  ASSERT_TRUE(processor()->contains_guid("new"));
  syncer::SyncChange change = processor()->change_for_guid("new");
  EXPECT_EQ(syncer::SyncChange::ACTION_ADD, change.change_type());
  EXPECT_EQ("baidu", GetKeyword(change.sync_data()));
  EXPECT_EQ("http://baidu.cn", GetURL(change.sync_data()));

  // Change a keyword.
  TemplateURL* existing_turl = model()->GetTemplateURLForGUID("key1");
  model()->ResetTemplateURL(existing_turl, existing_turl->short_name(),
                            ASCIIToUTF16("k"), existing_turl->url());
  EXPECT_EQ(1U, processor()->change_list_size());
  ASSERT_TRUE(processor()->contains_guid("key1"));
  change = processor()->change_for_guid("key1");
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, change.change_type());
  EXPECT_EQ("k", GetKeyword(change.sync_data()));

  // Remove an existing search engine.
  existing_turl = model()->GetTemplateURLForGUID("key2");
  model()->Remove(existing_turl);
  EXPECT_EQ(1U, processor()->change_list_size());
  ASSERT_TRUE(processor()->contains_guid("key2"));
  change = processor()->change_for_guid("key2");
  EXPECT_EQ(syncer::SyncChange::ACTION_DELETE, change.change_type());
}

TEST_F(TemplateURLServiceSyncTest, ProcessChangesWithLocalExtensions) {
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES,
                                    CreateInitialSyncData(), PassProcessor(),
                                    CreateAndPassSyncErrorFactory());

  // Add some extension keywords locally.
  TemplateURL* extension1 = CreateTestTemplateURL(ASCIIToUTF16("keyword1"),
      std::string(extensions::kExtensionScheme) + "://extension1");
  model()->Add(extension1);
  EXPECT_EQ(1U, processor()->change_list_size());
  TemplateURL* extension2 = CreateTestTemplateURL(ASCIIToUTF16("keyword2"),
      std::string(extensions::kExtensionScheme) + "://extension2");
  model()->Add(extension2);
  EXPECT_EQ(1U, processor()->change_list_size());

  // Create some sync changes that will conflict with the extension keywords.
  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
    CreateTestTemplateURL(ASCIIToUTF16("keyword1"), "http://aaa.com",
                          std::string(), 100, true)));
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
    CreateTestTemplateURL(ASCIIToUTF16("keyword2"), "http://bbb.com")));
  model()->ProcessSyncChanges(FROM_HERE, changes);

  // The existing extension keywords should be uniquified.
  EXPECT_FALSE(model()->GetTemplateURLForHost("aaa.com") == NULL);
  EXPECT_EQ(model()->GetTemplateURLForHost("aaa.com"),
            model()->GetTemplateURLForKeyword(ASCIIToUTF16("keyword1")));
  TemplateURL* url_for_keyword2 =
      model()->GetTemplateURLForKeyword(ASCIIToUTF16("keyword2"));
  EXPECT_NE(extension2, url_for_keyword2);
  EXPECT_EQ("http://bbb.com", url_for_keyword2->url());

  // Replaced extension keywords should be uniquified.
  EXPECT_EQ(extension1,
            model()->GetTemplateURLForKeyword(ASCIIToUTF16("keyword1_")));
  EXPECT_EQ(extension2,
            model()->GetTemplateURLForKeyword(ASCIIToUTF16("keyword2_")));
}

TEST_F(TemplateURLServiceSyncTest, AutogeneratedKeywordMigrated) {
  // Create a couple of sync entries with autogenerated keywords.
  syncer::SyncDataList initial_data;
  scoped_ptr<TemplateURL> turl(
      CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://key1.com", "key1"));
  initial_data.push_back(
      CreateCustomSyncData(*turl, true, turl->url(), turl->sync_guid()));
  turl.reset(CreateTestTemplateURL(ASCIIToUTF16("key2"),
      "{google:baseURL}search?q={searchTerms}", "key2"));
  initial_data.push_back(
      CreateCustomSyncData(*turl, true, turl->url(), turl->sync_guid()));

  // Now try to sync the data locally.
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  // Both entries should have been added, with explicit keywords.
  TemplateURL* key1 = model()->GetTemplateURLForHost("key1.com");
  ASSERT_FALSE(key1 == NULL);
  EXPECT_EQ(ASCIIToUTF16("key1.com"), key1->keyword());
  GURL google_url(UIThreadSearchTermsData(profile_a()).GoogleBaseURLValue());
  TemplateURL* key2 = model()->GetTemplateURLForHost(google_url.host());
  ASSERT_FALSE(key2 == NULL);
  string16 google_keyword(net::StripWWWFromHost(google_url));
  EXPECT_EQ(google_keyword, key2->keyword());

  // We should also have gotten some corresponding UPDATEs pushed upstream.
  EXPECT_GE(processor()->change_list_size(), 2U);
  ASSERT_TRUE(processor()->contains_guid("key1"));
  syncer::SyncChange key1_change = processor()->change_for_guid("key1");
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, key1_change.change_type());
  EXPECT_EQ("key1.com", GetKeyword(key1_change.sync_data()));
  ASSERT_TRUE(processor()->contains_guid("key2"));
  syncer::SyncChange key2_change = processor()->change_for_guid("key2");
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, key2_change.change_type());
  EXPECT_EQ(google_keyword, UTF8ToUTF16(GetKeyword(key2_change.sync_data())));
}

TEST_F(TemplateURLServiceSyncTest, AutogeneratedKeywordConflicts) {
  // Sync brings in some autogenerated keywords, but the generated keywords we
  // try to create conflict with ones in the model.
  string16 google_keyword(net::StripWWWFromHost(GURL(
      UIThreadSearchTermsData(profile_a()).GoogleBaseURLValue())));
  const std::string local_google_url =
      "{google:baseURL}1/search?q={searchTerms}";
  TemplateURL* google = CreateTestTemplateURL(google_keyword, local_google_url);
  model()->Add(google);
  TemplateURL* other =
      CreateTestTemplateURL(ASCIIToUTF16("other.com"), "http://other.com/foo");
  model()->Add(other);
  syncer::SyncDataList initial_data;
  scoped_ptr<TemplateURL> turl(CreateTestTemplateURL(ASCIIToUTF16("sync1"),
      "{google:baseURL}2/search?q={searchTerms}", "sync1", 50));
  initial_data.push_back(
      CreateCustomSyncData(*turl, true, turl->url(), turl->sync_guid()));
  const std::string synced_other_url =
      "http://other.com/search?q={searchTerms}";
  turl.reset(CreateTestTemplateURL(ASCIIToUTF16("sync2"),
      synced_other_url, "sync2", 150));
  initial_data.push_back(
      CreateCustomSyncData(*turl, true, turl->url(), turl->sync_guid()));

  // Before we merge the data, grab the local sync_guids so we can ensure that
  // they've been replaced.
  const std::string local_google_guid = google->sync_guid();
  const std::string local_other_guid = other->sync_guid();

  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  // In this case, the conflicts should be handled just like any other keyword
  // conflicts -- the later-modified TemplateURL is assumed to be authoritative.
  // Since the initial TemplateURLs were local only, they should be merged with
  // the sync TemplateURLs (GUIDs transferred over).
  EXPECT_FALSE(model()->GetTemplateURLForGUID(local_google_guid));
  ASSERT_TRUE(model()->GetTemplateURLForGUID("sync1"));
  EXPECT_EQ(google_keyword, model()->GetTemplateURLForGUID("sync1")->keyword());
  EXPECT_FALSE(model()->GetTemplateURLForGUID(local_other_guid));
  ASSERT_TRUE(model()->GetTemplateURLForGUID("sync2"));
  EXPECT_EQ(ASCIIToUTF16("other.com"),
            model()->GetTemplateURLForGUID("sync2")->keyword());

  // Both synced URLs should have associated UPDATEs, since both needed their
  // keywords to be generated.
  EXPECT_EQ(processor()->change_list_size(), 2U);
  ASSERT_TRUE(processor()->contains_guid("sync1"));
  syncer::SyncChange sync1_change = processor()->change_for_guid("sync1");
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, sync1_change.change_type());
  EXPECT_EQ(google_keyword, UTF8ToUTF16(GetKeyword(sync1_change.sync_data())));
  EXPECT_EQ(local_google_url, GetURL(sync1_change.sync_data()));
  ASSERT_TRUE(processor()->contains_guid("sync2"));
  syncer::SyncChange sync2_change = processor()->change_for_guid("sync2");
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, sync2_change.change_type());
  EXPECT_EQ("other.com", GetKeyword(sync2_change.sync_data()));
  EXPECT_EQ(synced_other_url, GetURL(sync2_change.sync_data()));
}

TEST_F(TemplateURLServiceSyncTest, TwoAutogeneratedKeywordsUsingGoogleBaseURL) {
  // Sync brings in two autogenerated keywords and both use Google base URLs.
  // We make the first older so that it will get renamed once before the second
  // and then again once after (when we resolve conflicts for the second).
  syncer::SyncDataList initial_data;
  scoped_ptr<TemplateURL> turl(CreateTestTemplateURL(ASCIIToUTF16("key1"),
      "{google:baseURL}1/search?q={searchTerms}", "key1", 50));
  initial_data.push_back(
      CreateCustomSyncData(*turl, true, turl->url(), turl->sync_guid()));
  turl.reset(CreateTestTemplateURL(ASCIIToUTF16("key2"),
      "{google:baseURL}2/search?q={searchTerms}", "key2"));
  initial_data.push_back(
      CreateCustomSyncData(*turl, true, turl->url(), turl->sync_guid()));
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  // We should still have coalesced the updates to one each.
  string16 google_keyword(net::StripWWWFromHost(GURL(
      UIThreadSearchTermsData(profile_a()).GoogleBaseURLValue())));
  TemplateURL* keyword1 =
      model()->GetTemplateURLForKeyword(google_keyword + ASCIIToUTF16("_"));
  ASSERT_FALSE(keyword1 == NULL);
  EXPECT_EQ("key1", keyword1->sync_guid());
  TemplateURL* keyword2 = model()->GetTemplateURLForKeyword(google_keyword);
  ASSERT_FALSE(keyword2 == NULL);
  EXPECT_EQ("key2", keyword2->sync_guid());

  EXPECT_GE(processor()->change_list_size(), 2U);
  ASSERT_TRUE(processor()->contains_guid("key1"));
  syncer::SyncChange key1_change = processor()->change_for_guid("key1");
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, key1_change.change_type());
  EXPECT_EQ(keyword1->keyword(),
            UTF8ToUTF16(GetKeyword(key1_change.sync_data())));
  ASSERT_TRUE(processor()->contains_guid("key2"));
  syncer::SyncChange key2_change = processor()->change_for_guid("key2");
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, key2_change.change_type());
  EXPECT_EQ(keyword2->keyword(),
            UTF8ToUTF16(GetKeyword(key2_change.sync_data())));
}

TEST_F(TemplateURLServiceSyncTest, DuplicateEncodingsRemoved) {
  // Create a sync entry with duplicate encodings.
  syncer::SyncDataList initial_data;

  TemplateURLData data;
  data.short_name = ASCIIToUTF16("test");
  data.SetKeyword(ASCIIToUTF16("keyword"));
  data.SetURL("http://test/%s");
  data.input_encodings.push_back("UTF-8");
  data.input_encodings.push_back("UTF-8");
  data.input_encodings.push_back("UTF-16");
  data.input_encodings.push_back("UTF-8");
  data.input_encodings.push_back("Big5");
  data.input_encodings.push_back("UTF-16");
  data.input_encodings.push_back("Big5");
  data.input_encodings.push_back("Windows-1252");
  data.date_created = Time::FromTimeT(100);
  data.last_modified = Time::FromTimeT(100);
  data.sync_guid = "keyword";
  scoped_ptr<TemplateURL> turl(new TemplateURL(NULL, data));
  initial_data.push_back(
      TemplateURLService::CreateSyncDataFromTemplateURL(*turl));

  // Now try to sync the data locally.
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  // The entry should have been added, with duplicate encodings removed.
  TemplateURL* keyword =
      model()->GetTemplateURLForKeyword(ASCIIToUTF16("keyword"));
  ASSERT_FALSE(keyword == NULL);
  EXPECT_EQ(4U, keyword->input_encodings().size());

  // We should also have gotten a corresponding UPDATE pushed upstream.
  EXPECT_GE(processor()->change_list_size(), 1U);
  ASSERT_TRUE(processor()->contains_guid("keyword"));
  syncer::SyncChange keyword_change = processor()->change_for_guid("keyword");
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, keyword_change.change_type());
  EXPECT_EQ("UTF-8;UTF-16;Big5;Windows-1252", keyword_change.sync_data().
      GetSpecifics().search_engine().input_encodings());
}

TEST_F(TemplateURLServiceSyncTest, MergeTwoClientsBasic) {
  // Start off B with some empty data.
  model_b()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES,
                                      CreateInitialSyncData(), PassProcessor(),
                                      CreateAndPassSyncErrorFactory());

  // Merge A and B. All of B's data should transfer over to A, which initially
  // has no data.
  scoped_ptr<SyncChangeProcessorDelegate> delegate_b(
      new SyncChangeProcessorDelegate(model_b()));
  model_a()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES,
      model_b()->GetAllSyncData(syncer::SEARCH_ENGINES),
      delegate_b.PassAs<syncer::SyncChangeProcessor>(),
      CreateAndPassSyncErrorFactory());

  // They should be consistent.
  AssertEquals(model_a()->GetAllSyncData(syncer::SEARCH_ENGINES),
               model_b()->GetAllSyncData(syncer::SEARCH_ENGINES));
}

TEST_F(TemplateURLServiceSyncTest, MergeTwoClientsDupesAndConflicts) {
  // Start off B with some empty data.
  model_b()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES,
                                      CreateInitialSyncData(), PassProcessor(),
                                      CreateAndPassSyncErrorFactory());

  // Set up A so we have some interesting duplicates and conflicts.
  model_a()->Add(CreateTestTemplateURL(ASCIIToUTF16("key4"), "http://key4.com",
                                       "key4"));  // Added
  model_a()->Add(CreateTestTemplateURL(ASCIIToUTF16("key2"), "http://key2.com",
                                       "key2"));  // Merge - Copy of key2.
  model_a()->Add(CreateTestTemplateURL(ASCIIToUTF16("key3"), "http://key3.com",
                                       "key5", 10));  // Merge - Dupe of key3.
  model_a()->Add(CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://key6.com",
                                       "key6", 10));  // Conflict with key1

  // Merge A and B.
  scoped_ptr<SyncChangeProcessorDelegate> delegate_b(
      new SyncChangeProcessorDelegate(model_b()));
  model_a()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES,
      model_b()->GetAllSyncData(syncer::SEARCH_ENGINES),
      delegate_b.PassAs<syncer::SyncChangeProcessor>(),
      CreateAndPassSyncErrorFactory());

  // They should be consistent.
  AssertEquals(model_a()->GetAllSyncData(syncer::SEARCH_ENGINES),
               model_b()->GetAllSyncData(syncer::SEARCH_ENGINES));
}

TEST_F(TemplateURLServiceSyncTest, StopSyncing) {
  syncer::SyncError error =
      model()->MergeDataAndStartSyncing(
          syncer::SEARCH_ENGINES,
          CreateInitialSyncData(),
          PassProcessor(),
          CreateAndPassSyncErrorFactory()).error();
  ASSERT_FALSE(error.IsSet());
  model()->StopSyncing(syncer::SEARCH_ENGINES);

  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_UPDATE,
      CreateTestTemplateURL(ASCIIToUTF16("newkeyword"), "http://new.com",
                            "key2")));
  error = model()->ProcessSyncChanges(FROM_HERE, changes);
  EXPECT_TRUE(error.IsSet());

  // Ensure that the sync changes were not accepted.
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key2"));
  EXPECT_FALSE(model()->GetTemplateURLForKeyword(ASCIIToUTF16("newkeyword")));
}

TEST_F(TemplateURLServiceSyncTest, SyncErrorOnInitialSync) {
  processor()->set_erroneous(true);
  syncer::SyncError error =
      model()->MergeDataAndStartSyncing(
          syncer::SEARCH_ENGINES,
          CreateInitialSyncData(),
          PassProcessor(),
          CreateAndPassSyncErrorFactory()).error();
  EXPECT_TRUE(error.IsSet());

  // Ensure that if the initial merge was erroneous, then subsequence attempts
  // to push data into the local model are rejected, since the model was never
  // successfully associated with Sync in the first place.
  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_UPDATE,
      CreateTestTemplateURL(ASCIIToUTF16("newkeyword"), "http://new.com",
                            "key2")));
  processor()->set_erroneous(false);
  error = model()->ProcessSyncChanges(FROM_HERE, changes);
  EXPECT_TRUE(error.IsSet());

  // Ensure that the sync changes were not accepted.
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key2"));
  EXPECT_FALSE(model()->GetTemplateURLForKeyword(ASCIIToUTF16("newkeyword")));
}

TEST_F(TemplateURLServiceSyncTest, SyncErrorOnLaterSync) {
  // Ensure that if the SyncProcessor succeeds in the initial merge, but fails
  // in future ProcessSyncChanges, we still return an error.
  syncer::SyncError error =
      model()->MergeDataAndStartSyncing(
          syncer::SEARCH_ENGINES,
          CreateInitialSyncData(),
          PassProcessor(),
          CreateAndPassSyncErrorFactory()).error();
  ASSERT_FALSE(error.IsSet());

  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_UPDATE,
      CreateTestTemplateURL(ASCIIToUTF16("newkeyword"), "http://new.com",
                            "key2")));
  processor()->set_erroneous(true);
  error = model()->ProcessSyncChanges(FROM_HERE, changes);
  EXPECT_TRUE(error.IsSet());
}

TEST_F(TemplateURLServiceSyncTest, MergeTwiceWithSameSyncData) {
  // Ensure that a second merge with the same data as the first does not
  // actually update the local data.
  syncer::SyncDataList initial_data;
  initial_data.push_back(CreateInitialSyncData()[0]);

  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://key1.com",
                                     "key1", 10));  // earlier

  syncer::SyncError error =
      model()->MergeDataAndStartSyncing(
          syncer::SEARCH_ENGINES,
          initial_data,
          PassProcessor(),
          CreateAndPassSyncErrorFactory()).error();
  ASSERT_FALSE(error.IsSet());

  // We should have updated the original TemplateURL with Sync's version.
  // Keep a copy of it so we can compare it after we re-merge.
  TemplateURL* key1_url = model()->GetTemplateURLForGUID("key1");
  ASSERT_TRUE(key1_url);
  scoped_ptr<TemplateURL> updated_turl(new TemplateURL(key1_url->profile(),
                                                       key1_url->data()));
  EXPECT_EQ(Time::FromTimeT(90), updated_turl->last_modified());

  // Modify a single field of the initial data. This should not be updated in
  // the second merge, as the last_modified timestamp remains the same.
  scoped_ptr<TemplateURL> temp_turl(Deserialize(initial_data[0]));
  TemplateURLData data(temp_turl->data());
  data.short_name = ASCIIToUTF16("SomethingDifferent");
  temp_turl.reset(new TemplateURL(temp_turl->profile(), data));
  initial_data.clear();
  initial_data.push_back(
      TemplateURLService::CreateSyncDataFromTemplateURL(*temp_turl));

  // Remerge the data again. This simulates shutting down and syncing again
  // at a different time, but the cloud data has not changed.
  model()->StopSyncing(syncer::SEARCH_ENGINES);
  sync_processor_delegate_.reset(new SyncChangeProcessorDelegate(
      sync_processor_.get()));
  error = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES,
      initial_data,
      PassProcessor(),
      CreateAndPassSyncErrorFactory()).error();
  ASSERT_FALSE(error.IsSet());

  // Check that the TemplateURL was not modified.
  const TemplateURL* reupdated_turl = model()->GetTemplateURLForGUID("key1");
  ASSERT_TRUE(reupdated_turl);
  AssertEquals(*updated_turl, *reupdated_turl);
}

TEST_F(TemplateURLServiceSyncTest, SyncedDefaultGUIDArrivesFirst) {
  syncer::SyncDataList initial_data = CreateInitialSyncData();
  // The default search provider should support replacement.
  scoped_ptr<TemplateURL> turl(CreateTestTemplateURL(ASCIIToUTF16("key2"),
      "http://key2.com/{searchTerms}", "key2", 90));
  initial_data[1] = TemplateURLService::CreateSyncDataFromTemplateURL(*turl);
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());
  model()->SetDefaultSearchProvider(model()->GetTemplateURLForGUID("key2"));

  EXPECT_EQ(3U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  const TemplateURL* default_search = model()->GetDefaultSearchProvider();
  ASSERT_TRUE(default_search);

  // Change kSyncedDefaultSearchProviderGUID to a GUID that does not exist in
  // the model yet. Ensure that the default has not changed in any way.
  profile_a()->GetTestingPrefService()->SetString(
      prefs::kSyncedDefaultSearchProviderGUID, "newdefault");

  ASSERT_EQ(default_search, model()->GetDefaultSearchProvider());

  // Bring in a random new search engine with a different GUID. Ensure that
  // it doesn't change the default.
  syncer::SyncChangeList changes1;
  changes1.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
      CreateTestTemplateURL(ASCIIToUTF16("random"), "http://random.com",
                            "random")));
  model()->ProcessSyncChanges(FROM_HERE, changes1);

  EXPECT_EQ(4U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  ASSERT_EQ(default_search, model()->GetDefaultSearchProvider());

  // Finally, bring in the expected entry with the right GUID. Ensure that
  // the default has changed to the new search engine.
  syncer::SyncChangeList changes2;
  changes2.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
      CreateTestTemplateURL(ASCIIToUTF16("new"), "http://new.com/{searchTerms}",
                            "newdefault")));
  model()->ProcessSyncChanges(FROM_HERE, changes2);

  EXPECT_EQ(5U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  ASSERT_NE(default_search, model()->GetDefaultSearchProvider());
  ASSERT_EQ("newdefault", model()->GetDefaultSearchProvider()->sync_guid());
}

TEST_F(TemplateURLServiceSyncTest, DefaultGuidDeletedBeforeNewDSPArrives) {
  syncer::SyncDataList initial_data;
  // The default search provider should support replacement.
  scoped_ptr<TemplateURL> turl1(CreateTestTemplateURL(ASCIIToUTF16("key1"),
      "http://key1.com/{searchTerms}", "key1", 90));
  // Create a second default search provider for the
  // FindNewDefaultSearchProvider method to find.
  TemplateURLData data;
  data.short_name = ASCIIToUTF16("unittest");
  data.SetKeyword(ASCIIToUTF16("key2"));
  data.SetURL("http://key2.com/{searchTerms}");
  data.favicon_url = GURL("http://favicon.url");
  data.safe_for_autoreplace = false;
  data.date_created = Time::FromTimeT(100);
  data.last_modified = Time::FromTimeT(100);
  data.created_by_policy = false;
  data.prepopulate_id = 999999;
  data.sync_guid = "key2";
  data.show_in_default_list = true;
  scoped_ptr<TemplateURL> turl2(new TemplateURL(NULL, data));
  initial_data.push_back(TemplateURLService::CreateSyncDataFromTemplateURL(
      *turl1));
  initial_data.push_back(TemplateURLService::CreateSyncDataFromTemplateURL(
      *turl2));
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());
  model()->SetDefaultSearchProvider(model()->GetTemplateURLForGUID("key1"));
  ASSERT_EQ("key1", model()->GetDefaultSearchProvider()->sync_guid());

  EXPECT_EQ(2U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  const TemplateURL* default_search = model()->GetDefaultSearchProvider();
  ASSERT_TRUE(default_search);

  // Change kSyncedDefaultSearchProviderGUID to a GUID that does not exist in
  // the model yet. Ensure that the default has not changed in any way.
  profile_a()->GetTestingPrefService()->SetString(
      prefs::kSyncedDefaultSearchProviderGUID, "newdefault");

  ASSERT_EQ("key1", model()->GetDefaultSearchProvider()->sync_guid());
  EXPECT_EQ("newdefault", profile_a()->GetTestingPrefService()->GetString(
      prefs::kSyncedDefaultSearchProviderGUID));

  // Simulate a situation where an ACTION_DELETE on the default arrives before
  // the new default search provider entry. This should fail to delete the
  // target entry, and instead send up an "undelete" to the server, after
  // further uniquifying the keyword to avoid infinite sync loops. The synced
  // default GUID should not be changed so that when the expected default entry
  // arrives, it can still be set as the default.
  syncer::SyncChangeList changes1;
  changes1.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_DELETE,
                                          turl1.release()));
  model()->ProcessSyncChanges(FROM_HERE, changes1);

  EXPECT_TRUE(model()->GetTemplateURLForKeyword(ASCIIToUTF16("key1_")));
  EXPECT_EQ(2U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  EXPECT_EQ("key1", model()->GetDefaultSearchProvider()->sync_guid());
  EXPECT_EQ("newdefault", profile_a()->GetTestingPrefService()->GetString(
      prefs::kSyncedDefaultSearchProviderGUID));
  syncer::SyncChange undelete = processor()->change_for_guid("key1");
  EXPECT_EQ(syncer::SyncChange::ACTION_ADD, undelete.change_type());
  EXPECT_EQ("key1_",
            undelete.sync_data().GetSpecifics().search_engine().keyword());

  // Finally, bring in the expected entry with the right GUID. Ensure that
  // the default has changed to the new search engine.
  syncer::SyncChangeList changes2;
  changes2.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
      CreateTestTemplateURL(ASCIIToUTF16("new"), "http://new.com/{searchTerms}",
                            "newdefault")));
  model()->ProcessSyncChanges(FROM_HERE, changes2);

  EXPECT_EQ(3U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  EXPECT_EQ("newdefault", model()->GetDefaultSearchProvider()->sync_guid());
  EXPECT_EQ("newdefault", profile_a()->GetTestingPrefService()->GetString(
      prefs::kSyncedDefaultSearchProviderGUID));
}

TEST_F(TemplateURLServiceSyncTest, SyncedDefaultArrivesAfterStartup) {
  // Start with the default set to something in the model before we start
  // syncing.
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("what"),
                                     "http://thewhat.com/{searchTerms}",
                                     "initdefault"));
  model()->SetDefaultSearchProvider(
      model()->GetTemplateURLForGUID("initdefault"));

  const TemplateURL* default_search = model()->GetDefaultSearchProvider();
  ASSERT_TRUE(default_search);

  // Set kSyncedDefaultSearchProviderGUID to something that is not yet in
  // the model but is expected in the initial sync. Ensure that this doesn't
  // change our default since we're not quite syncing yet.
  profile_a()->GetTestingPrefService()->SetString(
      prefs::kSyncedDefaultSearchProviderGUID, "key2");

  EXPECT_EQ(default_search, model()->GetDefaultSearchProvider());

  // Now sync the initial data, which will include the search engine entry
  // destined to become the new default.
  syncer::SyncDataList initial_data = CreateInitialSyncData();
  // The default search provider should support replacement.
  scoped_ptr<TemplateURL> turl(CreateTestTemplateURL(ASCIIToUTF16("key2"),
      "http://key2.com/{searchTerms}", "key2", 90));
  initial_data[1] = TemplateURLService::CreateSyncDataFromTemplateURL(*turl);

  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  // Ensure that the new default has been set.
  EXPECT_EQ(4U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  ASSERT_NE(default_search, model()->GetDefaultSearchProvider());
  ASSERT_EQ("key2", model()->GetDefaultSearchProvider()->sync_guid());
}

TEST_F(TemplateURLServiceSyncTest, SyncedDefaultAlreadySetOnStartup) {
  // Start with the default set to something in the model before we start
  // syncing.
  const char kGUID[] = "initdefault";
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("what"),
                                     "http://thewhat.com/{searchTerms}",
                                     kGUID));
  model()->SetDefaultSearchProvider(model()->GetTemplateURLForGUID(kGUID));

  const TemplateURL* default_search = model()->GetDefaultSearchProvider();
  ASSERT_TRUE(default_search);

  // Set kSyncedDefaultSearchProviderGUID to the current default.
  profile_a()->GetTestingPrefService()->SetString(
      prefs::kSyncedDefaultSearchProviderGUID, kGUID);

  EXPECT_EQ(default_search, model()->GetDefaultSearchProvider());

  // Now sync the initial data.
  syncer::SyncDataList initial_data = CreateInitialSyncData();
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  // Ensure that the new entries were added and the default has not changed.
  EXPECT_EQ(4U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  ASSERT_EQ(default_search, model()->GetDefaultSearchProvider());
}

TEST_F(TemplateURLServiceSyncTest, NewDefaultIsAlreadySynced) {
  // Ensure that if the synced DSP pref changed to another synced entry (as
  // opposed to coming in as a new entry), it gets reset correctly.
  // Start by setting kSyncedDefaultSearchProviderGUID to the entry that should
  // end up as the default. Note that this must be done before the initial
  // entries are added as otherwise this call will set the DSP immediately.
  profile_a()->GetTestingPrefService()->SetString(
      prefs::kSyncedDefaultSearchProviderGUID, "key2");

  syncer::SyncDataList initial_data = CreateInitialSyncData();
  // Ensure that our candidate default supports replacement.
  scoped_ptr<TemplateURL> turl(CreateTestTemplateURL(ASCIIToUTF16("key2"),
      "http://key2.com/{searchTerms}", "key2", 90));
  initial_data[1] = TemplateURLService::CreateSyncDataFromTemplateURL(*turl);
  for (syncer::SyncDataList::const_iterator iter = initial_data.begin();
      iter != initial_data.end(); ++iter) {
    TemplateURL* converted = Deserialize(*iter);
    model()->Add(converted);
  }

  // Set the initial default to something other than the desired default.
  model()->SetDefaultSearchProvider(model()->GetTemplateURLForGUID("key1"));

  // Merge in the same data (i.e. already synced entries).
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  EXPECT_EQ(3U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  TemplateURL* current_default = model()->GetDefaultSearchProvider();
  ASSERT_TRUE(current_default);
  EXPECT_EQ("key2", current_default->sync_guid());
  EXPECT_EQ(ASCIIToUTF16("key2"), current_default->keyword());
}

TEST_F(TemplateURLServiceSyncTest, SyncWithManagedDefaultSearch) {
  // First start off with a few entries and make sure we can set an unmanaged
  // default search provider.
  syncer::SyncDataList initial_data = CreateInitialSyncData();
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());
  model()->SetDefaultSearchProvider(model()->GetTemplateURLForGUID("key2"));

  EXPECT_EQ(3U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  ASSERT_FALSE(model()->is_default_search_managed());
  ASSERT_TRUE(model()->GetDefaultSearchProvider());

  // Change the default search provider to a managed one.
  const char kName[] = "manageddefault";
  const char kSearchURL[] = "http://manageddefault.com/search?t={searchTerms}";
  const char kIconURL[] = "http://manageddefault.com/icon.jpg";
  const char kEncodings[] = "UTF-16;UTF-32";
  const char kAlternateURL[] =
      "http://manageddefault.com/search#t={searchTerms}";
  const char kSearchTermsReplacementKey[] = "espv";
  test_util_a_.SetManagedDefaultSearchPreferences(true, kName, kName,
      kSearchURL, std::string(), kIconURL, kEncodings, kAlternateURL,
      kSearchTermsReplacementKey);
  const TemplateURL* dsp_turl = model()->GetDefaultSearchProvider();

  EXPECT_TRUE(model()->is_default_search_managed());

  // Add a new entry from Sync. It should still sync in despite the default
  // being managed.
  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
      CreateTestTemplateURL(ASCIIToUTF16("newkeyword"),
                            "http://new.com/{searchTerms}",
                            "newdefault")));
  model()->ProcessSyncChanges(FROM_HERE, changes);

  EXPECT_EQ(4U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());

  // Change kSyncedDefaultSearchProviderGUID to point to the new entry and
  // ensure that the DSP remains managed.
  profile_a()->GetTestingPrefService()->SetString(
      prefs::kSyncedDefaultSearchProviderGUID,
      "newdefault");

  EXPECT_EQ(dsp_turl, model()->GetDefaultSearchProvider());
  EXPECT_TRUE(model()->is_default_search_managed());

  // Go unmanaged. Ensure that the DSP changes to the expected pending entry
  // from Sync.
  const TemplateURL* expected_default =
      model()->GetTemplateURLForGUID("newdefault");
  test_util_a_.RemoveManagedDefaultSearchPreferences();

  EXPECT_EQ(expected_default, model()->GetDefaultSearchProvider());
}

TEST_F(TemplateURLServiceSyncTest, SyncMergeDeletesDefault) {
  // If the value from Sync is a duplicate of the local default and is newer, it
  // should safely replace the local value and set as the new default.
  TemplateURL* default_turl = CreateTestTemplateURL(ASCIIToUTF16("key1"),
      "http://key1.com/{searchTerms}", "whateverguid", 10);
  model()->Add(default_turl);
  model()->SetDefaultSearchProvider(default_turl);

  syncer::SyncDataList initial_data = CreateInitialSyncData();
  // The key1 entry should be a duplicate of the default.
  scoped_ptr<TemplateURL> turl(CreateTestTemplateURL(ASCIIToUTF16("key1"),
      "http://key1.com/{searchTerms}", "key1", 90));
  initial_data[0] = TemplateURLService::CreateSyncDataFromTemplateURL(*turl);

  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  EXPECT_EQ(3U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  EXPECT_FALSE(model()->GetTemplateURLForGUID("whateverguid"));
  EXPECT_EQ(model()->GetDefaultSearchProvider(),
            model()->GetTemplateURLForGUID("key1"));
}

TEST_F(TemplateURLServiceSyncTest, LocalDefaultWinsConflict) {
  // We expect that the local default always wins keyword conflict resolution.
  const string16 keyword(ASCIIToUTF16("key1"));
  const std::string url("http://whatever.com/{searchTerms}");
  TemplateURL* default_turl = CreateTestTemplateURL(keyword,
                                                    url,
                                                    "whateverguid",
                                                    10);
  model()->Add(default_turl);
  model()->SetDefaultSearchProvider(default_turl);

  syncer::SyncDataList initial_data = CreateInitialSyncData();
  // The key1 entry should be different from the default but conflict in the
  // keyword.
  scoped_ptr<TemplateURL> turl(CreateTestTemplateURL(keyword,
      "http://key1.com/{searchTerms}", "key1", 90));
  initial_data[0] = TemplateURLService::CreateSyncDataFromTemplateURL(*turl);

  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  // Since the local default was not yet synced, it should be merged with the
  // conflicting TemplateURL. However, its values should have been preserved
  // since it would have won conflict resolution due to being the default.
  EXPECT_EQ(3U, model()->GetAllSyncData(syncer::SEARCH_ENGINES).size());
  const TemplateURL* winner = model()->GetTemplateURLForGUID("key1");
  ASSERT_TRUE(winner);
  EXPECT_EQ(model()->GetDefaultSearchProvider(), winner);
  EXPECT_EQ(keyword, winner->keyword());
  EXPECT_EQ(url, winner->url());
  ASSERT_TRUE(processor()->contains_guid("key1"));
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE,
            processor()->change_for_guid("key1").change_type());
  EXPECT_EQ(url, GetURL(processor()->change_for_guid("key1").sync_data()));

  // There is no loser, as the two were merged together. The local sync_guid
  // should no longer be found in the model.
  const TemplateURL* loser = model()->GetTemplateURLForGUID("whateverguid");
  ASSERT_FALSE(loser);
}

TEST_F(TemplateURLServiceSyncTest, DeleteBogusData) {
  // Create a couple of bogus entries to sync.
  syncer::SyncDataList initial_data;
  scoped_ptr<TemplateURL> turl(
      CreateTestTemplateURL(ASCIIToUTF16("key1"), "http://key1.com", "key1"));
  initial_data.push_back(
      CreateCustomSyncData(*turl, false, std::string(), turl->sync_guid()));
  turl.reset(CreateTestTemplateURL(ASCIIToUTF16("key2"), "http://key2.com"));
  initial_data.push_back(
      CreateCustomSyncData(*turl, false, turl->url(), std::string()));

  // Now try to sync the data locally.
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());

  // Nothing should have been added, and both bogus entries should be marked for
  // deletion.
  EXPECT_EQ(0U, model()->GetTemplateURLs().size());
  EXPECT_EQ(2U, processor()->change_list_size());
  ASSERT_TRUE(processor()->contains_guid("key1"));
  EXPECT_EQ(syncer::SyncChange::ACTION_DELETE,
            processor()->change_for_guid("key1").change_type());
  ASSERT_TRUE(processor()->contains_guid(std::string()));
  EXPECT_EQ(syncer::SyncChange::ACTION_DELETE,
            processor()->change_for_guid(std::string()).change_type());
}

TEST_F(TemplateURLServiceSyncTest, PreSyncDeletes) {
  model()->pre_sync_deletes_.insert("key1");
  model()->pre_sync_deletes_.insert("key2");
  model()->pre_sync_deletes_.insert("aaa");
  model()->Add(CreateTestTemplateURL(ASCIIToUTF16("whatever"),
      "http://key1.com", "bbb"));
  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES,
      CreateInitialSyncData(), PassProcessor(),
      CreateAndPassSyncErrorFactory());

  // We expect the model to have GUIDs {bbb, key3} after our initial merge.
  EXPECT_TRUE(model()->GetTemplateURLForGUID("bbb"));
  EXPECT_TRUE(model()->GetTemplateURLForGUID("key3"));
  syncer::SyncChange change = processor()->change_for_guid("key1");
  EXPECT_EQ(syncer::SyncChange::ACTION_DELETE, change.change_type());
  change = processor()->change_for_guid("key2");
  EXPECT_EQ(syncer::SyncChange::ACTION_DELETE, change.change_type());
  // "aaa" should have been pruned out on account of not being from Sync.
  EXPECT_FALSE(processor()->contains_guid("aaa"));
  // The set of pre-sync deletes should be cleared so they're not reused if
  // MergeDataAndStartSyncing gets called again.
  EXPECT_TRUE(model()->pre_sync_deletes_.empty());

  // Those sync items deleted via pre-sync-deletes should not get added. The
  // remaining sync item (key3) should though.
  EXPECT_EQ(1, merge_result.num_items_added());
  EXPECT_EQ(0, merge_result.num_items_modified());
  EXPECT_EQ(0, merge_result.num_items_deleted());
  EXPECT_EQ(1, merge_result.num_items_before_association());
  EXPECT_EQ(2, merge_result.num_items_after_association());
}

TEST_F(TemplateURLServiceSyncTest, PreSyncUpdates) {
  const char* kNewKeyword = "somethingnew";
  // Fetch the prepopulate search engines so we know what they are.
  ScopedVector<TemplateURL> prepop_turls;
  size_t default_search_provider_index = 0;
  TemplateURLPrepopulateData::GetPrepopulatedEngines(
      profile_a(), &prepop_turls.get(), &default_search_provider_index);

  // We have to prematurely exit this test if for some reason this machine does
  // not have any prepopulate TemplateURLs.
  ASSERT_FALSE(prepop_turls.empty());

  // Create a copy of the first TemplateURL with a really old timestamp and a
  // new keyword. Add it to the model.
  TemplateURLData data_copy(prepop_turls[0]->data());
  data_copy.last_modified = Time::FromTimeT(10);
  string16 original_keyword = data_copy.keyword();
  data_copy.SetKeyword(ASCIIToUTF16(kNewKeyword));
  // Set safe_for_autoreplace to false so our keyword survives.
  data_copy.safe_for_autoreplace = false;
  model()->Add(new TemplateURL(prepop_turls[0]->profile(), data_copy));

  // Merge the prepopulate search engines.
  base::Time pre_merge_time = base::Time::Now();
  base::RunLoop().RunUntilIdle();
  test_util_a_.ResetModel(true);

  // The newly added search engine should have been safely merged, with an
  // updated time.
  TemplateURL* added_turl = model()->GetTemplateURLForKeyword(
      ASCIIToUTF16(kNewKeyword));
  base::Time new_timestamp = added_turl->last_modified();
  EXPECT_GE(new_timestamp, pre_merge_time);
  ASSERT_TRUE(added_turl);
  std::string sync_guid = added_turl->sync_guid();

  // Bring down a copy of the prepopulate engine from Sync with the old values,
  // including the old timestamp and the same GUID. Ensure that it loses
  // conflict resolution against the local value, and an update is sent to the
  // server. The new timestamp should be preserved.
  syncer::SyncDataList initial_data;
  data_copy.SetKeyword(original_keyword);
  data_copy.sync_guid = sync_guid;
  scoped_ptr<TemplateURL> sync_turl(
      new TemplateURL(prepop_turls[0]->profile(), data_copy));
  initial_data.push_back(
      TemplateURLService::CreateSyncDataFromTemplateURL(*sync_turl));

  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES,
      initial_data, PassProcessor(), CreateAndPassSyncErrorFactory());

  ASSERT_EQ(added_turl, model()->GetTemplateURLForKeyword(
      ASCIIToUTF16(kNewKeyword)));
  EXPECT_EQ(new_timestamp, added_turl->last_modified());
  syncer::SyncChange change = processor()->change_for_guid(sync_guid);
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, change.change_type());
  EXPECT_EQ(kNewKeyword,
            change.sync_data().GetSpecifics().search_engine().keyword());
  EXPECT_EQ(new_timestamp, base::Time::FromInternalValue(
      change.sync_data().GetSpecifics().search_engine().last_modified()));

  // All the sync data is old, so nothing should change locally.
  EXPECT_EQ(0, merge_result.num_items_added());
  EXPECT_EQ(0, merge_result.num_items_modified());
  EXPECT_EQ(0, merge_result.num_items_deleted());
  EXPECT_EQ(static_cast<int>(prepop_turls.size()),
            merge_result.num_items_before_association());
  EXPECT_EQ(static_cast<int>(prepop_turls.size()),
            merge_result.num_items_after_association());
}

TEST_F(TemplateURLServiceSyncTest, SyncBaseURLs) {
  // Verify that bringing in a remote TemplateURL that uses Google base URLs
  // causes it to get a local keyword that matches the local base URL.
  test_util_a_.SetGoogleBaseURL(GURL("http://google.com/"));
  syncer::SyncDataList initial_data;
  scoped_ptr<TemplateURL> turl(CreateTestTemplateURL(
      ASCIIToUTF16("google.co.uk"), "{google:baseURL}search?q={searchTerms}",
      "guid"));
  initial_data.push_back(
      TemplateURLService::CreateSyncDataFromTemplateURL(*turl));
  model()->MergeDataAndStartSyncing(syncer::SEARCH_ENGINES, initial_data,
      PassProcessor(), CreateAndPassSyncErrorFactory());
  TemplateURL* synced_turl = model()->GetTemplateURLForGUID("guid");
  ASSERT_TRUE(synced_turl);
  EXPECT_EQ(ASCIIToUTF16("google.com"), synced_turl->keyword());
  EXPECT_EQ(0U, processor()->change_list_size());

  // Remote updates to this URL's keyword should be silently ignored.
  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_UPDATE,
      CreateTestTemplateURL(ASCIIToUTF16("google.de"),
          "{google:baseURL}search?q={searchTerms}", "guid")));
  model()->ProcessSyncChanges(FROM_HERE, changes);
  EXPECT_EQ(ASCIIToUTF16("google.com"), synced_turl->keyword());
  EXPECT_EQ(0U, processor()->change_list_size());

  // A local change to the Google base URL should update the keyword and
  // generate a sync change.
  test_util_a_.SetGoogleBaseURL(GURL("http://google.co.in/"));
  EXPECT_EQ(ASCIIToUTF16("google.co.in"), synced_turl->keyword());
  EXPECT_EQ(1U, processor()->change_list_size());
  ASSERT_TRUE(processor()->contains_guid("guid"));
  syncer::SyncChange change(processor()->change_for_guid("guid"));
  EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE, change.change_type());
  EXPECT_EQ("google.co.in", GetKeyword(change.sync_data()));
}

TEST_F(TemplateURLServiceSyncTest, MergeInSyncTemplateURL) {
  // An enumeration used to indicate which TemplateURL test value is expected
  // for a particular test result.
  enum ExpectedTemplateURL {
    LOCAL,
    SYNC,
    BOTH,
    NEITHER,
  };

  // Sets up and executes a MergeInSyncTemplateURL test given a number of
  // expected start and end states:
  //  * |conflict_winner| denotes which TemplateURL should win the
  //    conflict.
  //  * |synced_at_start| denotes which of the TemplateURLs should known
  //    to Sync.
  //  * |update_sent| denotes which TemplateURL should have an
  //    ACTION_UPDATE sent to the server after the merge.
  //  * |turl_uniquified| denotes which TemplateURL should have its
  //    keyword updated after the merge.
  //  * |present_in_model| denotes which TemplateURL should be found in
  //    the model after the merge.
  //  * If |keywords_conflict| is true, the TemplateURLs are set up with
  //    the same keyword.
  const struct {
    ExpectedTemplateURL conflict_winner;
    ExpectedTemplateURL synced_at_start;
    ExpectedTemplateURL update_sent;
    ExpectedTemplateURL turl_uniquified;
    ExpectedTemplateURL present_in_model;
    bool keywords_conflict;
    int merge_results[3];  // in Added, Modified, Deleted order.
  } test_cases[] = {
    // Both are synced and the new sync entry is better: Local is uniquified and
    // UPDATE sent. Sync is added.
    {SYNC, BOTH, LOCAL, LOCAL, BOTH, true, {1, 1, 0}},
    // Both are synced and the local entry is better: Sync is uniquified and
    // added to the model. An UPDATE is sent for it.
    {LOCAL, BOTH, SYNC, SYNC, BOTH, true, {1, 1, 0}},
    // Local was not known to Sync and the new sync entry is better: Sync is
    // added. Local is removed. No updates.
    {SYNC, SYNC, NEITHER, NEITHER, SYNC, true, {1, 0, 1}},
    // Local was not known to sync and the local entry is better: Local is
    // updated with sync GUID, Sync is not added. UPDATE sent for Sync.
    {LOCAL, SYNC, SYNC, NEITHER, SYNC, true, {0, 1, 0}},
    // No conflicting keyword. Both should be added with their original
    // keywords, with no updates sent. Note that MergeDataAndStartSyncing is
    // responsible for creating the ACTION_ADD for the local TemplateURL.
    {NEITHER, SYNC, NEITHER, NEITHER, BOTH, false, {1, 0, 0}},
  };

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(test_cases); ++i) {
    // Assert all the valid states of ExpectedTemplateURLs.
    ASSERT_FALSE(test_cases[i].conflict_winner == BOTH);
    ASSERT_FALSE(test_cases[i].synced_at_start == NEITHER);
    ASSERT_FALSE(test_cases[i].synced_at_start == LOCAL);
    ASSERT_FALSE(test_cases[i].update_sent == BOTH);
    ASSERT_FALSE(test_cases[i].turl_uniquified == BOTH);
    ASSERT_FALSE(test_cases[i].present_in_model == NEITHER);

    const string16 local_keyword = ASCIIToUTF16("localkeyword");
    const string16 sync_keyword = test_cases[i].keywords_conflict ?
        local_keyword : ASCIIToUTF16("synckeyword");
    const std::string local_url = "www.localurl.com";
    const std::string sync_url = "www.syncurl.com";
    const time_t local_last_modified = 100;
    const time_t sync_last_modified =
        test_cases[i].conflict_winner == SYNC ? 110 : 90;
    const std::string local_guid = "local_guid";
    const std::string sync_guid = "sync_guid";

    // Initialize expectations.
    string16 expected_local_keyword = local_keyword;
    string16 expected_sync_keyword = sync_keyword;

    // Create the data and run the actual test.
    TemplateURL* local_turl = CreateTestTemplateURL(
        local_keyword, local_url, local_guid, local_last_modified);
    model()->Add(local_turl);
    scoped_ptr<TemplateURL> sync_turl(CreateTestTemplateURL(
        sync_keyword, sync_url, sync_guid, sync_last_modified));

    SyncDataMap sync_data;
    if (test_cases[i].synced_at_start == SYNC ||
        test_cases[i].synced_at_start == BOTH) {
      sync_data[sync_turl->sync_guid()] =
          TemplateURLService::CreateSyncDataFromTemplateURL(*sync_turl);
    }
    if (test_cases[i].synced_at_start == BOTH) {
      sync_data[local_turl->sync_guid()] =
          TemplateURLService::CreateSyncDataFromTemplateURL(*local_turl);
    }
    SyncDataMap initial_data;
    initial_data[local_turl->sync_guid()] =
        TemplateURLService::CreateSyncDataFromTemplateURL(*local_turl);

    syncer::SyncChangeList change_list;
    syncer::SyncMergeResult merge_result(syncer::SEARCH_ENGINES);
    model()->MergeInSyncTemplateURL(sync_turl.get(),
                                    sync_data,
                                    &change_list,
                                    &initial_data,
                                    &merge_result);

    // Verify the merge results were set appropriately.
    EXPECT_EQ(test_cases[i].merge_results[0], merge_result.num_items_added());
    EXPECT_EQ(test_cases[i].merge_results[1],
              merge_result.num_items_modified());
    EXPECT_EQ(test_cases[i].merge_results[2], merge_result.num_items_deleted());

    // Check for expected updates, if any.
    std::string expected_update_guid;
    if (test_cases[i].update_sent == LOCAL)
      expected_update_guid = local_guid;
    else if (test_cases[i].update_sent == SYNC)
      expected_update_guid = sync_guid;
    if (!expected_update_guid.empty()) {
      ASSERT_EQ(1U, change_list.size());
      EXPECT_EQ(expected_update_guid, GetGUID(change_list[0].sync_data()));
      EXPECT_EQ(syncer::SyncChange::ACTION_UPDATE,
                change_list[0].change_type());
    } else {
      EXPECT_EQ(0U, change_list.size());
    }

    // Adjust the expectations based on the expectation enums.
    if (test_cases[i].turl_uniquified == LOCAL) {
      DCHECK(test_cases[i].keywords_conflict);
      expected_local_keyword = ASCIIToUTF16("localkeyword_");
    }
    if (test_cases[i].turl_uniquified == SYNC) {
      DCHECK(test_cases[i].keywords_conflict);
      expected_sync_keyword = ASCIIToUTF16("localkeyword_");
    }

    // Check for TemplateURLs expected in the model. Note that this is checked
    // by GUID rather than the initial pointer, as a merge could occur (the
    // Sync TemplateURL overtakes the local one). Also remove the present
    // TemplateURL when done so the next test case starts with a clean slate.
    if (test_cases[i].present_in_model == LOCAL ||
        test_cases[i].present_in_model == BOTH) {
      ASSERT_TRUE(model()->GetTemplateURLForGUID(local_guid));
      EXPECT_EQ(expected_local_keyword, local_turl->keyword());
      EXPECT_EQ(local_url, local_turl->url());
      EXPECT_EQ(local_last_modified, local_turl->last_modified().ToTimeT());
      model()->Remove(model()->GetTemplateURLForGUID(local_guid));
    }
    if (test_cases[i].present_in_model == SYNC ||
        test_cases[i].present_in_model == BOTH) {
      ASSERT_TRUE(model()->GetTemplateURLForGUID(sync_guid));
      EXPECT_EQ(expected_sync_keyword, sync_turl->keyword());
      EXPECT_EQ(sync_url, sync_turl->url());
      EXPECT_EQ(sync_last_modified, sync_turl->last_modified().ToTimeT());
      model()->Remove(model()->GetTemplateURLForGUID(sync_guid));
    }
  }  // for
}

TEST_F(TemplateURLServiceSyncTest, MergePrepopulatedEngine) {
  scoped_ptr<TemplateURL> default_turl(
      TemplateURLPrepopulateData::GetPrepopulatedDefaultSearch(NULL));

  // Merge with an initial list containing a prepopulated engine with a wrong
  // URL.
  syncer::SyncDataList list;
  scoped_ptr<TemplateURL> sync_turl(CopyTemplateURL(default_turl.get(),
      "http://wrong.url.com?q={searchTerms}", "default"));
  list.push_back(TemplateURLService::CreateSyncDataFromTemplateURL(*sync_turl));
  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES, list, PassProcessor(),
      CreateAndPassSyncErrorFactory());

  const TemplateURL* result_turl = model()->GetTemplateURLForGUID("default");
  EXPECT_TRUE(result_turl);
  EXPECT_EQ(default_turl->keyword(), result_turl->keyword());
  EXPECT_EQ(default_turl->short_name(), result_turl->short_name());
  EXPECT_EQ(default_turl->url(), result_turl->url());
}

TEST_F(TemplateURLServiceSyncTest, AddPrepopulatedEngine) {
  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES, syncer::SyncDataList(), PassProcessor(),
      CreateAndPassSyncErrorFactory());

  scoped_ptr<TemplateURL> default_turl(
      TemplateURLPrepopulateData::GetPrepopulatedDefaultSearch(NULL));
  TemplateURL* sync_turl = CopyTemplateURL(default_turl.get(),
      "http://wrong.url.com?q={searchTerms}", "default");

  // Add a prepopulated engine with a wrong URL.
  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_ADD,
                                         sync_turl));
  model()->ProcessSyncChanges(FROM_HERE, changes);

  const TemplateURL* result_turl = model()->GetTemplateURLForGUID("default");
  EXPECT_TRUE(result_turl);
  EXPECT_EQ(default_turl->keyword(), result_turl->keyword());
  EXPECT_EQ(default_turl->short_name(), result_turl->short_name());
  EXPECT_EQ(default_turl->url(), result_turl->url());
}

TEST_F(TemplateURLServiceSyncTest, UpdatePrepopulatedEngine) {
  scoped_ptr<TemplateURL> default_turl(
      TemplateURLPrepopulateData::GetPrepopulatedDefaultSearch(NULL));

  TemplateURLData data(default_turl->data());
  data.SetURL("http://old.wrong.url.com?q={searchTerms}");
  data.sync_guid = "default";
  model()->Add(new TemplateURL(NULL, data));

  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES, syncer::SyncDataList(), PassProcessor(),
      CreateAndPassSyncErrorFactory());

  TemplateURL* sync_turl = CopyTemplateURL(default_turl.get(),
      "http://new.wrong.url.com?q={searchTerms}", "default");

  // Update the engine in the model, which is prepopulated, with a new one.
  // Both have wrong URLs, but it should still get corrected.
  syncer::SyncChangeList changes;
  changes.push_back(CreateTestSyncChange(syncer::SyncChange::ACTION_UPDATE,
                                         sync_turl));
  model()->ProcessSyncChanges(FROM_HERE, changes);

  const TemplateURL* result_turl = model()->GetTemplateURLForGUID("default");
  EXPECT_TRUE(result_turl);
  EXPECT_EQ(default_turl->keyword(), result_turl->keyword());
  EXPECT_EQ(default_turl->short_name(), result_turl->short_name());
  EXPECT_EQ(default_turl->url(), result_turl->url());
}

TEST_F(TemplateURLServiceSyncTest, MergeEditedPrepopulatedEngine) {
  scoped_ptr<TemplateURL> default_turl(
      TemplateURLPrepopulateData::GetPrepopulatedDefaultSearch(NULL));

  TemplateURLData data(default_turl->data());
  data.safe_for_autoreplace = false;
  data.SetKeyword(ASCIIToUTF16("new_kw"));
  data.short_name = ASCIIToUTF16("my name");
  data.SetURL("http://wrong.url.com?q={searchTerms}");
  data.date_created = Time::FromTimeT(50);
  data.last_modified = Time::FromTimeT(50);
  data.sync_guid = "default";
  model()->Add(new TemplateURL(NULL, data));

  data.date_created = Time::FromTimeT(100);
  data.last_modified = Time::FromTimeT(100);
  scoped_ptr<TemplateURL> sync_turl(new TemplateURL(NULL, data));
  syncer::SyncDataList list;
  list.push_back(TemplateURLService::CreateSyncDataFromTemplateURL(*sync_turl));
  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES, list, PassProcessor(),
      CreateAndPassSyncErrorFactory());

  const TemplateURL* result_turl = model()->GetTemplateURLForGUID("default");
  EXPECT_TRUE(result_turl);
  EXPECT_EQ(ASCIIToUTF16("new_kw"), result_turl->keyword());
  EXPECT_EQ(ASCIIToUTF16("my name"), result_turl->short_name());
  EXPECT_EQ(default_turl->url(), result_turl->url());
}

TEST_F(TemplateURLServiceSyncTest, MergeNonEditedPrepopulatedEngine) {
  scoped_ptr<TemplateURL> default_turl(
      TemplateURLPrepopulateData::GetPrepopulatedDefaultSearch(NULL));

  TemplateURLData data(default_turl->data());
  data.safe_for_autoreplace = true;  // Can be replaced with built-in values.
  data.SetKeyword(ASCIIToUTF16("new_kw"));
  data.short_name = ASCIIToUTF16("my name");
  data.SetURL("http://wrong.url.com?q={searchTerms}");
  data.date_created = Time::FromTimeT(50);
  data.last_modified = Time::FromTimeT(50);
  data.sync_guid = "default";
  model()->Add(new TemplateURL(NULL, data));

  data.date_created = Time::FromTimeT(100);
  data.last_modified = Time::FromTimeT(100);
  scoped_ptr<TemplateURL> sync_turl(new TemplateURL(NULL, data));
  syncer::SyncDataList list;
  list.push_back(TemplateURLService::CreateSyncDataFromTemplateURL(*sync_turl));
  syncer::SyncMergeResult merge_result = model()->MergeDataAndStartSyncing(
      syncer::SEARCH_ENGINES, list, PassProcessor(),
      CreateAndPassSyncErrorFactory());

  const TemplateURL* result_turl = model()->GetTemplateURLForGUID("default");
  EXPECT_TRUE(result_turl);
  EXPECT_EQ(default_turl->keyword(), result_turl->keyword());
  EXPECT_EQ(default_turl->short_name(), result_turl->short_name());
  EXPECT_EQ(default_turl->url(), result_turl->url());
}
