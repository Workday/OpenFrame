// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/test_timeouts.h"
#include "base/time/time.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/invalidation/invalidation_service_factory.h"
#include "chrome/browser/password_manager/mock_password_store.h"
#include "chrome/browser/password_manager/password_store.h"
#include "chrome/browser/password_manager/password_store_factory.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/signin/token_service_factory.h"
#include "chrome/browser/sync/abstract_profile_sync_service_test.h"
#include "chrome/browser/sync/glue/password_change_processor.h"
#include "chrome/browser/sync/glue/password_data_type_controller.h"
#include "chrome/browser/sync/glue/password_model_associator.h"
#include "chrome/browser/sync/profile_sync_components_factory.h"
#include "chrome/browser/sync/profile_sync_components_factory_mock.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/sync/profile_sync_test_util.h"
#include "chrome/browser/sync/test_profile_sync_service.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/profile_mock.h"
#include "content/public/browser/notification_source.h"
#include "content/public/common/password_form.h"
#include "content/public/test/mock_notification_observer.h"
#include "content/public/test/test_browser_thread.h"
#include "google_apis/gaia/gaia_constants.h"
#include "sync/internal_api/public/read_node.h"
#include "sync/internal_api/public/read_transaction.h"
#include "sync/internal_api/public/write_node.h"
#include "sync/internal_api/public/write_transaction.h"
#include "sync/protocol/password_specifics.pb.h"
#include "sync/test/engine/test_id_factory.h"
#include "testing/gmock/include/gmock/gmock.h"

using base::Time;
using browser_sync::PasswordChangeProcessor;
using browser_sync::PasswordDataTypeController;
using browser_sync::PasswordModelAssociator;
using content::BrowserThread;
using content::PasswordForm;
using syncer::syncable::WriteTransaction;
using testing::_;
using testing::AtLeast;
using testing::DoAll;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::SetArgumentPointee;

ACTION_P3(MakePasswordSyncComponents, service, ps, dtc) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  PasswordModelAssociator* model_associator =
      new PasswordModelAssociator(service, ps, NULL);
  PasswordChangeProcessor* change_processor =
      new PasswordChangeProcessor(model_associator, ps, dtc);
  return ProfileSyncComponentsFactory::SyncComponents(model_associator,
                                                      change_processor);
}

ACTION_P(AcquireSyncTransaction, password_test_service) {
  // Check to make sure we can aquire a transaction (will crash if a transaction
  // is already held by this thread, deadlock if held by another thread).
  syncer::WriteTransaction trans(
      FROM_HERE, password_test_service->GetUserShare());
  DVLOG(1) << "Sync transaction acquired.";
}

class NullPasswordStore : public MockPasswordStore {
 public:
  NullPasswordStore() {}

  static scoped_refptr<RefcountedBrowserContextKeyedService> Build(
      content::BrowserContext* profile) {
    return scoped_refptr<RefcountedBrowserContextKeyedService>();
  }

 protected:
  virtual ~NullPasswordStore() {}
};

class PasswordTestProfileSyncService : public TestProfileSyncService {
 public:
  PasswordTestProfileSyncService(
      ProfileSyncComponentsFactory* factory,
      Profile* profile,
      SigninManagerBase* signin)
      : TestProfileSyncService(factory,
                               profile,
                               signin,
                               ProfileSyncService::AUTO_START,
                               false) {}

  virtual ~PasswordTestProfileSyncService() {}

  virtual void OnPassphraseAccepted() OVERRIDE {
    if (!callback_.is_null())
      callback_.Run();

    TestProfileSyncService::OnPassphraseAccepted();
  }

  static BrowserContextKeyedService* Build(content::BrowserContext* context) {
    Profile* profile = static_cast<Profile*>(context);
    SigninManagerBase* signin =
        SigninManagerFactory::GetForProfile(profile);
    ProfileSyncComponentsFactoryMock* factory =
        new ProfileSyncComponentsFactoryMock();
    return new PasswordTestProfileSyncService(factory, profile, signin);
  }

  void set_passphrase_accept_callback(const base::Closure& callback) {
    callback_ = callback;
  }

 private:
  base::Closure callback_;
};

