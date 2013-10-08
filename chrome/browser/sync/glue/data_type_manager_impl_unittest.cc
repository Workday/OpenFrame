// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/data_type_manager_impl.h"

#include "base/compiler_specific.h"
#include "base/message_loop/message_loop.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/sync/glue/backend_data_type_configurer.h"
#include "chrome/browser/sync/glue/data_type_controller.h"
#include "chrome/browser/sync/glue/data_type_encryption_handler.h"
#include "chrome/browser/sync/glue/data_type_manager_observer.h"
#include "chrome/browser/sync/glue/failed_data_types_handler.h"
#include "chrome/browser/sync/glue/fake_data_type_controller.h"
#include "content/public/test/test_browser_thread.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/configure_reason.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace browser_sync {

using syncer::ModelType;
using syncer::ModelTypeSet;
using syncer::ModelTypeToString;
using syncer::BOOKMARKS;
using syncer::APPS;
using syncer::PASSWORDS;
using syncer::PREFERENCES;
using syncer::NIGORI;
using testing::_;
using testing::Mock;
using testing::ResultOf;

namespace {

// Used by SetConfigureDoneExpectation.
DataTypeManager::ConfigureStatus GetStatus(
    const DataTypeManager::ConfigureResult& result) {
  return result.status;
}

// Those types that are priority AND always configured.
syncer::ModelTypeSet HighPriorityTypes() {
  syncer::ModelTypeSet result = syncer::PriorityCoreTypes();
  return result;
}

// Helper for unioning with priority types.
syncer::ModelTypeSet AddHighPriorityTypesTo(syncer::ModelTypeSet types) {
  syncer::ModelTypeSet result = HighPriorityTypes();
  result.PutAll(types);
  return result;
}

// Helper for unioning with core types.
syncer::ModelTypeSet AddLowPriorityCoreTypesTo(syncer::ModelTypeSet types) {
  syncer::ModelTypeSet result = syncer::Difference(syncer::CoreTypes(),
                                                   syncer::PriorityCoreTypes());
  result.PutAll(types);
  return result;
}

// Fake BackendDataTypeConfigurer implementation that simply stores away the
// callback passed into ConfigureDataTypes.
class FakeBackendDataTypeConfigurer : public BackendDataTypeConfigurer {
 public:
  FakeBackendDataTypeConfigurer() {}
  virtual ~FakeBackendDataTypeConfigurer() {}

  virtual void ConfigureDataTypes(
      syncer::ConfigureReason reason,
      const DataTypeConfigStateMap& config_state_map,
      const base::Callback<void(ModelTypeSet,
                                ModelTypeSet)>& ready_task,
      const base::Callback<void()>& retry_callback) OVERRIDE {
    last_ready_task_ = ready_task;

    if (!expected_configure_types_.Empty()) {
      EXPECT_TRUE(
          expected_configure_types_.Equals(
              GetDataTypesInState(CONFIGURE_ACTIVE, config_state_map)))
          << syncer::ModelTypeSetToString(expected_configure_types_)
          << " v.s. "
          << syncer::ModelTypeSetToString(
              GetDataTypesInState(CONFIGURE_ACTIVE, config_state_map));
    }
  }

  base::Callback<void(ModelTypeSet, ModelTypeSet)> last_ready_task() const {
    return last_ready_task_;
  }

  void set_expected_configure_types(syncer::ModelTypeSet types) {
    expected_configure_types_ = types;
  }

 private:
  base::Callback<void(ModelTypeSet, ModelTypeSet)> last_ready_task_;
  syncer::ModelTypeSet expected_configure_types_;
};

// Mock DataTypeManagerObserver implementation.
class DataTypeManagerObserverMock : public DataTypeManagerObserver {
 public:
  DataTypeManagerObserverMock() {}
  virtual ~DataTypeManagerObserverMock() {}

