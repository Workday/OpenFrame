// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/history/history_backend.h"
#include "chrome/browser/history/history_db_task.h"
#include "chrome/browser/history/history_notifications.h"
#include "chrome/browser/history/history_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/history/history_types.h"
#include "chrome/browser/invalidation/invalidation_service_factory.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/signin/token_service_factory.h"
#include "chrome/browser/sync/abstract_profile_sync_service_test.h"
#include "chrome/browser/sync/glue/data_type_error_handler_mock.h"
#include "chrome/browser/sync/glue/sync_backend_host.h"
#include "chrome/browser/sync/glue/typed_url_change_processor.h"
#include "chrome/browser/sync/glue/typed_url_data_type_controller.h"
#include "chrome/browser/sync/glue/typed_url_model_associator.h"
#include "chrome/browser/sync/profile_sync_components_factory.h"
#include "chrome/browser/sync/profile_sync_components_factory_mock.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/sync/profile_sync_test_util.h"
#include "chrome/browser/sync/test_profile_sync_service.h"
#include "chrome/test/base/profile_mock.h"
#include "chrome/test/base/testing_profile.h"
#include "components/browser_context_keyed_service/refcounted_browser_context_keyed_service.h"
#include "content/public/browser/notification_service.h"
#include "google_apis/gaia/gaia_constants.h"
#include "sync/internal_api/public/read_node.h"
#include "sync/internal_api/public/read_transaction.h"
#include "sync/internal_api/public/write_node.h"
#include "sync/internal_api/public/write_transaction.h"
#include "sync/protocol/typed_url_specifics.pb.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "url/gurl.h"

using base::Time;
using base::Thread;
using browser_sync::TypedUrlChangeProcessor;
using browser_sync::TypedUrlDataTypeController;
using browser_sync::TypedUrlModelAssociator;
using history::HistoryBackend;
using history::URLID;
using history::URLRow;
using syncer::syncable::WriteTransaction;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgumentPointee;

namespace {
// Visits with this timestamp are treated as expired.
static const int EXPIRED_VISIT = -1;

class HistoryBackendMock : public HistoryBackend {
 public:
  HistoryBackendMock() : HistoryBackend(base::FilePath(), 0, NULL, NULL) {}
  virtual bool IsExpiredVisitTime(const base::Time& time) OVERRIDE {
    return time.ToInternalValue() == EXPIRED_VISIT;
  }
  MOCK_METHOD1(GetAllTypedURLs, bool(history::URLRows* entries));
  MOCK_METHOD3(GetMostRecentVisitsForURL, bool(history::URLID id,
                                               int max_visits,
                                               history::VisitVector* visits));
  MOCK_METHOD2(UpdateURL, bool(history::URLID id, const history::URLRow& url));
  MOCK_METHOD3(AddVisits, bool(const GURL& url,
                               const std::vector<history::VisitInfo>& visits,
                               history::VisitSource visit_source));
  MOCK_METHOD1(RemoveVisits, bool(const history::VisitVector& visits));
  MOCK_METHOD2(GetURL, bool(const GURL& url_id, history::URLRow* url_row));
  MOCK_METHOD2(SetPageTitle, void(const GURL& url, const string16& title));
  MOCK_METHOD1(DeleteURL, void(const GURL& url));

 private:
  virtual ~HistoryBackendMock() {}
};

class HistoryServiceMock : public HistoryService {
 public:
  explicit HistoryServiceMock(Profile* profile) : HistoryService(profile) {}
  MOCK_METHOD2(ScheduleDBTask, void(history::HistoryDBTask*,
                                    CancelableRequestConsumerBase*));
  MOCK_METHOD0(Shutdown, void());

  void ShutdownBaseService() {
    HistoryService::Shutdown();
  }

 private:
  virtual ~HistoryServiceMock() {}
};

BrowserContextKeyedService* BuildHistoryService(
    content::BrowserContext* profile) {
  return new HistoryServiceMock(static_cast<Profile*>(profile));
}

class TestTypedUrlModelAssociator : public TypedUrlModelAssociator {
 public:
  TestTypedUrlModelAssociator(
      ProfileSyncService* sync_service,
      history::HistoryBackend* history_backend,
      browser_sync::DataTypeErrorHandler* error_handler) :
      TypedUrlModelAssociator(sync_service, history_backend, error_handler) {}