class ProfileSyncServicePasswordTest : public AbstractProfileSyncServiceTest {
 public:
  syncer::UserShare* GetUserShare() {
    return sync_service_->GetUserShare();
  }

  void AddPasswordSyncNode(const PasswordForm& entry) {
    syncer::WriteTransaction trans(FROM_HERE, sync_service_->GetUserShare());
    syncer::ReadNode password_root(&trans);
    ASSERT_EQ(syncer::BaseNode::INIT_OK,
              password_root.InitByTagLookup(browser_sync::kPasswordTag));

    syncer::WriteNode node(&trans);
    std::string tag = PasswordModelAssociator::MakeTag(entry);
    syncer::WriteNode::InitUniqueByCreationResult result =
        node.InitUniqueByCreation(syncer::PASSWORDS, password_root, tag);
    ASSERT_EQ(syncer::WriteNode::INIT_SUCCESS, result);
    PasswordModelAssociator::WriteToSyncNode(entry, &node);
  }

 protected:
  ProfileSyncServicePasswordTest() {}

  virtual void SetUp() {
    AbstractProfileSyncServiceTest::SetUp();
    profile_.reset(new ProfileMock());
    invalidation::InvalidationServiceFactory::GetInstance()->
        SetBuildOnlyFakeInvalidatorsForTest(true);
    password_store_ = static_cast<MockPasswordStore*>(
        PasswordStoreFactory::GetInstance()->SetTestingFactoryAndUse(
            profile_.get(), MockPasswordStore::Build).get());
  }

  virtual void TearDown() {
    if (password_store_.get())
      password_store_->ShutdownOnUIThread();
      ProfileSyncServiceFactory::GetInstance()->SetTestingFactory(
          profile_.get(), NULL);
      profile_.reset();
      AbstractProfileSyncServiceTest::TearDown();
  }

  static void SignalEvent(base::WaitableEvent* done) {
    done->Signal();
  }