  MOCK_METHOD1(OnConfigureDone,
               void(const browser_sync::DataTypeManager::ConfigureResult&));
  MOCK_METHOD0(OnConfigureRetry, void());
  MOCK_METHOD0(OnConfigureStart, void());
};

class FakeDataTypeEncryptionHandler : public DataTypeEncryptionHandler {
 public:
  FakeDataTypeEncryptionHandler();
  virtual ~FakeDataTypeEncryptionHandler();

  virtual bool IsPassphraseRequired() const OVERRIDE;
  virtual syncer::ModelTypeSet GetEncryptedDataTypes() const OVERRIDE;

  void set_passphrase_required(bool passphrase_required) {
    passphrase_required_ = passphrase_required;
  }
  void set_encrypted_types(syncer::ModelTypeSet encrypted_types) {
    encrypted_types_ = encrypted_types;
  }
 private:
  bool passphrase_required_;
  syncer::ModelTypeSet encrypted_types_;
};

FakeDataTypeEncryptionHandler::FakeDataTypeEncryptionHandler()
    : passphrase_required_(false) {}
FakeDataTypeEncryptionHandler::~FakeDataTypeEncryptionHandler() {}

bool FakeDataTypeEncryptionHandler::IsPassphraseRequired() const {
  return passphrase_required_;
}

syncer::ModelTypeSet
FakeDataTypeEncryptionHandler::GetEncryptedDataTypes() const {
  return encrypted_types_;
}

} // namespace

class TestDataTypeManager : public DataTypeManagerImpl {
 public:
  TestDataTypeManager(
      const syncer::WeakHandle<syncer::DataTypeDebugInfoListener>&
          debug_info_listener,
      BackendDataTypeConfigurer* configurer,
      const DataTypeController::TypeMap* controllers,
      const DataTypeEncryptionHandler* encryption_handler,
      DataTypeManagerObserver* observer,
      FailedDataTypesHandler* failed_data_types_handler)
      : DataTypeManagerImpl(debug_info_listener,
                            controllers,
                            encryption_handler,
                            configurer,
                            observer,
                            failed_data_types_handler),
        custom_priority_types_(HighPriorityTypes()) {}

  void set_priority_types(const syncer::ModelTypeSet& priority_types) {
    custom_priority_types_ = priority_types;
  }

 private:
  virtual syncer::ModelTypeSet GetPriorityTypes() const OVERRIDE {
    return custom_priority_types_;
  }

  syncer::ModelTypeSet custom_priority_types_;
};

// The actual test harness class, parametrized on nigori state (i.e., tests are
// run both configuring with nigori, and configuring without).
class SyncDataTypeManagerImplTest : public testing::Test {
 public:
  SyncDataTypeManagerImplTest()
      : ui_thread_(content::BrowserThread::UI, &ui_loop_) {}

  virtual ~SyncDataTypeManagerImplTest() {
  }

 protected:
  virtual void SetUp() {
   dtm_.reset(
       new TestDataTypeManager(
           syncer::WeakHandle<syncer::DataTypeDebugInfoListener>(),
           &configurer_,
           &controllers_,
           &encryption_handler_,
           &observer_,
           &failed_data_types_handler_));
  }

  void SetConfigureStartExpectation() {
    EXPECT_CALL(observer_, OnConfigureStart());
  }

  void SetConfigureDoneExpectation(DataTypeManager::ConfigureStatus status) {
    EXPECT_CALL(observer_, OnConfigureDone(ResultOf(&GetStatus, status)));
  }

  // Configure the given DTM with the given desired types.
  void Configure(DataTypeManagerImpl* dtm,
                 const syncer::ModelTypeSet& desired_types) {
    dtm->Configure(desired_types, syncer::CONFIGURE_REASON_RECONFIGURATION);
  }

