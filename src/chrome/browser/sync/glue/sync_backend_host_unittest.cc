// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/sync_backend_host.h"

#include <cstddef>

#include "base/location.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/test_timeouts.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/invalidation/invalidator_storage.h"
#include "chrome/browser/prefs/pref_service_syncable.h"
#include "chrome/browser/sync/glue/device_info.h"
#include "chrome/browser/sync/glue/synced_device_tracker.h"
#include "chrome/browser/sync/sync_prefs.h"
#include "chrome/test/base/testing_profile.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/test_utils.h"
#include "google/cacheinvalidation/include/types.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/engine/model_safe_worker.h"
#include "sync/internal_api/public/sync_manager_factory.h"
#include "sync/internal_api/public/test/fake_sync_manager.h"
#include "sync/internal_api/public/util/experiments.h"
#include "sync/notifier/invalidator_state.h"
#include "sync/notifier/object_id_invalidation_map_test_util.h"
#include "sync/protocol/encryption.pb.h"
#include "sync/protocol/sync_protocol_error.h"
#include "sync/util/test_unrecoverable_error_handler.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using content::BrowserThread;
using syncer::FakeSyncManager;
using syncer::SyncManager;
using ::testing::InvokeWithoutArgs;
using ::testing::StrictMock;
using ::testing::_;

namespace browser_sync {

namespace {

ACTION_P(Signal, event) {
  event->Signal();
}

void QuitMessageLoop() {
  base::MessageLoop::current()->Quit();
}

class MockSyncFrontend : public SyncFrontend {
 public:
  virtual ~MockSyncFrontend() {}

  MOCK_METHOD1(OnInvalidatorStateChange,
               void(syncer::InvalidatorState));
  MOCK_METHOD1(OnIncomingInvalidation,
               void(const syncer::ObjectIdInvalidationMap&));
  MOCK_METHOD3(
      OnBackendInitialized,
      void(const syncer::WeakHandle<syncer::JsBackend>&,
           const syncer::WeakHandle<syncer::DataTypeDebugInfoListener>&,
           bool));
  MOCK_METHOD0(OnSyncCycleCompleted, void());
  MOCK_METHOD1(OnConnectionStatusChange,
               void(syncer::ConnectionStatus status));
  MOCK_METHOD0(OnStopSyncingPermanently, void());
  MOCK_METHOD0(OnClearServerDataSucceeded, void());
  MOCK_METHOD0(OnClearServerDataFailed, void());
  MOCK_METHOD2(OnPassphraseRequired,
               void(syncer::PassphraseRequiredReason,
                    const sync_pb::EncryptedData&));
  MOCK_METHOD0(OnPassphraseAccepted, void());
  MOCK_METHOD2(OnEncryptedTypesChanged,
               void(syncer::ModelTypeSet, bool));
  MOCK_METHOD0(OnEncryptionComplete, void());
  MOCK_METHOD1(OnMigrationNeededForTypes, void(syncer::ModelTypeSet));
  MOCK_METHOD1(OnExperimentsChanged,
      void(const syncer::Experiments&));
  MOCK_METHOD1(OnActionableError,
      void(const syncer::SyncProtocolError& sync_error));
  MOCK_METHOD0(OnSyncConfigureRetry, void());
};

class FakeSyncManagerFactory : public syncer::SyncManagerFactory {
 public:
  explicit FakeSyncManagerFactory(FakeSyncManager** fake_manager)
     : fake_manager_(fake_manager) {
    *fake_manager_ = NULL;
  }
  virtual ~FakeSyncManagerFactory() {}

  // SyncManagerFactory implementation.  Called on the sync thread.
  virtual scoped_ptr<SyncManager> CreateSyncManager(
      std::string name) OVERRIDE {
    *fake_manager_ = new FakeSyncManager(initial_sync_ended_types_,
                                         progress_marker_types_,
                                         configure_fail_types_);
    return scoped_ptr<SyncManager>(*fake_manager_);
  }

  void set_initial_sync_ended_types(syncer::ModelTypeSet types) {
    initial_sync_ended_types_ = types;
  }