  void FlushLastDBTask() {
    base::WaitableEvent done(false, false);
    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(&ProfileSyncServicePasswordTest::SignalEvent, &done));
    done.TimedWait(TestTimeouts::action_timeout());
  }

  void StartSyncService(const base::Closure& root_callback,
                        const base::Closure& node_callback) {
    if (!sync_service_) {
      SigninManagerBase* signin =
          SigninManagerFactory::GetForProfile(profile_.get());
      signin->SetAuthenticatedUsername("test_user@gmail.com");
      token_service_ = static_cast<TokenService*>(
          TokenServiceFactory::GetInstance()->SetTestingFactoryAndUse(
              profile_.get(), BuildTokenService));
      ProfileOAuth2TokenServiceFactory::GetInstance()->SetTestingFactory(
          profile_.get(), FakeOAuth2TokenService::BuildTokenService);

      PasswordTestProfileSyncService* sync =
          static_cast<PasswordTestProfileSyncService*>(
              ProfileSyncServiceFactory::GetInstance()->
                  SetTestingFactoryAndUse(profile_.get(),
                      &PasswordTestProfileSyncService::Build));
      sync->set_backend_init_callback(root_callback);
      sync->set_passphrase_accept_callback(node_callback);
      sync_service_ = sync;

      syncer::ModelTypeSet preferred_types =
          sync_service_->GetPreferredDataTypes();
      preferred_types.Put(syncer::PASSWORDS);
      sync_service_->ChangePreferredDataTypes(preferred_types);
      PasswordDataTypeController* data_type_controller =
          new PasswordDataTypeController(sync_service_->factory(),
                                         profile_.get(),
                                         sync_service_);
      ProfileSyncComponentsFactoryMock* components =
          sync_service_->components_factory_mock();
      if (password_store_.get()) {
        EXPECT_CALL(*components, CreatePasswordSyncComponents(_, _, _))
            .Times(AtLeast(1)).  // Can be more if we hit NEEDS_CRYPTO.
            WillRepeatedly(MakePasswordSyncComponents(
                sync_service_, password_store_.get(), data_type_controller));
      } else {
        // When the password store is unavailable, password sync components must
        // not be created.
        EXPECT_CALL(*components, CreatePasswordSyncComponents(_, _, _))
            .Times(0);
      }
      EXPECT_CALL(*components, CreateDataTypeManager(_, _, _, _, _, _)).
          WillOnce(ReturnNewDataTypeManager());

      // We need tokens to get the tests going
      token_service_->IssueAuthTokenForTest(
          GaiaConstants::kGaiaOAuth2LoginRefreshToken, "oauth2_login_token");
      token_service_->IssueAuthTokenForTest(
          GaiaConstants::kSyncService, "token");

      sync_service_->RegisterDataTypeController(data_type_controller);
      sync_service_->Initialize();
      base::MessageLoop::current()->Run();
      FlushLastDBTask();

      sync_service_->SetEncryptionPassphrase("foo",
                                             ProfileSyncService::IMPLICIT);
      base::MessageLoop::current()->Run();
    }
  }

  // Helper to sort the results of GetPasswordEntriesFromSyncDB.  The sorting
  // doesn't need to be particularly intelligent, it just needs to be consistent
  // enough that we can base our tests expectations on the ordering it provides.
  static bool PasswordFormComparator(const PasswordForm& pf1,
                                     const PasswordForm& pf2) {
    if (pf1.submit_element < pf2.submit_element)
      return true;
    if (pf1.username_element < pf2.username_element)
      return true;
    if (pf1.username_value < pf2.username_value)
      return true;
    if (pf1.username_value < pf2.username_value)
      return true;
    if (pf1.password_element < pf2.password_element)
      return true;
    if (pf1.password_value < pf2.password_value)
      return true;

    return false;
  }

  void GetPasswordEntriesFromSyncDB(std::vector<PasswordForm>* entries) {
    syncer::ReadTransaction trans(FROM_HERE, sync_service_->GetUserShare());
    syncer::ReadNode password_root(&trans);
    ASSERT_EQ(syncer::BaseNode::INIT_OK,
              password_root.InitByTagLookup(browser_sync::kPasswordTag));

    int64 child_id = password_root.GetFirstChildId();
    while (child_id != syncer::kInvalidId) {
      syncer::ReadNode child_node(&trans);
      ASSERT_EQ(syncer::BaseNode::INIT_OK,
                child_node.InitByIdLookup(child_id));

      const sync_pb::PasswordSpecificsData& password =
          child_node.GetPasswordSpecifics();

      PasswordForm form;
      PasswordModelAssociator::CopyPassword(password, &form);

      entries->push_back(form);

      child_id = child_node.GetSuccessorId();
    }

    std::sort(entries->begin(), entries->end(), PasswordFormComparator);
  }

  bool ComparePasswords(const PasswordForm& lhs, const PasswordForm& rhs) {
    return lhs.scheme == rhs.scheme &&
           lhs.signon_realm == rhs.signon_realm &&
           lhs.origin == rhs.origin &&
           lhs.action == rhs.action &&
           lhs.username_element == rhs.username_element &&
           lhs.username_value == rhs.username_value &&
           lhs.password_element == rhs.password_element &&
           lhs.password_value == rhs.password_value &&
           lhs.ssl_valid == rhs.ssl_valid &&
           lhs.preferred == rhs.preferred &&
           lhs.date_created == rhs.date_created &&
           lhs.blacklisted_by_user == rhs.blacklisted_by_user;
  }

  void SetIdleChangeProcessorExpectations() {
    EXPECT_CALL(*password_store_.get(), AddLoginImpl(_)).Times(0);
    EXPECT_CALL(*password_store_.get(), UpdateLoginImpl(_)).Times(0);
    EXPECT_CALL(*password_store_.get(), RemoveLoginImpl(_)).Times(0);
  }

  content::MockNotificationObserver observer_;
  scoped_ptr<ProfileMock> profile_;
  scoped_refptr<MockPasswordStore> password_store_;
  content::NotificationRegistrar registrar_;
};

void AddPasswordEntriesCallback(ProfileSyncServicePasswordTest* test,
                                const std::vector<PasswordForm>& entries) {
  for (size_t i = 0; i < entries.size(); ++i)
    test->AddPasswordSyncNode(entries[i]);
}