  // Finish downloading for the given DTM. Should be done only after
  // a call to Configure().
  void FinishDownload(const DataTypeManager& dtm,
                      ModelTypeSet types_to_configure,
                      ModelTypeSet failed_download_types) {
    EXPECT_TRUE(DataTypeManager::DOWNLOAD_PENDING == dtm.state() ||
                DataTypeManager::CONFIGURING == dtm.state());
    ASSERT_FALSE(configurer_.last_ready_task().is_null());
    configurer_.last_ready_task().Run(
        syncer::Difference(types_to_configure, failed_download_types),
        failed_download_types);
  }

  // Adds a fake controller for the given type to |controllers_|.
  // Should be called only before setting up the DTM.
  void AddController(ModelType model_type) {
    controllers_[model_type] = new FakeDataTypeController(model_type);
  }

  // Gets the fake controller for the given type, which should have
  // been previously added via AddController().
  scoped_refptr<FakeDataTypeController> GetController(
      ModelType model_type) const {
    DataTypeController::TypeMap::const_iterator it =
        controllers_.find(model_type);
    if (it == controllers_.end()) {
      return NULL;
    }
    return make_scoped_refptr(
        static_cast<FakeDataTypeController*>(it->second.get()));
  }

  void FailEncryptionFor(syncer::ModelTypeSet encrypted_types) {
    encryption_handler_.set_passphrase_required(true);
    encryption_handler_.set_encrypted_types(encrypted_types);
  }

  base::MessageLoopForUI ui_loop_;
  content::TestBrowserThread ui_thread_;
  DataTypeController::TypeMap controllers_;
  FakeBackendDataTypeConfigurer configurer_;
  DataTypeManagerObserverMock observer_;
  scoped_ptr<TestDataTypeManager> dtm_;
  FailedDataTypesHandler failed_data_types_handler_;
  FakeDataTypeEncryptionHandler encryption_handler_;
};

// Set up a DTM with no controllers, configure it, finish downloading,
// and then stop it.
TEST_F(SyncDataTypeManagerImplTest, NoControllers) {
  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  Configure(dtm_.get(), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  dtm_->Stop();
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}

// Set up a DTM with a single controller, configure it, finish
// downloading, finish starting the controller, and then stop the DTM.
TEST_F(SyncDataTypeManagerImplTest, ConfigureOne) {
  AddController(BOOKMARKS);

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  dtm_->Stop();
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}

// Set up a DTM with 2 controllers. configure it. One of them finishes loading
// after the  timeout. Make sure eventually all are configured.
TEST_F(SyncDataTypeManagerImplTest, ConfigureSlowLoadingType) {
  AddController(BOOKMARKS);
  AddController(APPS);

  GetController(BOOKMARKS)->SetDelayModelLoad();

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::PARTIAL_SUCCESS);

  syncer::ModelTypeSet types;
  types.Put(BOOKMARKS);
  types.Put(APPS);

  Configure(dtm_.get(), types);
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, types, ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  base::OneShotTimer<ModelAssociationManager>* timer =
    dtm_->GetModelAssociationManagerForTesting()->GetTimerForTesting();

  base::Closure task = timer->user_task();
  timer->Stop();
  task.Run();

  SetConfigureDoneExpectation(DataTypeManager::OK);
  GetController(APPS)->FinishStart(DataTypeController::OK);

  SetConfigureStartExpectation();
  GetController(BOOKMARKS)->SimulateModelLoadFinishing();

  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  GetController(BOOKMARKS)->SimulateModelLoadFinishing();

  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  dtm_->Stop();
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}


// Set up a DTM with a single controller, configure it, but stop it
// before finishing the download.  It should still be safe to run the
// download callback even after the DTM is stopped and destroyed.
TEST_F(SyncDataTypeManagerImplTest, ConfigureOneStopWhileDownloadPending) {
  AddController(BOOKMARKS);

  {
    SetConfigureStartExpectation();
    SetConfigureDoneExpectation(DataTypeManager::ABORTED);

    Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
    EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

    dtm_->Stop();
    EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
  }

  configurer_.last_ready_task().Run(ModelTypeSet(BOOKMARKS), ModelTypeSet());
}