 protected:
  // Don't clear error stats - that way we can verify their values in our
  // tests.
  virtual void ClearErrorStats() OVERRIDE {}
};

void RunOnDBThreadCallback(HistoryBackend* backend,
                           history::HistoryDBTask* task) {
  task->RunOnDBThread(backend, NULL);
}

ACTION_P2(RunTaskOnDBThread, thread, backend) {
  // ScheduleDBTask takes ownership of its task argument, so we
  // should, too.
  scoped_refptr<history::HistoryDBTask> task(arg0);
  thread->message_loop()->PostTask(
      FROM_HERE, base::Bind(&RunOnDBThreadCallback, base::Unretained(backend),
                            task));
}

ACTION_P2(ShutdownHistoryService, thread, service) {
  service->ShutdownBaseService();
  delete thread;
}

ACTION_P6(MakeTypedUrlSyncComponents,
              profile,
              service,
              hb,
              dtc,
              error_handler,
              model_associator) {
  *model_associator =
      new TestTypedUrlModelAssociator(service, hb, error_handler);
  TypedUrlChangeProcessor* change_processor =
      new TypedUrlChangeProcessor(profile, *model_associator, hb, dtc);
  return ProfileSyncComponentsFactory::SyncComponents(*model_associator,
                                                      change_processor);
}

class ProfileSyncServiceTypedUrlTest : public AbstractProfileSyncServiceTest {
 public:
  void AddTypedUrlSyncNode(const history::URLRow& url,
                           const history::VisitVector& visits) {
    syncer::WriteTransaction trans(FROM_HERE, sync_service_->GetUserShare());
    syncer::ReadNode typed_url_root(&trans);
    ASSERT_EQ(syncer::BaseNode::INIT_OK,
              typed_url_root.InitByTagLookup(browser_sync::kTypedUrlTag));

    syncer::WriteNode node(&trans);
    std::string tag = url.url().spec();
    syncer::WriteNode::InitUniqueByCreationResult result =
        node.InitUniqueByCreation(syncer::TYPED_URLS, typed_url_root, tag);
    ASSERT_EQ(syncer::WriteNode::INIT_SUCCESS, result);
    TypedUrlModelAssociator::WriteToSyncNode(url, visits, &node);
  }

 protected:
  ProfileSyncServiceTypedUrlTest() {
    history_thread_.reset(new Thread("history"));
  }

  virtual void SetUp() {
    AbstractProfileSyncServiceTest::SetUp();
    profile_.reset(new ProfileMock());
    invalidation::InvalidationServiceFactory::GetInstance()->
        SetBuildOnlyFakeInvalidatorsForTest(true);
    history_backend_ = new HistoryBackendMock();
    history_service_ = static_cast<HistoryServiceMock*>(
        HistoryServiceFactory::GetInstance()->SetTestingFactoryAndUse(
            profile_.get(), BuildHistoryService));
    EXPECT_CALL((*history_service_), ScheduleDBTask(_, _))
        .WillRepeatedly(RunTaskOnDBThread(history_thread_.get(),
                                          history_backend_.get()));
    history_thread_->Start();
  }

  virtual void TearDown() {
    EXPECT_CALL((*history_service_), Shutdown())
        .WillOnce(ShutdownHistoryService(history_thread_.release(),
                                         history_service_));
    profile_.reset();
    AbstractProfileSyncServiceTest::TearDown();
  }

  TypedUrlModelAssociator* StartSyncService(const base::Closure& callback) {
    TypedUrlModelAssociator* model_associator = NULL;
    if (!sync_service_) {
      SigninManagerBase* signin =
          SigninManagerFactory::GetForProfile(profile_.get());
      signin->SetAuthenticatedUsername("test");
      token_service_ = static_cast<TokenService*>(
          TokenServiceFactory::GetInstance()->SetTestingFactoryAndUse(
              profile_.get(), BuildTokenService));
      ProfileOAuth2TokenServiceFactory::GetInstance()->SetTestingFactory(
          profile_.get(), FakeOAuth2TokenService::BuildTokenService);
      sync_service_ = static_cast<TestProfileSyncService*>(
          ProfileSyncServiceFactory::GetInstance()->SetTestingFactoryAndUse(
              profile_.get(),
              &TestProfileSyncService::BuildAutoStartAsyncInit));
      sync_service_->set_backend_init_callback(callback);
      ProfileSyncComponentsFactoryMock* components =
          sync_service_->components_factory_mock();
      TypedUrlDataTypeController* data_type_controller =
          new TypedUrlDataTypeController(components,
                                         profile_.get(),
                                         sync_service_);

      EXPECT_CALL(*components, CreateTypedUrlSyncComponents(_, _, _)).
          WillOnce(MakeTypedUrlSyncComponents(profile_.get(),
                                              sync_service_,
                                              history_backend_.get(),
                                              data_type_controller,
                                              &error_handler_,
                                              &model_associator));
      EXPECT_CALL(*components, CreateDataTypeManager(_, _, _, _, _, _)).
          WillOnce(ReturnNewDataTypeManager());

      token_service_->IssueAuthTokenForTest(
          GaiaConstants::kGaiaOAuth2LoginRefreshToken, "oauth2_login_token");
      token_service_->IssueAuthTokenForTest(
          GaiaConstants::kSyncService, "token");

      sync_service_->RegisterDataTypeController(data_type_controller);

      sync_service_->Initialize();
      base::MessageLoop::current()->Run();
    }
    return model_associator;
  }