// Flaky on mac_rel. See http://crbug.com/228943
#if defined(OS_MACOSX)
#define MAYBE_EmptyNativeEmptySync DISABLED_EmptyNativeEmptySync
#define MAYBE_EnsureNoTransactions DISABLED_EnsureNoTransactions
#define MAYBE_FailModelAssociation DISABLED_FailModelAssociation
#define MAYBE_FailPasswordStoreLoad DISABLED_FailPasswordStoreLoad
#define MAYBE_HasNativeEntriesEmptySync DISABLED_HasNativeEntriesEmptySync
#define MAYBE_HasNativeEntriesEmptySyncSameUsername \
    DISABLED_HasNativeEntriesEmptySyncSameUsername
#define MAYBE_HasNativeHasSyncMergeEntry DISABLED_HasNativeHasSyncMergeEntry
#define MAYBE_HasNativeHasSyncNoMerge DISABLED_HasNativeHasSyncNoMerge
#else
#define MAYBE_EmptyNativeEmptySync EmptyNativeEmptySync
#define MAYBE_EnsureNoTransactions EnsureNoTransactions
#define MAYBE_FailModelAssociation FailModelAssociation
#define MAYBE_FailPasswordStoreLoad FailPasswordStoreLoad
#define MAYBE_HasNativeEntriesEmptySync HasNativeEntriesEmptySync
#define MAYBE_HasNativeEntriesEmptySyncSameUsername \
    HasNativeEntriesEmptySyncSameUsername
#define MAYBE_HasNativeHasSyncMergeEntry HasNativeHasSyncMergeEntry
#define MAYBE_HasNativeHasSyncNoMerge HasNativeHasSyncNoMerge
#endif

TEST_F(ProfileSyncServicePasswordTest, MAYBE_FailModelAssociation) {
  StartSyncService(base::Closure(), base::Closure());
  EXPECT_TRUE(sync_service_->HasUnrecoverableError());
}

TEST_F(ProfileSyncServicePasswordTest, MAYBE_FailPasswordStoreLoad) {
  password_store_ = static_cast<NullPasswordStore*>(
      PasswordStoreFactory::GetInstance()->SetTestingFactoryAndUse(
          profile_.get(), NullPasswordStore::Build).get());
  StartSyncService(base::Closure(), base::Closure());
  EXPECT_FALSE(sync_service_->HasUnrecoverableError());
  syncer::ModelTypeSet failed_types =
      sync_service_->failed_data_types_handler().GetFailedTypes();
  EXPECT_TRUE(failed_types.Equals(syncer::ModelTypeSet(syncer::PASSWORDS)));
}

TEST_F(ProfileSyncServicePasswordTest, MAYBE_EmptyNativeEmptySync) {
  EXPECT_CALL(*password_store_.get(), FillAutofillableLogins(_))
      .WillOnce(Return(true));
  EXPECT_CALL(*password_store_.get(), FillBlacklistLogins(_))
      .WillOnce(Return(true));
  SetIdleChangeProcessorExpectations();
  CreateRootHelper create_root(this, syncer::PASSWORDS);
  StartSyncService(create_root.callback(), base::Closure());
  std::vector<PasswordForm> sync_entries;
  GetPasswordEntriesFromSyncDB(&sync_entries);
  EXPECT_EQ(0U, sync_entries.size());
}