// Set up a DTM with a single controller, configure it, finish
// downloading, but stop the DTM before the controller finishes
// starting up.  It should still be safe to finish starting up the
// controller even after the DTM is stopped and destroyed.
TEST_F(SyncDataTypeManagerImplTest, ConfigureOneStopWhileStartingModel) {
  AddController(BOOKMARKS);

  {
    SetConfigureStartExpectation();
    SetConfigureDoneExpectation(DataTypeManager::ABORTED);

    Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
    EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

    FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
    FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
    EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

    dtm_->Stop();
    EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
    dtm_.reset();
  }

  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
}

// Set up a DTM with a single controller, configure it, finish
// downloading, start the controller's model, but stop the DTM before
// the controller finishes starting up.  It should still be safe to
// finish starting up the controller even after the DTM is stopped and
// destroyed.
TEST_F(SyncDataTypeManagerImplTest, ConfigureOneStopWhileAssociating) {
  AddController(BOOKMARKS);

  {
    SetConfigureStartExpectation();
    SetConfigureDoneExpectation(DataTypeManager::ABORTED);

    Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
    EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

    FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
    FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
    EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

    dtm_->Stop();
    EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
    dtm_.reset();
  }

  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
}

// Set up a DTM with a single controller.  Then:
//
//   1) Configure.
//   2) Finish the download for step 1.
//   3) Finish starting the controller with the NEEDS_CRYPTO status.
//   4) Complete download for the reconfiguration without the controller.
//   5) Stop the DTM.
TEST_F(SyncDataTypeManagerImplTest, OneWaitingForCrypto) {
  AddController(PASSWORDS);

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::PARTIAL_SUCCESS);

  const ModelTypeSet types(PASSWORDS);

  // Step 1.
  Configure(dtm_.get(), types);
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 2.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, types, ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 3.
  FailEncryptionFor(types);
  GetController(PASSWORDS)->FinishStart(DataTypeController::NEEDS_CRYPTO);
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 4.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  // Step 5.
  dtm_->Stop();
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}

// Set up a DTM with two controllers.  Then:
//
//   1) Configure with first controller.
//   2) Finish the download for step 1.
//   3) Finish starting the first controller.
//   4) Configure with both controllers.
//   5) Finish the download for step 4.
//   6) Finish starting the second controller.
//   7) Stop the DTM.
TEST_F(SyncDataTypeManagerImplTest, ConfigureOneThenBoth) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  // Step 1.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 2.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 3.
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  Mock::VerifyAndClearExpectations(&observer_);
  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  // Step 4.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 5.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS, PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 6.
  GetController(PREFERENCES)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  // Step 7.
  dtm_->Stop();
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}

// Set up a DTM with two controllers.  Then:
//
//   1) Configure with first controller.
//   2) Finish the download for step 1.
//   3) Finish starting the first controller.
//   4) Configure with second controller.
//   5) Finish the download for step 4.
//   6) Finish starting the second controller.
//   7) Stop the DTM.
TEST_F(SyncDataTypeManagerImplTest, ConfigureOneThenSwitch) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  // Step 1.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 2.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 3.
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  Mock::VerifyAndClearExpectations(&observer_);
  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  // Step 4.
  Configure(dtm_.get(), ModelTypeSet(PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 5.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 6.
  GetController(PREFERENCES)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  // Step 7.
  dtm_->Stop();
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}

// Set up a DTM with two controllers.  Then:
//
//   1) Configure with first controller.
//   2) Finish the download for step 1.
//   3) Configure with both controllers.
//   4) Finish starting the first controller.
//   5) Finish the download for step 3.
//   6) Finish starting the second controller.
//   7) Stop the DTM.
TEST_F(SyncDataTypeManagerImplTest, ConfigureWhileOneInFlight) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  // Step 1.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 2.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 3.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 4.
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 5.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS, PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 6.
  GetController(PREFERENCES)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  // Step 7.
  dtm_->Stop();
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}