  void GetTypedUrlsFromSyncDB(history::URLRows* urls) {
    urls->clear();
    syncer::ReadTransaction trans(FROM_HERE, sync_service_->GetUserShare());
    syncer::ReadNode typed_url_root(&trans);
    if (typed_url_root.InitByTagLookup(browser_sync::kTypedUrlTag) !=
            syncer::BaseNode::INIT_OK)
      return;

    int64 child_id = typed_url_root.GetFirstChildId();
    while (child_id != syncer::kInvalidId) {
      syncer::ReadNode child_node(&trans);
      if (child_node.InitByIdLookup(child_id) != syncer::BaseNode::INIT_OK)
        return;

      const sync_pb::TypedUrlSpecifics& typed_url(
          child_node.GetTypedUrlSpecifics());
      history::URLRow new_url(GURL(typed_url.url()));

      new_url.set_title(UTF8ToUTF16(typed_url.title()));
      DCHECK(typed_url.visits_size());
      DCHECK_EQ(typed_url.visits_size(), typed_url.visit_transitions_size());
      new_url.set_last_visit(base::Time::FromInternalValue(
          typed_url.visits(typed_url.visits_size() - 1)));
      new_url.set_hidden(typed_url.hidden());

      urls->push_back(new_url);
      child_id = child_node.GetSuccessorId();
    }
  }

  void SetIdleChangeProcessorExpectations() {
    EXPECT_CALL((*history_backend_.get()), SetPageTitle(_, _)).Times(0);
    EXPECT_CALL((*history_backend_.get()), UpdateURL(_, _)).Times(0);
    EXPECT_CALL((*history_backend_.get()), GetURL(_, _)).Times(0);
    EXPECT_CALL((*history_backend_.get()), DeleteURL(_)).Times(0);
  }

  static bool URLsEqual(history::URLRow& lhs, history::URLRow& rhs) {
    // Only verify the fields we explicitly sync (i.e. don't verify typed_count
    // or visit_count because we rely on the history DB to manage those values
    // and they are left unchanged by HistoryBackendMock).
    return (lhs.url().spec().compare(rhs.url().spec()) == 0) &&
           (lhs.title().compare(rhs.title()) == 0) &&
           (lhs.last_visit() == rhs.last_visit()) &&
           (lhs.hidden() == rhs.hidden());
  }

  static history::URLRow MakeTypedUrlEntry(const char* url,
                                           const char* title,
                                           int typed_count,
                                           int64 last_visit,
                                           bool hidden,
                                           history::VisitVector* visits) {
    // Give each URL a unique ID, to mimic the behavior of the real database.
    static int unique_url_id = 0;
    GURL gurl(url);
    URLRow history_url(gurl, ++unique_url_id);
    history_url.set_title(UTF8ToUTF16(title));
    history_url.set_typed_count(typed_count);
    history_url.set_last_visit(
        base::Time::FromInternalValue(last_visit));
    history_url.set_hidden(hidden);
    visits->push_back(history::VisitRow(
        history_url.id(), history_url.last_visit(), 0,
        content::PAGE_TRANSITION_TYPED, 0));
    history_url.set_visit_count(visits->size());
    return history_url;
  }

  scoped_ptr<Thread> history_thread_;

  scoped_ptr<ProfileMock> profile_;
  scoped_refptr<HistoryBackendMock> history_backend_;
  HistoryServiceMock* history_service_;
  browser_sync::DataTypeErrorHandlerMock error_handler_;
};

void AddTypedUrlEntries(ProfileSyncServiceTypedUrlTest* test,
                        const history::URLRows& entries) {
  test->CreateRoot(syncer::TYPED_URLS);
  for (size_t i = 0; i < entries.size(); ++i) {
    history::VisitVector visits;
    visits.push_back(history::VisitRow(
        entries[i].id(), entries[i].last_visit(), 0,
        content::PageTransitionFromInt(0), 0));
    test->AddTypedUrlSyncNode(entries[i], visits);
  }
}

} // namespace

TEST_F(ProfileSyncServiceTypedUrlTest, EmptyNativeEmptySync) {
  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(Return(true));
  SetIdleChangeProcessorExpectations();
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  TypedUrlModelAssociator* associator =
      StartSyncService(create_root.callback());
  history::URLRows sync_entries;
  GetTypedUrlsFromSyncDB(&sync_entries);
  EXPECT_EQ(0U, sync_entries.size());
  ASSERT_EQ(0, associator->GetErrorPercentage());
}

TEST_F(ProfileSyncServiceTypedUrlTest, HasNativeEmptySync) {
  history::URLRows entries;
  history::VisitVector visits;
  entries.push_back(MakeTypedUrlEntry("http://foo.com", "bar",
                                      2, 15, false, &visits));

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(entries), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(visits), Return(true)));
  SetIdleChangeProcessorExpectations();
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  TypedUrlModelAssociator* associator =
      StartSyncService(create_root.callback());
  history::URLRows sync_entries;
  GetTypedUrlsFromSyncDB(&sync_entries);
  ASSERT_EQ(1U, sync_entries.size());
  EXPECT_TRUE(URLsEqual(entries[0], sync_entries[0]));
  ASSERT_EQ(0, associator->GetErrorPercentage());
}