  void set_progress_marker_types(syncer::ModelTypeSet types) {
    progress_marker_types_ = types;
  }

  void set_configure_fail_types(syncer::ModelTypeSet types) {
    configure_fail_types_ = types;
  }

 private:
  syncer::ModelTypeSet initial_sync_ended_types_;
  syncer::ModelTypeSet progress_marker_types_;
  syncer::ModelTypeSet configure_fail_types_;
  FakeSyncManager** fake_manager_;
};

class SyncBackendHostTest : public testing::Test {
 protected:
  SyncBackendHostTest()
      : thread_bundle_(content::TestBrowserThreadBundle::REAL_IO_THREAD),
        fake_manager_(NULL) {}

  virtual ~SyncBackendHostTest() {}

  virtual void SetUp() OVERRIDE {
    profile_.reset(new TestingProfile());
    sync_prefs_.reset(new SyncPrefs(profile_->GetPrefs()));
    backend_.reset(new SyncBackendHost(
        profile_->GetDebugName(),
        profile_.get(),
        sync_prefs_->AsWeakPtr()));
    credentials_.email = "user@example.com";
    credentials_.sync_token = "sync_token";

    fake_manager_factory_.reset(new FakeSyncManagerFactory(&fake_manager_));

    // These types are always implicitly enabled.
    enabled_types_.PutAll(syncer::ControlTypes());

    // NOTE: We can't include Passwords or Typed URLs due to the Sync Backend
    // Registrar removing them if it can't find their model workers.
    enabled_types_.Put(syncer::BOOKMARKS);
    enabled_types_.Put(syncer::NIGORI);
    enabled_types_.Put(syncer::DEVICE_INFO);
    enabled_types_.Put(syncer::PREFERENCES);
    enabled_types_.Put(syncer::SESSIONS);
    enabled_types_.Put(syncer::SEARCH_ENGINES);
    enabled_types_.Put(syncer::AUTOFILL);
    enabled_types_.Put(syncer::EXPERIMENTS);
  }

  virtual void TearDown() OVERRIDE {
    if (backend_) {
      backend_->StopSyncingForShutdown();
      backend_->Shutdown(SyncBackendHost::STOP);
    }
    backend_.reset();
    sync_prefs_.reset();
    profile_.reset();
    // Pump messages posted by the sync thread (which may end up
    // posting on the IO thread).
    base::RunLoop().RunUntilIdle();
    content::RunAllPendingInMessageLoop(BrowserThread::IO);
    // Pump any messages posted by the IO thread.
    base::RunLoop().RunUntilIdle();
  }

  // Synchronously initializes the backend.
  void InitializeBackend(bool expect_success) {
    EXPECT_CALL(mock_frontend_, OnBackendInitialized(_, _, expect_success)).
        WillOnce(InvokeWithoutArgs(QuitMessageLoop));
    backend_->Initialize(
        &mock_frontend_,
        scoped_ptr<base::Thread>(),
        syncer::WeakHandle<syncer::JsEventHandler>(),
        GURL(std::string()),
        credentials_,
        true,
        fake_manager_factory_.PassAs<syncer::SyncManagerFactory>(),
        scoped_ptr<syncer::UnrecoverableErrorHandler>(
            new syncer::TestUnrecoverableErrorHandler).Pass(),
        NULL);
    base::RunLoop run_loop;
    BrowserThread::PostDelayedTask(BrowserThread::UI, FROM_HERE,
                                   run_loop.QuitClosure(),
                                   TestTimeouts::action_timeout());
    run_loop.Run();
    // |fake_manager_factory_|'s fake_manager() is set on the sync
    // thread, but we can rely on the message loop barriers to
    // guarantee that we see the updated value.
    DCHECK(fake_manager_);
  }