// Set up a DTM with one controller.  Then configure, finish
// downloading, and start the controller with an unrecoverable error.
// The unrecoverable error should cause the DTM to stop.
TEST_F(SyncDataTypeManagerImplTest, OneFailingController) {
  AddController(BOOKMARKS);

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::UNRECOVERABLE_ERROR);

  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  GetController(BOOKMARKS)->FinishStart(
      DataTypeController::UNRECOVERABLE_ERROR);
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}

// Set up a DTM with two controllers.  Then:
//
//   1) Configure with both controllers.
//   2) Finish the download for step 1.
//   3) Finish starting the first controller successfully.
//   4) Finish starting the second controller with an unrecoverable error.
//
// The failure from step 4 should cause the DTM to stop.
TEST_F(SyncDataTypeManagerImplTest, SecondControllerFails) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::UNRECOVERABLE_ERROR);

  // Step 1.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 2.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS, PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 3.
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 4.
  GetController(PREFERENCES)->FinishStart(
      DataTypeController::UNRECOVERABLE_ERROR);
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}

// Set up a DTM with two controllers.  Then:
//
//   1) Configure with both controllers.
//   2) Finish the download for step 1.
//   3) Finish starting the first controller successfully.
//   4) Finish starting the second controller with an association failure.
//   5) Finish the purge/reconfigure without the failed type.
//   6) Stop the DTM.
//
// The association failure from step 3 should be ignored.
//
// TODO(akalin): Check that the data type that failed association is
// recorded in the CONFIGURE_DONE notification.
TEST_F(SyncDataTypeManagerImplTest, OneControllerFailsAssociation) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::PARTIAL_SUCCESS);

  // Step 1.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 2.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS, PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 3.
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 4.
  GetController(PREFERENCES)->FinishStart(
      DataTypeController::ASSOCIATION_FAILED);
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 5.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  // Step 6.
  dtm_->Stop();
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}

// Set up a DTM with two controllers.  Then:
//
//   1) Configure with first controller.
//   2) Configure with both controllers.
//   3) Finish the download for step 1.
//   4) Finish the download for step 2.
//   5) Finish starting both controllers.
//   6) Stop the DTM.
TEST_F(SyncDataTypeManagerImplTest, ConfigureWhileDownloadPending) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  // Step 1.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 2.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 3.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Step 4.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS, PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 5.
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());
  GetController(PREFERENCES)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  // Step 6.
  dtm_->Stop();
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}

// Set up a DTM with two controllers.  Then:
//
//   1) Configure with first controller.
//   2) Configure with both controllers.
//   3) Finish the download for step 1 with a failed data type.
//   4) Finish the download for step 2 successfully.
//   5) Finish starting both controllers.
//   6) Stop the DTM.
//
// The failure from step 3 should be ignored since there's a
// reconfigure pending from step 2.
TEST_F(SyncDataTypeManagerImplTest, ConfigureWhileDownloadPendingWithFailure) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  // Step 1.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 2.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 3.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 4.
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS, PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Step 5.
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());
  GetController(PREFERENCES)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());

  // Step 6.
  dtm_->Stop();
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
}

// Tests a Purge then Configure.  This is similar to the sequence of
// operations that would be invoked by the BackendMigrator.
TEST_F(SyncDataTypeManagerImplTest, MigrateAll) {
  AddController(BOOKMARKS);

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  // Initial setup.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);

  // We've now configured bookmarks and (implicitly) the control types.
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());
  Mock::VerifyAndClearExpectations(&observer_);

  // Pretend we were told to migrate all types.
  ModelTypeSet to_migrate;
  to_migrate.Put(BOOKMARKS);
  to_migrate.PutAll(syncer::ControlTypes());

  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);
  dtm_->PurgeForMigration(to_migrate,
                          syncer::CONFIGURE_REASON_MIGRATION);
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // The DTM will call ConfigureDataTypes(), even though it is unnecessary.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());
  Mock::VerifyAndClearExpectations(&observer_);

  // Re-enable the migrated types.
  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);
  Configure(dtm_.get(), to_migrate);
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, to_migrate, ModelTypeSet());
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());
}