TEST_F(ProfileSyncServiceTypedUrlTest, HasNativeErrorReadingVisits) {
  history::URLRows entries;
  history::VisitVector visits;
  history::URLRow native_entry1(MakeTypedUrlEntry("http://foo.com", "bar",
                                                  2, 15, false, &visits));
  history::URLRow native_entry2(MakeTypedUrlEntry("http://foo2.com", "bar",
                                                  3, 15, false, &visits));
  entries.push_back(native_entry1);
  entries.push_back(native_entry2);
  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(entries), Return(true)));
  // Return an error from GetMostRecentVisitsForURL() for the second URL.
  EXPECT_CALL((*history_backend_.get()),
              GetMostRecentVisitsForURL(native_entry1.id(), _, _)).
                  WillRepeatedly(Return(true));
  EXPECT_CALL((*history_backend_.get()),
              GetMostRecentVisitsForURL(native_entry2.id(), _, _)).
                  WillRepeatedly(Return(false));
  SetIdleChangeProcessorExpectations();
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());
  history::URLRows sync_entries;
  GetTypedUrlsFromSyncDB(&sync_entries);
  ASSERT_EQ(1U, sync_entries.size());
  EXPECT_TRUE(URLsEqual(native_entry1, sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, HasNativeWithBlankEmptySync) {
  std::vector<history::URLRow> entries;
  history::VisitVector visits;
  // Add an empty URL.
  entries.push_back(MakeTypedUrlEntry("", "bar",
                                      2, 15, false, &visits));
  entries.push_back(MakeTypedUrlEntry("http://foo.com", "bar",
                                      2, 15, false, &visits));
  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(entries), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(visits), Return(true)));
  SetIdleChangeProcessorExpectations();
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());
  std::vector<history::URLRow> sync_entries;
  GetTypedUrlsFromSyncDB(&sync_entries);
  // The empty URL should be ignored.
  ASSERT_EQ(1U, sync_entries.size());
  EXPECT_TRUE(URLsEqual(entries[1], sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, HasNativeHasSyncNoMerge) {
  history::VisitVector native_visits;
  history::VisitVector sync_visits;
  history::URLRow native_entry(MakeTypedUrlEntry("http://native.com", "entry",
                                                 2, 15, false, &native_visits));
  history::URLRow sync_entry(MakeTypedUrlEntry("http://sync.com", "entry",
                                               3, 16, false, &sync_visits));

  history::URLRows native_entries;
  native_entries.push_back(native_entry);
  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(native_entries), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(native_visits), Return(true)));
  EXPECT_CALL((*history_backend_.get()),
      AddVisits(_, _, history::SOURCE_SYNCED)).WillRepeatedly(Return(true));

  history::URLRows sync_entries;
  sync_entries.push_back(sync_entry);

  EXPECT_CALL((*history_backend_.get()), UpdateURL(_, _)).
      WillRepeatedly(Return(true));
  StartSyncService(base::Bind(&AddTypedUrlEntries, this, sync_entries));

  std::map<std::string, history::URLRow> expected;
  expected[native_entry.url().spec()] = native_entry;
  expected[sync_entry.url().spec()] = sync_entry;

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);

  EXPECT_TRUE(new_sync_entries.size() == expected.size());
  for (history::URLRows::iterator entry = new_sync_entries.begin();
       entry != new_sync_entries.end(); ++entry) {
    EXPECT_TRUE(URLsEqual(expected[entry->url().spec()], *entry));
  }
}

TEST_F(ProfileSyncServiceTypedUrlTest, EmptyNativeExpiredSync) {
  history::VisitVector sync_visits;
  history::URLRow sync_entry(MakeTypedUrlEntry("http://sync.com", "entry",
                                               3, EXPIRED_VISIT, false,
                                               &sync_visits));
  history::URLRows sync_entries;
  sync_entries.push_back(sync_entry);

  // Since all our URLs are expired, no backend calls to add new URLs will be
  // made.
  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(Return(true));
  SetIdleChangeProcessorExpectations();

  StartSyncService(base::Bind(&AddTypedUrlEntries, this, sync_entries));
}