TEST_F(ProfileSyncServicePasswordTest, MAYBE_HasNativeEntriesEmptySync) {
  std::vector<PasswordForm*> forms;
  std::vector<PasswordForm> expected_forms;
  PasswordForm* new_form = new PasswordForm;
  new_form->scheme = PasswordForm::SCHEME_HTML;
  new_form->signon_realm = "pie";
  new_form->origin = GURL("http://pie.com");
  new_form->action = GURL("http://pie.com/submit");
  new_form->username_element = UTF8ToUTF16("name");
  new_form->username_value = UTF8ToUTF16("tom");
  new_form->password_element = UTF8ToUTF16("cork");
  new_form->password_value = UTF8ToUTF16("password1");
  new_form->ssl_valid = true;
  new_form->preferred = false;
  new_form->date_created = base::Time::FromInternalValue(1234);
  new_form->blacklisted_by_user = false;
  forms.push_back(new_form);
  expected_forms.push_back(*new_form);
  EXPECT_CALL(*password_store_.get(), FillAutofillableLogins(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(forms), Return(true)));
  EXPECT_CALL(*password_store_.get(), FillBlacklistLogins(_))
      .WillOnce(Return(true));
  SetIdleChangeProcessorExpectations();
  CreateRootHelper create_root(this, syncer::PASSWORDS);
  StartSyncService(create_root.callback(), base::Closure());
  std::vector<PasswordForm> sync_forms;
  GetPasswordEntriesFromSyncDB(&sync_forms);
  ASSERT_EQ(1U, sync_forms.size());
  EXPECT_TRUE(ComparePasswords(expected_forms[0], sync_forms[0]));
}

TEST_F(ProfileSyncServicePasswordTest,
       MAYBE_HasNativeEntriesEmptySyncSameUsername) {
  std::vector<PasswordForm*> forms;
  std::vector<PasswordForm> expected_forms;

  {
    PasswordForm* new_form = new PasswordForm;
    new_form->scheme = PasswordForm::SCHEME_HTML;
    new_form->signon_realm = "pie";
    new_form->origin = GURL("http://pie.com");
    new_form->action = GURL("http://pie.com/submit");
    new_form->username_element = UTF8ToUTF16("name");
    new_form->username_value = UTF8ToUTF16("tom");
    new_form->password_element = UTF8ToUTF16("cork");
    new_form->password_value = UTF8ToUTF16("password1");
    new_form->ssl_valid = true;
    new_form->preferred = false;
    new_form->date_created = base::Time::FromInternalValue(1234);
    new_form->blacklisted_by_user = false;
    forms.push_back(new_form);
    expected_forms.push_back(*new_form);
  }
  {
    PasswordForm* new_form = new PasswordForm;
    new_form->scheme = PasswordForm::SCHEME_HTML;
    new_form->signon_realm = "pie";
    new_form->origin = GURL("http://pie.com");
    new_form->action = GURL("http://pie.com/submit");
    new_form->username_element = UTF8ToUTF16("name");
    new_form->username_value = UTF8ToUTF16("pete");
    new_form->password_element = UTF8ToUTF16("cork");
    new_form->password_value = UTF8ToUTF16("password2");
    new_form->ssl_valid = true;
    new_form->preferred = false;
    new_form->date_created = base::Time::FromInternalValue(1234);
    new_form->blacklisted_by_user = false;
    forms.push_back(new_form);
    expected_forms.push_back(*new_form);
  }

  EXPECT_CALL(*password_store_.get(), FillAutofillableLogins(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(forms), Return(true)));
  EXPECT_CALL(*password_store_.get(), FillBlacklistLogins(_))
      .WillOnce(Return(true));
  SetIdleChangeProcessorExpectations();
  CreateRootHelper create_root(this, syncer::PASSWORDS);
  StartSyncService(create_root.callback(), base::Closure());
  std::vector<PasswordForm> sync_forms;
  GetPasswordEntriesFromSyncDB(&sync_forms);
  ASSERT_EQ(2U, sync_forms.size());
  EXPECT_TRUE(ComparePasswords(expected_forms[0], sync_forms[1]));
  EXPECT_TRUE(ComparePasswords(expected_forms[1], sync_forms[0]));
}