// Test receipt of a Configure request while a purge is in flight.
TEST_F(SyncDataTypeManagerImplTest, ConfigureDuringPurge) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);

  // Initial configure.
  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS));
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());
  Mock::VerifyAndClearExpectations(&observer_);

  // Purge the Nigori type.
  SetConfigureStartExpectation();
  dtm_->PurgeForMigration(ModelTypeSet(NIGORI),
                          syncer::CONFIGURE_REASON_MIGRATION);
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());
  Mock::VerifyAndClearExpectations(&observer_);

  // Before the backend configuration completes, ask for a different
  // set of types.  This request asks for
  // - BOOKMARKS: which is redundant because it was already enabled,
  // - PREFERENCES: which is new and will need to be downloaded, and
  // - NIGORI: (added implicitly because it is a control type) which
  //   the DTM is part-way through purging.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Invoke the callback we've been waiting for since we asked to purge NIGORI.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  Mock::VerifyAndClearExpectations(&observer_);

  SetConfigureDoneExpectation(DataTypeManager::OK);
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Now invoke the callback for the second configure request.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS, PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Start the preferences controller.  We don't need to start controller for
  // the NIGORI because it has none.  We don't need to start the controller for
  // the BOOKMARKS because it was never stopped.
  GetController(PREFERENCES)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());
}

TEST_F(SyncDataTypeManagerImplTest, PrioritizedConfiguration) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);

  dtm_->set_priority_types(
      AddHighPriorityTypesTo(syncer::ModelTypeSet(PREFERENCES)));

  // Initial configure.
  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  // Initially only PREFERENCES is configured.
  configurer_.set_expected_configure_types(
      AddHighPriorityTypesTo(ModelTypeSet(PREFERENCES)));
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // BOOKMARKS is configured after download of PREFERENCES finishes.
  configurer_.set_expected_configure_types(
      AddLowPriorityCoreTypesTo(ModelTypeSet(BOOKMARKS)));
  FinishDownload(*dtm_, ModelTypeSet(PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  GetController(PREFERENCES)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());
}

TEST_F(SyncDataTypeManagerImplTest, PrioritizedConfigurationReconfigure) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);
  AddController(APPS);

  dtm_->set_priority_types(
      AddHighPriorityTypesTo(syncer::ModelTypeSet(PREFERENCES)));

  // Initial configure.
  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::OK);

  // Reconfigure while associating PREFERENCES and downloading BOOKMARKS.
  configurer_.set_expected_configure_types(
      AddHighPriorityTypesTo(ModelTypeSet(PREFERENCES)));
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  configurer_.set_expected_configure_types(
      AddLowPriorityCoreTypesTo(ModelTypeSet(BOOKMARKS)));
  FinishDownload(*dtm_, ModelTypeSet(PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Enable syncing for APPS.
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES, APPS));
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Reconfiguration starts after downloading and association of previous
  // types finish.
  configurer_.set_expected_configure_types(
      AddHighPriorityTypesTo(ModelTypeSet(PREFERENCES)));
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  GetController(PREFERENCES)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  configurer_.set_expected_configure_types(
      AddLowPriorityCoreTypesTo(ModelTypeSet(BOOKMARKS, APPS)));
  FinishDownload(*dtm_, ModelTypeSet(PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS, APPS), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Skip calling FinishStart() for PREFENCES because it's already started in
  // first configuration.
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);
  GetController(APPS)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());
}