TEST_F(ProfileSyncServiceTypedUrlTest, HasNativeHasSyncMerge) {
  history::VisitVector native_visits;
  history::URLRow native_entry(MakeTypedUrlEntry("http://native.com", "entry",
                                                 2, 15, false, &native_visits));
  history::VisitVector sync_visits;
  history::URLRow sync_entry(MakeTypedUrlEntry("http://native.com", "name",
                                               1, 17, false, &sync_visits));
  history::VisitVector merged_visits;
  merged_visits.push_back(history::VisitRow(
      sync_entry.id(), base::Time::FromInternalValue(15), 0,
      content::PageTransitionFromInt(0), 0));

  history::URLRow merged_entry(MakeTypedUrlEntry("http://native.com", "name",
                                                 2, 17, false, &merged_visits));

  history::URLRows native_entries;
  native_entries.push_back(native_entry);
  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(native_entries), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(native_visits), Return(true)));
  EXPECT_CALL((*history_backend_.get()),
      AddVisits(_, _, history::SOURCE_SYNCED)). WillRepeatedly(Return(true));

  history::URLRows sync_entries;
  sync_entries.push_back(sync_entry);

  EXPECT_CALL((*history_backend_.get()), UpdateURL(_, _)).
      WillRepeatedly(Return(true));
  EXPECT_CALL((*history_backend_.get()), SetPageTitle(_, _)).
      WillRepeatedly(Return());
  StartSyncService(base::Bind(&AddTypedUrlEntries, this, sync_entries));

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(merged_entry, new_sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, HasNativeWithErrorHasSyncMerge) {
  history::VisitVector native_visits;
  history::URLRow native_entry(MakeTypedUrlEntry("http://native.com", "native",
                                                 2, 15, false, &native_visits));
  history::VisitVector sync_visits;
  history::URLRow sync_entry(MakeTypedUrlEntry("http://native.com", "sync",
                                               1, 17, false, &sync_visits));

  history::URLRows native_entries;
  native_entries.push_back(native_entry);
  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(native_entries), Return(true)));
  // Return an error getting the visits for the native URL.
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(Return(false));
  EXPECT_CALL((*history_backend_.get()), GetURL(_, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<1>(native_entry), Return(true)));
  EXPECT_CALL((*history_backend_.get()),
      AddVisits(_, _, history::SOURCE_SYNCED)). WillRepeatedly(Return(true));

  history::URLRows sync_entries;
  sync_entries.push_back(sync_entry);

  EXPECT_CALL((*history_backend_.get()), UpdateURL(_, _)).
      WillRepeatedly(Return(true));
  EXPECT_CALL((*history_backend_.get()), SetPageTitle(_, _)).
      WillRepeatedly(Return());
  StartSyncService(base::Bind(&AddTypedUrlEntries, this, sync_entries));

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(sync_entry, new_sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, ProcessUserChangeAdd) {
  history::VisitVector added_visits;
  history::URLRow added_entry(MakeTypedUrlEntry("http://added.com", "entry",
                                                2, 15, false, &added_visits));

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(Return(true));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillOnce(DoAll(SetArgumentPointee<2>(added_visits), Return(true)));

  SetIdleChangeProcessorExpectations();
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());

  history::URLsModifiedDetails details;
  details.changed_urls.push_back(added_entry);
  scoped_refptr<ThreadNotifier> notifier(
      new ThreadNotifier(history_thread_.get()));
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URLS_MODIFIED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLsModifiedDetails>(&details));

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(added_entry, new_sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, ProcessUserChangeAddWithBlank) {
  history::VisitVector added_visits;
  history::URLRow empty_entry(MakeTypedUrlEntry("", "entry",
                                                2, 15, false, &added_visits));
  history::URLRow added_entry(MakeTypedUrlEntry("http://added.com", "entry",
                                                2, 15, false, &added_visits));

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(Return(true));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(added_visits), Return(true)));

  SetIdleChangeProcessorExpectations();
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());

  history::URLsModifiedDetails details;
  details.changed_urls.push_back(empty_entry);
  details.changed_urls.push_back(added_entry);
  scoped_refptr<ThreadNotifier> notifier(
      new ThreadNotifier(history_thread_.get()));
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URLS_MODIFIED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLsModifiedDetails>(&details));

  std::vector<history::URLRow> new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(added_entry, new_sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, ProcessUserChangeUpdate) {
  history::VisitVector original_visits;
  history::URLRow original_entry(MakeTypedUrlEntry("http://mine.com", "entry",
                                                   2, 15, false,
                                                   &original_visits));
  history::URLRows original_entries;
  original_entries.push_back(original_entry);

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(original_entries), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillOnce(DoAll(SetArgumentPointee<2>(original_visits),
                     Return(true)));
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());

  history::VisitVector updated_visits;
  history::URLRow updated_entry(MakeTypedUrlEntry("http://mine.com", "entry",
                                                  7, 17, false,
                                                  &updated_visits));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillOnce(DoAll(SetArgumentPointee<2>(updated_visits),
                     Return(true)));

  history::URLsModifiedDetails details;
  details.changed_urls.push_back(updated_entry);
  scoped_refptr<ThreadNotifier> notifier(
      new ThreadNotifier(history_thread_.get()));
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URLS_MODIFIED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLsModifiedDetails>(&details));

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(updated_entry, new_sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, ProcessUserChangeAddFromVisit) {
  history::VisitVector added_visits;
  history::URLRow added_entry(MakeTypedUrlEntry("http://added.com", "entry",
                                                2, 15, false, &added_visits));

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(Return(true));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillOnce(DoAll(SetArgumentPointee<2>(added_visits), Return(true)));

  SetIdleChangeProcessorExpectations();
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());

  history::URLVisitedDetails details;
  details.row = added_entry;
  details.transition = content::PAGE_TRANSITION_TYPED;
  scoped_refptr<ThreadNotifier> notifier(
      new ThreadNotifier(history_thread_.get()));
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URL_VISITED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLVisitedDetails>(&details));

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(added_entry, new_sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, ProcessUserChangeUpdateFromVisit) {
  history::VisitVector original_visits;
  history::URLRow original_entry(MakeTypedUrlEntry("http://mine.com", "entry",
                                                   2, 15, false,
                                                   &original_visits));
  history::URLRows original_entries;
  original_entries.push_back(original_entry);

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(original_entries), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillOnce(DoAll(SetArgumentPointee<2>(original_visits),
                           Return(true)));
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());

  history::VisitVector updated_visits;
  history::URLRow updated_entry(MakeTypedUrlEntry("http://mine.com", "entry",
                                                  7, 17, false,
                                                  &updated_visits));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillOnce(DoAll(SetArgumentPointee<2>(updated_visits),
                           Return(true)));

  history::URLVisitedDetails details;
  details.row = updated_entry;
  details.transition = content::PAGE_TRANSITION_TYPED;
  scoped_refptr<ThreadNotifier> notifier(
      new ThreadNotifier(history_thread_.get()));
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URL_VISITED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLVisitedDetails>(&details));

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(updated_entry, new_sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, ProcessUserIgnoreChangeUpdateFromVisit) {
  history::VisitVector original_visits;
  history::URLRow original_entry(MakeTypedUrlEntry("http://mine.com", "entry",
                                                   2, 15, false,
                                                   &original_visits));
  history::URLRows original_entries;
  original_entries.push_back(original_entry);

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(original_entries), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(original_visits),
                           Return(true)));
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());
  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(original_entry, new_sync_entries[0]));

  history::VisitVector updated_visits;
  history::URLRow updated_entry(MakeTypedUrlEntry("http://mine.com", "entry",
                                                  7, 15, false,
                                                  &updated_visits));
  history::URLVisitedDetails details;
  details.row = updated_entry;

  // Should ignore this change because it's not TYPED.
  details.transition = content::PAGE_TRANSITION_RELOAD;
  scoped_refptr<ThreadNotifier> notifier(
      new ThreadNotifier(history_thread_.get()));
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URL_VISITED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLVisitedDetails>(&details));

  GetTypedUrlsFromSyncDB(&new_sync_entries);

  // Should be no changes to the sync DB from this notification.
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(original_entry, new_sync_entries[0]));

  // Now, try updating it with a large number of visits not divisible by 10
  // (should ignore this visit).
  history::URLRow twelve_visits(MakeTypedUrlEntry("http://mine.com", "entry",
                                                  12, 15, false,
                                                  &updated_visits));
  details.row = twelve_visits;
  details.transition = content::PAGE_TRANSITION_TYPED;
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URL_VISITED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLVisitedDetails>(&details));
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  // Should be no changes to the sync DB from this notification.
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(original_entry, new_sync_entries[0]));

  // Now, try updating it with a large number of visits that is divisible by 10
  // (should *not* be ignored).
  history::URLRow twenty_visits(MakeTypedUrlEntry("http://mine.com", "entry",
                                                  20, 15, false,
                                                  &updated_visits));
  details.row = twenty_visits;
  details.transition = content::PAGE_TRANSITION_TYPED;
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URL_VISITED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLVisitedDetails>(&details));
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(twenty_visits, new_sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, ProcessUserChangeRemove) {
  history::VisitVector original_visits1;
  history::URLRow original_entry1(MakeTypedUrlEntry("http://mine.com", "entry",
                                                    2, 15, false,
                                                    &original_visits1));
  history::VisitVector original_visits2;
  history::URLRow original_entry2(MakeTypedUrlEntry("http://mine2.com",
                                                    "entry2",
                                                    3, 15, false,
                                                    &original_visits2));
  history::URLRows original_entries;
  original_entries.push_back(original_entry1);
  original_entries.push_back(original_entry2);

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(original_entries), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(original_visits1),
                           Return(true)));
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());

  history::URLsDeletedDetails changes;
  changes.all_history = false;
  changes.rows.push_back(history::URLRow(GURL("http://mine.com")));
  scoped_refptr<ThreadNotifier> notifier(
      new ThreadNotifier(history_thread_.get()));
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URLS_DELETED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLsDeletedDetails>(&changes));

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(original_entry2, new_sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, ProcessUserChangeRemoveArchive) {
  history::VisitVector original_visits1;
  history::URLRow original_entry1(MakeTypedUrlEntry("http://mine.com", "entry",
                                                    2, 15, false,
                                                    &original_visits1));
  history::VisitVector original_visits2;
  history::URLRow original_entry2(MakeTypedUrlEntry("http://mine2.com",
                                                    "entry2",
                                                    3, 15, false,
                                                    &original_visits2));
  history::URLRows original_entries;
  original_entries.push_back(original_entry1);
  original_entries.push_back(original_entry2);

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(original_entries), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(original_visits1),
                           Return(true)));
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());

  history::URLsDeletedDetails changes;
  changes.all_history = false;
  // Setting archived=true should cause the sync code to ignore this deletion.
  changes.archived = true;
  changes.rows.push_back(history::URLRow(GURL("http://mine.com")));
  scoped_refptr<ThreadNotifier> notifier(
      new ThreadNotifier(history_thread_.get()));
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URLS_DELETED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLsDeletedDetails>(&changes));

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  // Both URLs should still be there.
  ASSERT_EQ(2U, new_sync_entries.size());
}