  // Synchronously configures the backend's datatypes.
  void ConfigureDataTypes(syncer::ModelTypeSet types_to_add,
                          syncer::ModelTypeSet types_to_remove) {
    BackendDataTypeConfigurer::DataTypeConfigStateMap config_state_map;
    BackendDataTypeConfigurer::SetDataTypesState(
        BackendDataTypeConfigurer::CONFIGURE_ACTIVE,
        types_to_add,
        &config_state_map);
    BackendDataTypeConfigurer::SetDataTypesState(
        BackendDataTypeConfigurer::DISABLED,
        types_to_remove, &config_state_map);

    types_to_add.PutAll(syncer::ControlTypes());
    backend_->ConfigureDataTypes(
        syncer::CONFIGURE_REASON_RECONFIGURATION,
        config_state_map,
        base::Bind(&SyncBackendHostTest::DownloadReady,
                   base::Unretained(this)),
        base::Bind(&SyncBackendHostTest::OnDownloadRetry,
                   base::Unretained(this)));
    base::RunLoop run_loop;
    BrowserThread::PostDelayedTask(BrowserThread::UI, FROM_HERE,
                                   run_loop.QuitClosure(),
                                   TestTimeouts::action_timeout());
    run_loop.Run();
  }

  void IssueRefreshRequest(syncer::ModelTypeSet types) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_SYNC_REFRESH_LOCAL,
        content::Source<Profile>(profile_.get()),
        content::Details<syncer::ModelTypeSet>(&types));
  }

 protected:
  void DownloadReady(syncer::ModelTypeSet succeeded_types,
                     syncer::ModelTypeSet failed_types) {
    base::MessageLoop::current()->Quit();
  }

  void OnDownloadRetry() {
    NOTIMPLEMENTED();
  }

  content::TestBrowserThreadBundle thread_bundle_;
  StrictMock<MockSyncFrontend> mock_frontend_;
  syncer::SyncCredentials credentials_;
  scoped_ptr<TestingProfile> profile_;
  scoped_ptr<SyncPrefs> sync_prefs_;
  scoped_ptr<SyncBackendHost> backend_;
  scoped_ptr<FakeSyncManagerFactory> fake_manager_factory_;
  FakeSyncManager* fake_manager_;
  syncer::ModelTypeSet enabled_types_;
};

// Test basic initialization with no initial types (first time initialization).
// Only the nigori should be configured.
TEST_F(SyncBackendHostTest, InitShutdown) {
  InitializeBackend(true);
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      syncer::ControlTypes()));
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(
      syncer::ControlTypes()));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      syncer::ControlTypes()).Empty());
}

// Test first time sync scenario. All types should be properly configured.
TEST_F(SyncBackendHostTest, FirstTimeSync) {
  InitializeBackend(true);
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      syncer::ControlTypes()));
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(
      syncer::ControlTypes()));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      syncer::ControlTypes()).Empty());

  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().HasAll(
      Difference(enabled_types_, syncer::ControlTypes())));
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetEnabledTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());
}

// Test the restart after setting up sync scenario. No enabled types should be
// downloaded or cleaned.
TEST_F(SyncBackendHostTest, Restart) {
  sync_prefs_->SetSyncSetupCompleted();
  syncer::ModelTypeSet all_but_nigori = enabled_types_;
  fake_manager_factory_->set_progress_marker_types(enabled_types_);
  fake_manager_factory_->set_initial_sync_ended_types(enabled_types_);
  InitializeBackend(true);
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Empty());
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           enabled_types_).Empty());
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());

  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Empty());
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           enabled_types_).Empty());
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetEnabledTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());
}