TEST_F(SyncDataTypeManagerImplTest, PrioritizedConfigurationStop) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);

  dtm_->set_priority_types(
      AddHighPriorityTypesTo(syncer::ModelTypeSet(PREFERENCES)));

  // Initial configure.
  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::ABORTED);

  // Initially only PREFERENCES is configured.
  configurer_.set_expected_configure_types(
      AddHighPriorityTypesTo(ModelTypeSet(PREFERENCES)));
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // BOOKMARKS is configured after download of PREFERENCES finishes.
  configurer_.set_expected_configure_types(
      AddLowPriorityCoreTypesTo(ModelTypeSet(BOOKMARKS)));
  FinishDownload(*dtm_, ModelTypeSet(PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // PERFERENCES controller is associating while BOOKMARKS is downloading.
  EXPECT_EQ(DataTypeController::ASSOCIATING,
            GetController(PREFERENCES)->state());
  EXPECT_EQ(DataTypeController::NOT_RUNNING, GetController(BOOKMARKS)->state());

  dtm_->Stop();
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
  EXPECT_EQ(DataTypeController::NOT_RUNNING,
            GetController(PREFERENCES)->state());
  EXPECT_EQ(DataTypeController::NOT_RUNNING, GetController(BOOKMARKS)->state());
}

TEST_F(SyncDataTypeManagerImplTest, PrioritizedConfigurationDownloadError) {
  AddController(BOOKMARKS);
  AddController(PREFERENCES);

  dtm_->set_priority_types(
      AddHighPriorityTypesTo(syncer::ModelTypeSet(PREFERENCES)));

  // Initial configure.
  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::UNRECOVERABLE_ERROR);

  // Initially only PREFERENCES is configured.
  configurer_.set_expected_configure_types(
      AddHighPriorityTypesTo(ModelTypeSet(PREFERENCES)));
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // BOOKMARKS is configured after download of PREFERENCES finishes.
  configurer_.set_expected_configure_types(
      AddLowPriorityCoreTypesTo(ModelTypeSet(BOOKMARKS)));
  FinishDownload(*dtm_, ModelTypeSet(PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // PERFERENCES controller is associating while BOOKMARKS is downloading.
  EXPECT_EQ(DataTypeController::ASSOCIATING,
            GetController(PREFERENCES)->state());
  EXPECT_EQ(DataTypeController::NOT_RUNNING, GetController(BOOKMARKS)->state());

  // Make BOOKMARKS download fail.
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet(BOOKMARKS));
  EXPECT_EQ(DataTypeManager::STOPPED, dtm_->state());
  EXPECT_EQ(DataTypeController::NOT_RUNNING,
            GetController(PREFERENCES)->state());
  EXPECT_EQ(DataTypeController::NOT_RUNNING, GetController(BOOKMARKS)->state());
}

TEST_F(SyncDataTypeManagerImplTest, HighPriorityAssociationFailure) {
  AddController(PREFERENCES);   // Will fail.
  AddController(BOOKMARKS);     // Will succeed.

  dtm_->set_priority_types(
      AddHighPriorityTypesTo(syncer::ModelTypeSet(PREFERENCES)));

  // Initial configure.
  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::PARTIAL_SUCCESS);

  // Initially only PREFERENCES is configured.
  configurer_.set_expected_configure_types(
      AddHighPriorityTypesTo(ModelTypeSet(PREFERENCES)));
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // BOOKMARKS is configured after download of PREFERENCES finishes.
  configurer_.set_expected_configure_types(
      AddLowPriorityCoreTypesTo(ModelTypeSet(BOOKMARKS)));
  FinishDownload(*dtm_, ModelTypeSet(PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // PERFERENCES controller is associating while BOOKMARKS is downloading.
  EXPECT_EQ(DataTypeController::ASSOCIATING,
            GetController(PREFERENCES)->state());
  EXPECT_EQ(DataTypeController::NOT_RUNNING, GetController(BOOKMARKS)->state());

  // Make PREFERENCES association fail.
  GetController(PREFERENCES)->FinishStart(
      DataTypeController::ASSOCIATION_FAILED);
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // Reconfigure without PREFERENCES after the BOOKMARKS download completes,
  // then reconfigure with BOOKMARKS.
  configurer_.set_expected_configure_types(HighPriorityTypes());
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  configurer_.set_expected_configure_types(
      AddLowPriorityCoreTypesTo(ModelTypeSet(BOOKMARKS)));
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());

  // Reconfigure with BOOKMARKS.
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  EXPECT_EQ(DataTypeController::ASSOCIATING,
            GetController(BOOKMARKS)->state());
  GetController(BOOKMARKS)->FinishStart(DataTypeController::OK);

  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());
  EXPECT_EQ(DataTypeController::NOT_RUNNING,
            GetController(PREFERENCES)->state());
  EXPECT_EQ(DataTypeController::RUNNING, GetController(BOOKMARKS)->state());
}