TEST_F(ProfileSyncServiceTypedUrlTest, ProcessUserChangeRemoveAll) {
  history::VisitVector original_visits1;
  history::URLRow original_entry1(MakeTypedUrlEntry("http://mine.com", "entry",
                                                    2, 15, false,
                                                    &original_visits1));
  history::VisitVector original_visits2;
  history::URLRow original_entry2(MakeTypedUrlEntry("http://mine2.com",
                                                    "entry2",
                                                    3, 15, false,
                                                    &original_visits2));
  history::URLRows original_entries;
  original_entries.push_back(original_entry1);
  original_entries.push_back(original_entry2);

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(original_entries), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(original_visits1),
                           Return(true)));
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(2U, new_sync_entries.size());

  history::URLsDeletedDetails changes;
  changes.all_history = true;
  scoped_refptr<ThreadNotifier> notifier(
      new ThreadNotifier(history_thread_.get()));
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URLS_DELETED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLsDeletedDetails>(&changes));

  GetTypedUrlsFromSyncDB(&new_sync_entries);
  ASSERT_EQ(0U, new_sync_entries.size());
}

TEST_F(ProfileSyncServiceTypedUrlTest, FailWriteToHistoryBackend) {
  history::VisitVector native_visits;
  history::VisitVector sync_visits;
  history::URLRow native_entry(MakeTypedUrlEntry("http://native.com", "entry",
                                                 2, 15, false, &native_visits));
  history::URLRow sync_entry(MakeTypedUrlEntry("http://sync.com", "entry",
                                               3, 16, false, &sync_visits));

  history::URLRows native_entries;
  native_entries.push_back(native_entry);
  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(native_entries), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetURL(_, _)).
      WillOnce(DoAll(SetArgumentPointee<1>(native_entry), Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(native_visits), Return(true)));
  EXPECT_CALL((*history_backend_.get()),
      AddVisits(_, _, history::SOURCE_SYNCED)).WillRepeatedly(Return(false));

  history::URLRows sync_entries;
  sync_entries.push_back(sync_entry);

  EXPECT_CALL((*history_backend_.get()), UpdateURL(_, _)).
      WillRepeatedly(Return(false));
  TypedUrlModelAssociator* associator =
      StartSyncService(base::Bind(&AddTypedUrlEntries, this, sync_entries));
  // Errors writing to the DB should be recorded, but should not cause an
  // unrecoverable error.
  ASSERT_FALSE(
      sync_service_->failed_data_types_handler().GetFailedTypes().Has(
          syncer::TYPED_URLS));
  // Some calls should have succeeded, so the error percentage should be
  // somewhere > 0 and < 100.
  ASSERT_NE(0, associator->GetErrorPercentage());
  ASSERT_NE(100, associator->GetErrorPercentage());
}

