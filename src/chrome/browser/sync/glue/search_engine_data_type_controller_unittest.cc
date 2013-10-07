// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/run_loop.h"
#include "base/tracked_objects.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/search_engines/template_url_service_test_util.h"
#include "chrome/browser/sync/glue/data_type_controller_mock.h"
#include "chrome/browser/sync/glue/fake_generic_change_processor.h"
#include "chrome/browser/sync/glue/search_engine_data_type_controller.h"
#include "chrome/browser/sync/profile_sync_components_factory_mock.h"
#include "chrome/browser/sync/profile_sync_service_mock.h"
#include "chrome/test/base/profile_mock.h"
#include "content/public/browser/notification_service.h"
#include "sync/api/fake_syncable_service.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::SetArgumentPointee;

namespace browser_sync {
namespace {

ACTION(MakeSharedChangeProcessor) {
  return new SharedChangeProcessor();
}

ACTION_P(ReturnAndRelease, change_processor) {
  return change_processor->release();
}

class SyncSearchEngineDataTypeControllerTest : public testing::Test {
 public:
  SyncSearchEngineDataTypeControllerTest()
      : change_processor_(new FakeGenericChangeProcessor()) {}

  virtual void SetUp() {
    test_util_.SetUp();
    profile_sync_factory_.reset(new ProfileSyncComponentsFactoryMock());
    // Feed the DTC test_util_'s profile so it is reused later.
    // This allows us to control the associated TemplateURLService.
    search_engine_dtc_ =
        new SearchEngineDataTypeController(profile_sync_factory_.get(),
                                           test_util_.profile(),
                                           &service_);
  }

  virtual void TearDown() {
    // Must be done before we pump the loop.
    syncable_service_.StopSyncing(syncer::SEARCH_ENGINES);
    search_engine_dtc_ = NULL;
    test_util_.TearDown();
  }

 protected:
  // Pre-emptively get the TURL Service and make sure it is loaded.
  void PreloadTemplateURLService() {
    test_util_.VerifyLoad();
  }

  void SetStartExpectations() {
    // Ownership gets passed to caller of CreateGenericChangeProcessor.
    EXPECT_CALL(model_load_callback_, Run(_, _));
    EXPECT_CALL(*profile_sync_factory_,
                GetSyncableServiceForType(syncer::SEARCH_ENGINES)).
        WillOnce(Return(syncable_service_.AsWeakPtr()));
    EXPECT_CALL(*profile_sync_factory_, CreateSharedChangeProcessor()).
        WillOnce(MakeSharedChangeProcessor());
    EXPECT_CALL(*profile_sync_factory_,
                CreateGenericChangeProcessor(_, _, _, _)).
        WillOnce(ReturnAndRelease(&change_processor_));
  }

  void SetActivateExpectations() {
    EXPECT_CALL(service_, ActivateDataType(syncer::SEARCH_ENGINES, _, _));
  }

  void SetStopExpectations() {
    EXPECT_CALL(service_, DeactivateDataType(syncer::SEARCH_ENGINES));
  }

  void Start() {
    search_engine_dtc_->LoadModels(
        base::Bind(&ModelLoadCallbackMock::Run,
                   base::Unretained(&model_load_callback_)));
    search_engine_dtc_->StartAssociating(
        base::Bind(&StartCallbackMock::Run,
                   base::Unretained(&start_callback_)));
  }

  // This also manages a BrowserThread and MessageLoop for us. Note that this
  // must be declared here as the destruction order of the BrowserThread
  // matters - we could leak if this is declared below.
  TemplateURLServiceTestUtil test_util_;
  scoped_refptr<SearchEngineDataTypeController> search_engine_dtc_;
  scoped_ptr<ProfileSyncComponentsFactoryMock> profile_sync_factory_;
  ProfileSyncServiceMock service_;
  scoped_ptr<FakeGenericChangeProcessor> change_processor_;
  syncer::FakeSyncableService syncable_service_;
  StartCallbackMock start_callback_;
  ModelLoadCallbackMock model_load_callback_;
};

TEST_F(SyncSearchEngineDataTypeControllerTest, StartURLServiceReady) {
  SetStartExpectations();
  // We want to start ready.
  PreloadTemplateURLService();
  SetActivateExpectations();
  EXPECT_CALL(start_callback_, Run(DataTypeController::OK, _, _));

  EXPECT_EQ(DataTypeController::NOT_RUNNING, search_engine_dtc_->state());
  EXPECT_FALSE(syncable_service_.syncing());
  Start();
  EXPECT_EQ(DataTypeController::RUNNING, search_engine_dtc_->state());
  EXPECT_TRUE(syncable_service_.syncing());
}

TEST_F(SyncSearchEngineDataTypeControllerTest, StartURLServiceNotReady) {
  EXPECT_CALL(*profile_sync_factory_, CreateSharedChangeProcessor()).
      WillOnce(MakeSharedChangeProcessor());

  EXPECT_CALL(model_load_callback_, Run(_, _));
  EXPECT_FALSE(syncable_service_.syncing());
  search_engine_dtc_->LoadModels(
      base::Bind(&ModelLoadCallbackMock::Run,
                 base::Unretained(&model_load_callback_)));
  EXPECT_EQ(DataTypeController::MODEL_STARTING, search_engine_dtc_->state());
  EXPECT_FALSE(syncable_service_.syncing());

  // Send the notification that the TemplateURLService has started.
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_TEMPLATE_URL_SERVICE_LOADED,
      content::Source<TemplateURLService>(test_util_.model()),
      content::NotificationService::NoDetails());
  EXPECT_EQ(DataTypeController::MODEL_LOADED, search_engine_dtc_->state());