// Test a sync restart scenario where some types had never finished configuring.
// The partial types should be purged, then reconfigured properly.
TEST_F(SyncBackendHostTest, PartialTypes) {
  sync_prefs_->SetSyncSetupCompleted();
  // Set sync manager behavior before passing it down. All types have progress
  // markers, but nigori and bookmarks are missing initial sync ended.
  syncer::ModelTypeSet partial_types(syncer::NIGORI, syncer::BOOKMARKS);
  syncer::ModelTypeSet full_types =
      Difference(enabled_types_, partial_types);
  fake_manager_factory_->set_progress_marker_types(enabled_types_);
  fake_manager_factory_->set_initial_sync_ended_types(full_types);

  // Bringing up the backend should purge all partial types, then proceed to
  // download the Nigori.
  InitializeBackend(true);
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      syncer::ModelTypeSet(syncer::NIGORI)));
  EXPECT_TRUE(fake_manager_->GetAndResetCleanedTypes().HasAll(partial_types));
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(
      Union(full_types, syncer::ModelTypeSet(syncer::NIGORI))));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Equals(
          Difference(partial_types, syncer::ModelTypeSet(syncer::NIGORI))));

  // Now do the actual configuration, which should download and apply bookmarks.
  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           enabled_types_).Empty());
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      partial_types));
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetEnabledTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());
}

// Test the behavior when we lose the sync db. Although we already have types
// enabled, we should re-download all of them because we lost their data.
TEST_F(SyncBackendHostTest, LostDB) {
  sync_prefs_->SetSyncSetupCompleted();
  // Initialization should fetch the Nigori node.  Everything else should be
  // left untouched.
  InitializeBackend(true);
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      syncer::ModelTypeSet(syncer::ControlTypes())));
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(
      syncer::ModelTypeSet(syncer::ControlTypes())));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Equals(
          Difference(enabled_types_, syncer::ControlTypes())));

  // The database was empty, so any cleaning is entirely optional.  We want to
  // reset this value before running the next part of the test, though.
  fake_manager_->GetAndResetCleanedTypes();

  // The actual configuration should redownload and apply all the enabled types.
  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().HasAll(
      Difference(enabled_types_, syncer::ControlTypes())));
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           enabled_types_).Empty());
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetEnabledTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());
}

TEST_F(SyncBackendHostTest, DisableTypes) {
  // Simulate first time sync.
  InitializeBackend(true);
  fake_manager_->GetAndResetCleanedTypes();
  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      enabled_types_));
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           enabled_types_).Empty());
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());

  // Then disable two datatypes.
  syncer::ModelTypeSet disabled_types(syncer::BOOKMARKS,
                                      syncer::SEARCH_ENGINES);
  syncer::ModelTypeSet old_types = enabled_types_;
  enabled_types_.RemoveAll(disabled_types);
  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));

  // Only those datatypes disabled should be cleaned. Nothing should be
  // downloaded.
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Empty());
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           old_types).Equals(disabled_types));
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetEnabledTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());
}

TEST_F(SyncBackendHostTest, AddTypes) {
  // Simulate first time sync.
  InitializeBackend(true);
  fake_manager_->GetAndResetCleanedTypes();
  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      enabled_types_));
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           enabled_types_).Empty());
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());

  // Then add two datatypes.
  syncer::ModelTypeSet new_types(syncer::EXTENSIONS,
                                 syncer::APPS);
  enabled_types_.PutAll(new_types);
  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));

  // Only those datatypes added should be downloaded (plus nigori). Nothing
  // should be cleaned aside from the disabled types.
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      Union(new_types, syncer::ModelTypeSet(syncer::NIGORI))));
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           enabled_types_).Empty());
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetEnabledTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());
}

// And and disable in the same configuration.
TEST_F(SyncBackendHostTest, AddDisableTypes) {
  // Simulate first time sync.
  InitializeBackend(true);
  fake_manager_->GetAndResetCleanedTypes();
  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      enabled_types_));
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           enabled_types_).Empty());
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());

  // Then add two datatypes.
  syncer::ModelTypeSet old_types = enabled_types_;
  syncer::ModelTypeSet disabled_types(syncer::BOOKMARKS,
                                      syncer::SEARCH_ENGINES);
  syncer::ModelTypeSet new_types(syncer::EXTENSIONS,
                                 syncer::APPS);
  enabled_types_.PutAll(new_types);
  enabled_types_.RemoveAll(disabled_types);
  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));

  // Only those datatypes added should be downloaded (plus nigori). Nothing
  // should be cleaned aside from the disabled types.
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      Union(new_types, syncer::ModelTypeSet(syncer::NIGORI))));
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           old_types).Equals(disabled_types));
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetEnabledTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      old_types).Equals(disabled_types));
}