TEST_F(ProfileSyncServiceTypedUrlTest, FailToGetTypedURLs) {
  history::VisitVector native_visits;
  history::VisitVector sync_visits;
  history::URLRow native_entry(MakeTypedUrlEntry("http://native.com", "entry",
                                                 2, 15, false, &native_visits));
  history::URLRow sync_entry(MakeTypedUrlEntry("http://sync.com", "entry",
                                               3, 16, false, &sync_visits));

  history::URLRows native_entries;
  native_entries.push_back(native_entry);
  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillOnce(DoAll(SetArgumentPointee<0>(native_entries), Return(false)));

  history::URLRows sync_entries;
  sync_entries.push_back(sync_entry);

  EXPECT_CALL(error_handler_, CreateAndUploadError(_, _, _)).
              WillOnce(Return(syncer::SyncError(
                                  FROM_HERE,
                                  syncer::SyncError::DATATYPE_ERROR,
                                  "Unit test",
                                  syncer::TYPED_URLS)));
  StartSyncService(base::Bind(&AddTypedUrlEntries, this, sync_entries));
  // Errors getting typed URLs will cause an unrecoverable error (since we can
  // do *nothing* in that case).
  ASSERT_TRUE(
      sync_service_->failed_data_types_handler().GetFailedTypes().Has(
          syncer::TYPED_URLS));
  ASSERT_EQ(
      1u, sync_service_->failed_data_types_handler().GetFailedTypes().Size());
  // Can't check GetErrorPercentage(), because generating an unrecoverable
  // error will free the model associator.
}