TEST_F(ProfileSyncServicePasswordTest, MAYBE_HasNativeHasSyncNoMerge) {
  std::vector<PasswordForm*> native_forms;
  std::vector<PasswordForm> sync_forms;
  std::vector<PasswordForm> expected_forms;
  {
    PasswordForm* new_form = new PasswordForm;
    new_form->scheme = PasswordForm::SCHEME_HTML;
    new_form->signon_realm = "pie";
    new_form->origin = GURL("http://pie.com");
    new_form->action = GURL("http://pie.com/submit");
    new_form->username_element = UTF8ToUTF16("name");
    new_form->username_value = UTF8ToUTF16("tom");
    new_form->password_element = UTF8ToUTF16("cork");
    new_form->password_value = UTF8ToUTF16("password1");
    new_form->ssl_valid = true;
    new_form->preferred = false;
    new_form->date_created = base::Time::FromInternalValue(1234);
    new_form->blacklisted_by_user = false;

    native_forms.push_back(new_form);
    expected_forms.push_back(*new_form);
  }

  {
    PasswordForm new_form;
    new_form.scheme = PasswordForm::SCHEME_HTML;
    new_form.signon_realm = "pie2";
    new_form.origin = GURL("http://pie2.com");
    new_form.action = GURL("http://pie2.com/submit");
    new_form.username_element = UTF8ToUTF16("name2");
    new_form.username_value = UTF8ToUTF16("tom2");
    new_form.password_element = UTF8ToUTF16("cork2");
    new_form.password_value = UTF8ToUTF16("password12");
    new_form.ssl_valid = false;
    new_form.preferred = true;
    new_form.date_created = base::Time::FromInternalValue(12345);
    new_form.blacklisted_by_user = false;
    sync_forms.push_back(new_form);
    expected_forms.push_back(new_form);
  }

  EXPECT_CALL(*password_store_.get(), FillAutofillableLogins(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(native_forms), Return(true)));
  EXPECT_CALL(*password_store_.get(), FillBlacklistLogins(_))
      .WillOnce(Return(true));
  EXPECT_CALL(*password_store_.get(), AddLoginImpl(_)).Times(1);

  CreateRootHelper create_root(this, syncer::PASSWORDS);
  StartSyncService(create_root.callback(),
                   base::Bind(&AddPasswordEntriesCallback, this, sync_forms));

  std::vector<PasswordForm> new_sync_forms;
  GetPasswordEntriesFromSyncDB(&new_sync_forms);

  EXPECT_EQ(2U, new_sync_forms.size());
  EXPECT_TRUE(ComparePasswords(expected_forms[0], new_sync_forms[0]));
  EXPECT_TRUE(ComparePasswords(expected_forms[1], new_sync_forms[1]));
}

// Same as HasNativeHasEmptyNoMerge, but we attempt to aquire a sync transaction
// every time the password store is accessed.
TEST_F(ProfileSyncServicePasswordTest, MAYBE_EnsureNoTransactions) {
  std::vector<PasswordForm*> native_forms;
  std::vector<PasswordForm> sync_forms;
  std::vector<PasswordForm> expected_forms;
  {
    PasswordForm* new_form = new PasswordForm;
    new_form->scheme = PasswordForm::SCHEME_HTML;
    new_form->signon_realm = "pie";
    new_form->origin = GURL("http://pie.com");
    new_form->action = GURL("http://pie.com/submit");
    new_form->username_element = UTF8ToUTF16("name");
    new_form->username_value = UTF8ToUTF16("tom");
    new_form->password_element = UTF8ToUTF16("cork");
    new_form->password_value = UTF8ToUTF16("password1");
    new_form->ssl_valid = true;
    new_form->preferred = false;
    new_form->date_created = base::Time::FromInternalValue(1234);
    new_form->blacklisted_by_user = false;

    native_forms.push_back(new_form);
    expected_forms.push_back(*new_form);
  }

  {
    PasswordForm new_form;
    new_form.scheme = PasswordForm::SCHEME_HTML;
    new_form.signon_realm = "pie2";
    new_form.origin = GURL("http://pie2.com");
    new_form.action = GURL("http://pie2.com/submit");
    new_form.username_element = UTF8ToUTF16("name2");
    new_form.username_value = UTF8ToUTF16("tom2");
    new_form.password_element = UTF8ToUTF16("cork2");
    new_form.password_value = UTF8ToUTF16("password12");
    new_form.ssl_valid = false;
    new_form.preferred = true;
    new_form.date_created = base::Time::FromInternalValue(12345);
    new_form.blacklisted_by_user = false;
    sync_forms.push_back(new_form);
    expected_forms.push_back(new_form);
  }

  EXPECT_CALL(*password_store_.get(), FillAutofillableLogins(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(native_forms),
                      AcquireSyncTransaction(this),
                      Return(true)));
  EXPECT_CALL(*password_store_.get(), FillBlacklistLogins(_))
      .WillOnce(DoAll(AcquireSyncTransaction(this), Return(true)));
  EXPECT_CALL(*password_store_.get(), AddLoginImpl(_))
      .WillOnce(AcquireSyncTransaction(this));

  CreateRootHelper create_root(this, syncer::PASSWORDS);
  StartSyncService(create_root.callback(),
                   base::Bind(&AddPasswordEntriesCallback, this, sync_forms));

  std::vector<PasswordForm> new_sync_forms;
  GetPasswordEntriesFromSyncDB(&new_sync_forms);

  EXPECT_EQ(2U, new_sync_forms.size());
  EXPECT_TRUE(ComparePasswords(expected_forms[0], new_sync_forms[0]));
  EXPECT_TRUE(ComparePasswords(expected_forms[1], new_sync_forms[1]));
}