TEST_F(SyncDataTypeManagerImplTest, LowPriorityAssociationFailure) {
  AddController(PREFERENCES);  // Will succeed.
  AddController(BOOKMARKS);    // Will fail.

  dtm_->set_priority_types(
      AddHighPriorityTypesTo(syncer::ModelTypeSet(PREFERENCES)));

  // Initial configure.
  SetConfigureStartExpectation();
  SetConfigureDoneExpectation(DataTypeManager::PARTIAL_SUCCESS);

  // Initially only PREFERENCES is configured.
  configurer_.set_expected_configure_types(
      AddHighPriorityTypesTo(ModelTypeSet(PREFERENCES)));
  Configure(dtm_.get(), ModelTypeSet(BOOKMARKS, PREFERENCES));
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // BOOKMARKS is configured after download of PREFERENCES finishes.
  configurer_.set_expected_configure_types(
      AddLowPriorityCoreTypesTo(ModelTypeSet(BOOKMARKS)));
  FinishDownload(*dtm_, ModelTypeSet(PREFERENCES), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURING, dtm_->state());

  // PERFERENCES controller is associating while BOOKMARKS is downloading.
  EXPECT_EQ(DataTypeController::ASSOCIATING,
            GetController(PREFERENCES)->state());
  EXPECT_EQ(DataTypeController::NOT_RUNNING, GetController(BOOKMARKS)->state());

  // BOOKMARKS finishes downloading and PREFERENCES finishes associating.
  FinishDownload(*dtm_, ModelTypeSet(BOOKMARKS), ModelTypeSet());
  GetController(PREFERENCES)->FinishStart(DataTypeController::OK);
  EXPECT_EQ(DataTypeController::RUNNING, GetController(PREFERENCES)->state());

  // Make BOOKMARKS association fail, which triggers reconfigure with only
  // PREFERENCES.
  configurer_.set_expected_configure_types(
      AddHighPriorityTypesTo(ModelTypeSet(PREFERENCES)));
  GetController(BOOKMARKS)->FinishStart(DataTypeController::ASSOCIATION_FAILED);
  EXPECT_EQ(DataTypeController::NOT_RUNNING,
            GetController(BOOKMARKS)->state());
  EXPECT_EQ(DataTypeManager::DOWNLOAD_PENDING, dtm_->state());

  // Finish configuration with only PREFERENCES.
  configurer_.set_expected_configure_types(
      AddLowPriorityCoreTypesTo(ModelTypeSet()));
  FinishDownload(*dtm_, ModelTypeSet(PREFERENCES), ModelTypeSet());
  FinishDownload(*dtm_, ModelTypeSet(), ModelTypeSet());
  EXPECT_EQ(DataTypeManager::CONFIGURED, dtm_->state());
  EXPECT_EQ(DataTypeController::RUNNING, GetController(PREFERENCES)->state());
  EXPECT_EQ(DataTypeController::NOT_RUNNING,
            GetController(BOOKMARKS)->state());
}

}  // namespace browser_sync