// Test restarting the browser to newly supported datatypes. The new datatypes
// should be downloaded on the configuration after backend initialization.
TEST_F(SyncBackendHostTest, NewlySupportedTypes) {
  sync_prefs_->SetSyncSetupCompleted();
  // Set sync manager behavior before passing it down. All types have progress
  // markers and initial sync ended except the new types.
  syncer::ModelTypeSet old_types = enabled_types_;
  fake_manager_factory_->set_progress_marker_types(old_types);
  fake_manager_factory_->set_initial_sync_ended_types(old_types);
  syncer::ModelTypeSet new_types(syncer::APP_SETTINGS,
                                 syncer::EXTENSION_SETTINGS);
  enabled_types_.PutAll(new_types);

  // Does nothing.
  InitializeBackend(true);
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Empty());
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           old_types).Empty());
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(old_types));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Equals(new_types));

  // Downloads and applies the new types.
  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      Union(new_types, syncer::ModelTypeSet(syncer::NIGORI))));
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           enabled_types_).Empty());
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetEnabledTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());
}

// Test the newly supported types scenario, but with the presence of partial
// types as well. Both partial and newly supported types should be downloaded
// the configuration.
TEST_F(SyncBackendHostTest, NewlySupportedTypesWithPartialTypes) {
  sync_prefs_->SetSyncSetupCompleted();
  // Set sync manager behavior before passing it down. All types have progress
  // markers and initial sync ended except the new types.
  syncer::ModelTypeSet old_types = enabled_types_;
  syncer::ModelTypeSet partial_types(syncer::NIGORI, syncer::BOOKMARKS);
  syncer::ModelTypeSet full_types =
      Difference(enabled_types_, partial_types);
  fake_manager_factory_->set_progress_marker_types(old_types);
  fake_manager_factory_->set_initial_sync_ended_types(full_types);
  syncer::ModelTypeSet new_types(syncer::APP_SETTINGS,
                                 syncer::EXTENSION_SETTINGS);
  enabled_types_.PutAll(new_types);

  // Purge the partial types.  The nigori will be among the purged types, but
  // the syncer will re-download it by the time the initialization is complete.
  InitializeBackend(true);
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      syncer::ModelTypeSet(syncer::NIGORI)));
  EXPECT_TRUE(fake_manager_->GetAndResetCleanedTypes().HasAll(partial_types));
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(
      syncer::Union(full_types, syncer::ModelTypeSet(syncer::NIGORI))));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Equals(Union(new_types, Difference(
                      partial_types, syncer::ModelTypeSet(syncer::NIGORI)))));

  // Downloads and applies the new types and partial types (which includes
  // nigori anyways).
  ConfigureDataTypes(enabled_types_,
                     Difference(syncer::ModelTypeSet::All(),
                                enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(
      Union(new_types, partial_types)));
  EXPECT_TRUE(Intersection(fake_manager_->GetAndResetCleanedTypes(),
                           enabled_types_).Empty());
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetAndResetEnabledTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());
}

// Ensure the device info tracker is initialized properly on startup.
TEST_F(SyncBackendHostTest, InitializeDeviceInfo) {
  ASSERT_EQ(NULL, backend_->GetSyncedDeviceTracker());

  InitializeBackend(true);
  const SyncedDeviceTracker* device_tracker =
      backend_->GetSyncedDeviceTracker();
  ASSERT_TRUE(device_tracker->ReadLocalDeviceInfo());
}