TEST_F(ProfileSyncServicePasswordTest, MAYBE_HasNativeHasSyncMergeEntry) {
  std::vector<PasswordForm*> native_forms;
  std::vector<PasswordForm> sync_forms;
  std::vector<PasswordForm> expected_forms;
  {
    PasswordForm* new_form = new PasswordForm;
    new_form->scheme = PasswordForm::SCHEME_HTML;
    new_form->signon_realm = "pie";
    new_form->origin = GURL("http://pie.com");
    new_form->action = GURL("http://pie.com/submit");
    new_form->username_element = UTF8ToUTF16("name");
    new_form->username_value = UTF8ToUTF16("tom");
    new_form->password_element = UTF8ToUTF16("cork");
    new_form->password_value = UTF8ToUTF16("password1");
    new_form->ssl_valid = true;
    new_form->preferred = false;
    new_form->date_created = base::Time::FromInternalValue(1234);
    new_form->blacklisted_by_user = false;

    native_forms.push_back(new_form);
  }

  {
    PasswordForm new_form;
    new_form.scheme = PasswordForm::SCHEME_HTML;
    new_form.signon_realm = "pie";
    new_form.origin = GURL("http://pie.com");
    new_form.action = GURL("http://pie.com/submit");
    new_form.username_element = UTF8ToUTF16("name");
    new_form.username_value = UTF8ToUTF16("tom");
    new_form.password_element = UTF8ToUTF16("cork");
    new_form.password_value = UTF8ToUTF16("password12");
    new_form.ssl_valid = false;
    new_form.preferred = true;
    new_form.date_created = base::Time::FromInternalValue(12345);
    new_form.blacklisted_by_user = false;
    sync_forms.push_back(new_form);
  }

  {
    PasswordForm new_form;
    new_form.scheme = PasswordForm::SCHEME_HTML;
    new_form.signon_realm = "pie";
    new_form.origin = GURL("http://pie.com");
    new_form.action = GURL("http://pie.com/submit");
    new_form.username_element = UTF8ToUTF16("name");
    new_form.username_value = UTF8ToUTF16("tom");
    new_form.password_element = UTF8ToUTF16("cork");
    new_form.password_value = UTF8ToUTF16("password12");
    new_form.ssl_valid = false;
    new_form.preferred = true;
    new_form.date_created = base::Time::FromInternalValue(12345);
    new_form.blacklisted_by_user = false;
    expected_forms.push_back(new_form);
  }

  EXPECT_CALL(*password_store_.get(), FillAutofillableLogins(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(native_forms), Return(true)));
  EXPECT_CALL(*password_store_.get(), FillBlacklistLogins(_))
      .WillOnce(Return(true));
  EXPECT_CALL(*password_store_.get(), UpdateLoginImpl(_)).Times(1);

  CreateRootHelper create_root(this, syncer::PASSWORDS);
  StartSyncService(create_root.callback(),
                   base::Bind(&AddPasswordEntriesCallback, this, sync_forms));

  std::vector<PasswordForm> new_sync_forms;
  GetPasswordEntriesFromSyncDB(&new_sync_forms);

  EXPECT_EQ(1U, new_sync_forms.size());
  EXPECT_TRUE(ComparePasswords(expected_forms[0], new_sync_forms[0]));
}