TEST_F(ProfileSyncServiceTypedUrlTest, IgnoreLocalFileURL) {
  history::VisitVector original_visits;
  // Create http and file url.
  history::URLRow url_entry(MakeTypedUrlEntry("http://yey.com",
                                              "yey", 12, 15, false,
                                              &original_visits));
  history::URLRow file_entry(MakeTypedUrlEntry("file:///kitty.jpg",
                                               "kitteh", 12, 15, false,
                                               &original_visits));

  history::URLRows original_entries;
  original_entries.push_back(url_entry);
  original_entries.push_back(file_entry);

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillRepeatedly(DoAll(SetArgumentPointee<0>(original_entries),
                     Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(original_visits),
                     Return(true)));
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());

  history::VisitVector updated_visits;
  // Create updates for the previous urls + a new file one.
  history::URLRow updated_url_entry(MakeTypedUrlEntry("http://yey.com",
                                                      "yey", 20, 15, false,
                                                      &updated_visits));
  history::URLRow updated_file_entry(MakeTypedUrlEntry("file:///cat.jpg",
                                                       "cat", 20, 15, false,
                                                       &updated_visits));
  history::URLRow new_file_entry(MakeTypedUrlEntry("file:///dog.jpg",
                                                   "dog", 20, 15, false,
                                                   &updated_visits));
  history::URLsModifiedDetails details;
  details.changed_urls.push_back(updated_url_entry);
  details.changed_urls.push_back(updated_file_entry);
  details.changed_urls.push_back(new_file_entry);
  scoped_refptr<ThreadNotifier> notifier(
      new ThreadNotifier(history_thread_.get()));
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URLS_MODIFIED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLsModifiedDetails>(&details));

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);

  // We should ignore the local file urls (existing and updated),
  // and only be left with the updated http url.
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(updated_url_entry, new_sync_entries[0]));
}

TEST_F(ProfileSyncServiceTypedUrlTest, IgnoreLocalhostURL) {
  history::VisitVector original_visits;
  // Create http and localhost url.
  history::URLRow url_entry(MakeTypedUrlEntry("http://yey.com",
                                              "yey", 12, 15, false,
                                              &original_visits));
  history::URLRow localhost_entry(MakeTypedUrlEntry("http://localhost",
                                              "localhost", 12, 15, false,
                                              &original_visits));

  history::URLRows original_entries;
  original_entries.push_back(url_entry);
  original_entries.push_back(localhost_entry);

  EXPECT_CALL((*history_backend_.get()), GetAllTypedURLs(_)).
      WillRepeatedly(DoAll(SetArgumentPointee<0>(original_entries),
                     Return(true)));
  EXPECT_CALL((*history_backend_.get()), GetMostRecentVisitsForURL(_, _, _)).
      WillRepeatedly(DoAll(SetArgumentPointee<2>(original_visits),
                     Return(true)));
  CreateRootHelper create_root(this, syncer::TYPED_URLS);
  StartSyncService(create_root.callback());

  history::VisitVector updated_visits;
  // Update the previous entries and add a new localhost.
  history::URLRow updated_url_entry(MakeTypedUrlEntry("http://yey.com",
                                                  "yey", 20, 15, false,
                                                  &updated_visits));
  history::URLRow updated_localhost_entry(MakeTypedUrlEntry(
                                                  "http://localhost:80",
                                                  "localhost", 20, 15, false,
                                                  &original_visits));
  history::URLRow localhost_ip_entry(MakeTypedUrlEntry("http://127.0.0.1",
                                                  "localhost", 12, 15, false,
                                                  &original_visits));
  history::URLsModifiedDetails details;
  details.changed_urls.push_back(updated_url_entry);
  details.changed_urls.push_back(updated_localhost_entry);
  details.changed_urls.push_back(localhost_ip_entry);
  scoped_refptr<ThreadNotifier> notifier(
      new ThreadNotifier(history_thread_.get()));
  notifier->Notify(chrome::NOTIFICATION_HISTORY_URLS_MODIFIED,
                   content::Source<Profile>(profile_.get()),
                   content::Details<history::URLsModifiedDetails>(&details));

  history::URLRows new_sync_entries;
  GetTypedUrlsFromSyncDB(&new_sync_entries);

  // We should ignore the localhost urls and left only with http url.
  ASSERT_EQ(1U, new_sync_entries.size());
  EXPECT_TRUE(URLsEqual(updated_url_entry, new_sync_entries[0]));
}