  // Wait until WebDB is loaded before we shut it down.
  base::RunLoop().RunUntilIdle();
}

TEST_F(SyncSearchEngineDataTypeControllerTest, StartFirstRun) {
  SetStartExpectations();
  PreloadTemplateURLService();
  SetActivateExpectations();
  change_processor_->set_sync_model_has_user_created_nodes(false);
  EXPECT_CALL(start_callback_, Run(DataTypeController::OK_FIRST_RUN, _, _));

  Start();
  EXPECT_TRUE(syncable_service_.syncing());
}

TEST_F(SyncSearchEngineDataTypeControllerTest, StartAssociationFailed) {
  SetStartExpectations();
  PreloadTemplateURLService();
  SetStopExpectations();
  EXPECT_CALL(start_callback_,
              Run(DataTypeController::ASSOCIATION_FAILED, _, _));
  syncable_service_.set_merge_data_and_start_syncing_error(
      syncer::SyncError(FROM_HERE,
                        syncer::SyncError::DATATYPE_ERROR,
                        "Error",
                        syncer::SEARCH_ENGINES));

  Start();
  EXPECT_EQ(DataTypeController::DISABLED, search_engine_dtc_->state());
  EXPECT_FALSE(syncable_service_.syncing());
  search_engine_dtc_->Stop();
  EXPECT_EQ(DataTypeController::NOT_RUNNING, search_engine_dtc_->state());
  EXPECT_FALSE(syncable_service_.syncing());
}

TEST_F(SyncSearchEngineDataTypeControllerTest,
       StartAssociationTriggersUnrecoverableError) {
  SetStartExpectations();
  PreloadTemplateURLService();
  EXPECT_CALL(start_callback_,
              Run(DataTypeController::UNRECOVERABLE_ERROR, _, _));
  // Set up association to fail with an unrecoverable error.
  change_processor_->set_sync_model_has_user_created_nodes_success(false);

  Start();
  EXPECT_EQ(DataTypeController::NOT_RUNNING, search_engine_dtc_->state());
  EXPECT_FALSE(syncable_service_.syncing());
}

TEST_F(SyncSearchEngineDataTypeControllerTest, Stop) {
  SetStartExpectations();
  PreloadTemplateURLService();
  SetActivateExpectations();
  SetStopExpectations();
  EXPECT_CALL(start_callback_, Run(DataTypeController::OK, _, _));

  EXPECT_EQ(DataTypeController::NOT_RUNNING, search_engine_dtc_->state());
  EXPECT_FALSE(syncable_service_.syncing());
  Start();
  EXPECT_EQ(DataTypeController::RUNNING, search_engine_dtc_->state());
  EXPECT_TRUE(syncable_service_.syncing());
  search_engine_dtc_->Stop();
  EXPECT_EQ(DataTypeController::NOT_RUNNING, search_engine_dtc_->state());
  EXPECT_FALSE(syncable_service_.syncing());
}

TEST_F(SyncSearchEngineDataTypeControllerTest,
       OnSingleDatatypeUnrecoverableError) {
  SetStartExpectations();
  PreloadTemplateURLService();
  SetActivateExpectations();
  EXPECT_CALL(service_, DisableBrokenDatatype(_, _, _)).
      WillOnce(InvokeWithoutArgs(search_engine_dtc_.get(),
                                 &SearchEngineDataTypeController::Stop));
  SetStopExpectations();

  EXPECT_CALL(start_callback_, Run(DataTypeController::OK, _, _));
  Start();
  // This should cause search_engine_dtc_->Stop() to be called.
  search_engine_dtc_->OnSingleDatatypeUnrecoverableError(FROM_HERE, "Test");
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(DataTypeController::NOT_RUNNING, search_engine_dtc_->state());
  EXPECT_FALSE(syncable_service_.syncing());
}

}  // namespace
}  // namespace browser_sync