// Verify that downloading control types only downloads those types that do
// not have initial sync ended set.
TEST_F(SyncBackendHostTest, DownloadControlTypes) {
  sync_prefs_->SetSyncSetupCompleted();
  // Set sync manager behavior before passing it down. Experiments and device
  // info are new types without progress markers or initial sync ended, while
  // all other types have been fully downloaded and applied.
  syncer::ModelTypeSet new_types(syncer::EXPERIMENTS, syncer::DEVICE_INFO);
  syncer::ModelTypeSet old_types =
      Difference(enabled_types_, new_types);
  fake_manager_factory_->set_progress_marker_types(old_types);
  fake_manager_factory_->set_initial_sync_ended_types(old_types);

  // Bringing up the backend should download the new types without downloading
  // any old types.
  InitializeBackend(true);
  EXPECT_TRUE(fake_manager_->GetAndResetDownloadedTypes().Equals(new_types));
  EXPECT_TRUE(fake_manager_->GetAndResetCleanedTypes().Equals(
                  Difference(syncer::ModelTypeSet::All(),
                             enabled_types_)));
  EXPECT_TRUE(fake_manager_->InitialSyncEndedTypes().Equals(enabled_types_));
  EXPECT_TRUE(fake_manager_->GetTypesWithEmptyProgressMarkerToken(
      enabled_types_).Empty());
}

// Fail to download control types.  It's believed that there is a server bug
// which can allow this to happen (crbug.com/164288).  The sync backend host
// should detect this condition and fail to initialize the backend.
//
// The failure is "silent" in the sense that the GetUpdates request appears to
// be successful, but it returned no results.  This means that the usual
// download retry logic will not be invoked.
TEST_F(SyncBackendHostTest, SilentlyFailToDownloadControlTypes) {
  fake_manager_factory_->set_configure_fail_types(syncer::ModelTypeSet::All());
  InitializeBackend(false);
}

// Test that local refresh requests are delivered to sync.
TEST_F(SyncBackendHostTest, ForwardLocalRefreshRequest) {
  InitializeBackend(true);

  syncer::ModelTypeSet set1 = syncer::ModelTypeSet::All();
  IssueRefreshRequest(set1);
  fake_manager_->WaitForSyncThread();
  EXPECT_TRUE(set1.Equals(fake_manager_->GetLastRefreshRequestTypes()));

  syncer::ModelTypeSet set2 = syncer::ModelTypeSet(syncer::SESSIONS);
  IssueRefreshRequest(set2);
  fake_manager_->WaitForSyncThread();
  EXPECT_TRUE(set2.Equals(fake_manager_->GetLastRefreshRequestTypes()));
}

// Test that local invalidations issued before sync is initialized are ignored.
TEST_F(SyncBackendHostTest, AttemptForwardLocalRefreshRequestEarly) {
  syncer::ModelTypeSet set1 = syncer::ModelTypeSet::All();
  IssueRefreshRequest(set1);

  InitializeBackend(true);

  fake_manager_->WaitForSyncThread();
  EXPECT_FALSE(set1.Equals(fake_manager_->GetLastRefreshRequestTypes()));
}

// Test that local invalidations issued while sync is shutting down are ignored.
TEST_F(SyncBackendHostTest, AttemptForwardLocalRefreshRequestLate) {
  InitializeBackend(true);

  backend_->StopSyncingForShutdown();

  syncer::ModelTypeSet types = syncer::ModelTypeSet::All();
  IssueRefreshRequest(types);
  fake_manager_->WaitForSyncThread();
  EXPECT_FALSE(types.Equals(fake_manager_->GetLastRefreshRequestTypes()));

  backend_->Shutdown(SyncBackendHost::STOP);
  backend_.reset();
}

// Test that configuration on signin sends the proper GU source.
TEST_F(SyncBackendHostTest, DownloadControlTypesNewClient) {
  InitializeBackend(true);
  EXPECT_EQ(syncer::CONFIGURE_REASON_NEW_CLIENT,
            fake_manager_->GetAndResetConfigureReason());
}

// Test that configuration on restart sends the proper GU source.
TEST_F(SyncBackendHostTest, DownloadControlTypesRestart) {
  sync_prefs_->SetSyncSetupCompleted();
  InitializeBackend(true);
  EXPECT_EQ(syncer::CONFIGURE_REASON_NEWLY_ENABLED_DATA_TYPE,
            fake_manager_->GetAndResetConfigureReason());
}

}  // namespace

}  // namespace browser_sync
