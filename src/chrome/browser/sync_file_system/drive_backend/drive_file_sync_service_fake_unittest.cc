// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/drive_backend/drive_file_sync_service.h"

#include <utility>

#include "base/command_line.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/run_loop.h"
#include "chrome/browser/drive/drive_uploader.h"
#include "chrome/browser/drive/fake_drive_service.h"
#include "chrome/browser/extensions/test_extension_service.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/google_apis/gdata_errorcode.h"
#include "chrome/browser/google_apis/gdata_wapi_parser.h"
#include "chrome/browser/google_apis/test_util.h"
#include "chrome/browser/sync_file_system/drive_backend/api_util.h"
#include "chrome/browser/sync_file_system/drive_backend/drive_file_sync_util.h"
#include "chrome/browser/sync_file_system/drive_backend/drive_metadata_store.h"
#include "chrome/browser/sync_file_system/drive_backend/fake_drive_service_helper.h"
#include "chrome/browser/sync_file_system/file_status_observer.h"
#include "chrome/browser/sync_file_system/mock_remote_change_processor.h"
#include "chrome/browser/sync_file_system/sync_direction.h"
#include "chrome/browser/sync_file_system/sync_file_metadata.h"
#include "chrome/browser/sync_file_system/sync_file_system.pb.h"
#include "chrome/browser/sync_file_system/syncable_file_system_util.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_builder.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "extensions/common/id_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/common/fileapi/file_system_util.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#endif

#define FPL(x) FILE_PATH_LITERAL(x)

using ::testing::StrictMock;
using ::testing::_;

using drive::DriveServiceInterface;
using drive::DriveUploader;
using drive::DriveUploaderInterface;
using drive::FakeDriveService;

using extensions::Extension;
using extensions::DictionaryBuilder;
using google_apis::GDataErrorCode;
using google_apis::ResourceEntry;

namespace sync_file_system {

using drive_backend::APIUtil;
using drive_backend::APIUtilInterface;
using drive_backend::FakeDriveServiceHelper;

namespace {

const char kExtensionName1[] = "example1";
const char kExtensionName2[] = "example2";

void DidInitialize(bool* done, SyncStatusCode status, bool created) {
  EXPECT_FALSE(*done);
  *done = true;
  EXPECT_EQ(SYNC_STATUS_OK, status);
  EXPECT_TRUE(created);
}

void DidProcessRemoteChange(SyncStatusCode* status_out,
                            fileapi::FileSystemURL* url_out,
                            SyncStatusCode status,
                            const fileapi::FileSystemURL& url) {
  ASSERT_TRUE(status_out);
  ASSERT_TRUE(url_out);
  *status_out = status;
  *url_out = url;
}

void ExpectEqStatus(bool* done,
                    SyncStatusCode expected,
                    SyncStatusCode actual) {
  EXPECT_FALSE(*done);
  *done = true;
  EXPECT_EQ(expected, actual);
}

// Mocks adding an installed extension to ExtensionService.
scoped_refptr<const extensions::Extension> AddTestExtension(
    ExtensionService* extension_service,
    const base::FilePath::StringType& extension_name) {
  std::string id = extensions::id_util::GenerateIdForPath(
      base::FilePath(extension_name));

  scoped_refptr<const Extension> extension =
      extensions::ExtensionBuilder().SetManifest(
          DictionaryBuilder()
            .Set("name", extension_name)
            .Set("version", "1.0"))
          .SetID(id)
      .Build();
  extension_service->AddExtension(extension.get());
  return extension;
}

// Converts extension_name to extension ID.
std::string ExtensionNameToId(const std::string& extension_name) {
  base::FilePath path = base::FilePath::FromUTF8Unsafe(extension_name);
  return extensions::id_util::GenerateIdForPath(path);
}

// Converts extension_name to GURL version.
GURL ExtensionNameToGURL(const std::string& extension_name) {
  return extensions::Extension::GetBaseURLFromExtensionId(
      ExtensionNameToId(extension_name));
}

ACTION(InvokeCompletionCallback) {
  base::MessageLoopProxy::current()->PostTask(FROM_HERE, arg1);
}

ACTION(PrepareForRemoteChange_Busy) {
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(arg1,
                 SYNC_STATUS_FILE_BUSY,
                 SyncFileMetadata(),
                 FileChangeList()));
}

ACTION(PrepareForRemoteChange_NotFound) {
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(arg1,
                 SYNC_STATUS_OK,
                 SyncFileMetadata(SYNC_FILE_TYPE_UNKNOWN, 0, base::Time()),
                 FileChangeList()));
}

ACTION(PrepareForRemoteChange_NotModified) {
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(arg1,
                 SYNC_STATUS_OK,
                 SyncFileMetadata(SYNC_FILE_TYPE_FILE, 0, base::Time()),
                 FileChangeList()));
}

ACTION(InvokeDidApplyRemoteChange) {
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE, base::Bind(arg3, SYNC_STATUS_OK));
}

}  // namespace

class MockFileStatusObserver: public FileStatusObserver {
 public:
  MockFileStatusObserver() {}
  virtual ~MockFileStatusObserver() {}

  MOCK_METHOD4(OnFileStatusChanged,
               void(const fileapi::FileSystemURL& url,
                    SyncFileStatus sync_status,
                    SyncAction action_taken,
                    SyncDirection direction));
};

class DriveFileSyncServiceFakeTest : public testing::Test {
 public:
  DriveFileSyncServiceFakeTest()
      : thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP),
        fake_drive_service_(NULL) {
  }

  virtual void SetUp() OVERRIDE {
    profile_.reset(new TestingProfile());

    // Add TestExtensionSystem with registered ExtensionIds used in tests.
    extensions::TestExtensionSystem* extension_system(
        static_cast<extensions::TestExtensionSystem*>(
            extensions::ExtensionSystem::Get(profile_.get())));
    extension_system->CreateExtensionService(
        CommandLine::ForCurrentProcess(), base::FilePath(), false);
    extension_service_ = extension_system->Get(
        profile_.get())->extension_service();

    AddTestExtension(extension_service_, FPL("example1"));
    AddTestExtension(extension_service_, FPL("example2"));

    RegisterSyncableFileSystem();

    fake_drive_service_ = new FakeDriveService;
    DriveUploaderInterface* drive_uploader = new DriveUploader(
        fake_drive_service_, base::MessageLoopProxy::current().get());

    fake_drive_helper_.reset(new FakeDriveServiceHelper(
        fake_drive_service_, drive_uploader));

    api_util_ = APIUtil::CreateForTesting(
        fake_drive_helper_->base_dir_path().AppendASCII("tmp"),
        scoped_ptr<DriveServiceInterface>(fake_drive_service_),
        scoped_ptr<DriveUploaderInterface>(drive_uploader)).Pass();
    metadata_store_.reset(new DriveMetadataStore(
        fake_drive_helper_->base_dir_path(),
        base::MessageLoopProxy::current().get()));

    bool done = false;
    metadata_store_->Initialize(base::Bind(&DidInitialize, &done));
    base::RunLoop().RunUntilIdle();
    EXPECT_TRUE(done);

    fake_drive_service_->LoadResourceListForWapi(
        "sync_file_system/initialize.json");
    fake_drive_service()->LoadAccountMetadataForWapi(
        "sync_file_system/account_metadata.json");

    // Setup the sync root directory.
    EXPECT_EQ(google_apis::HTTP_CREATED,
              fake_drive_helper_->AddOrphanedFolder(
                  APIUtil::GetSyncRootDirectoryName(),
                  &sync_root_resource_id_));
    metadata_store()->SetSyncRootDirectory(sync_root_resource_id_);
  }

  void SetUpDriveSyncService(bool enabled) {
    sync_service_ = DriveFileSyncService::CreateForTesting(
        profile_.get(),
        fake_drive_helper_->base_dir_path(),
        api_util_.PassAs<APIUtilInterface>(),
        metadata_store_.Pass()).Pass();
    sync_service_->AddFileStatusObserver(&mock_file_status_observer_);
    sync_service_->SetRemoteChangeProcessor(mock_remote_processor());
    sync_service_->SetSyncEnabled(enabled);
    base::RunLoop().RunUntilIdle();
  }

  virtual void TearDown() OVERRIDE {
    if (sync_service_) {
      sync_service_.reset();
    }

    metadata_store_.reset();
    api_util_.reset();
    fake_drive_service_ = NULL;

    RevokeSyncableFileSystem();

    extension_service_ = NULL;
    profile_.reset();
    base::RunLoop().RunUntilIdle();
  }

  void SetSyncEnabled(bool enabled) {
    sync_service_->SetSyncEnabled(enabled);
  }

 protected:
  void EnableExtension(const std::string& extension_id) {
    extension_service_->EnableExtension(extension_id);
  }

  void DisableExtension(const std::string& extension_id) {
    extension_service_->DisableExtension(
        extension_id, extensions::Extension::DISABLE_NONE);
  }

  void UninstallExtension(const std::string& extension_id) {
    // Call UnloadExtension instead of UninstallExtension since it does
    // unnecessary cleanup (e.g. deleting extension data) and emits warnings.
    extension_service_->UnloadExtension(
        extension_id, extension_misc::UNLOAD_REASON_UNINSTALL);
  }

  void UpdateRegisteredOrigins() {
    sync_service_->UpdateRegisteredOrigins();
    // Wait for completion of uninstalling origin.
    base::RunLoop().RunUntilIdle();
  }

  void VerifySizeOfRegisteredOrigins(size_t b_size,
                                     size_t i_size,
                                     size_t d_size) {
    EXPECT_EQ(b_size, pending_batch_sync_origins()->size());
    EXPECT_EQ(i_size, metadata_store()->incremental_sync_origins().size());
    EXPECT_EQ(d_size, metadata_store()->disabled_origins().size());
  }

  APIUtilInterface* api_util() {
    if (api_util_)
      return api_util_.get();
    return sync_service_->api_util_.get();
  }

  DriveMetadataStore* metadata_store() {
    if (metadata_store_)
      return metadata_store_.get();
    return sync_service_->metadata_store_.get();
  }

  FakeDriveService* fake_drive_service() {
    return fake_drive_service_;
  }

  StrictMock<MockFileStatusObserver>* mock_file_status_observer() {
    return &mock_file_status_observer_;
  }

  StrictMock<MockRemoteChangeProcessor>* mock_remote_processor() {
    return &mock_remote_processor_;
  }

  DriveFileSyncService* sync_service() { return sync_service_.get(); }
  std::map<GURL, std::string>* pending_batch_sync_origins() {
    return &(sync_service()->pending_batch_sync_origins_);
  }

  const RemoteChangeHandler& remote_change_handler() const {
    return sync_service_->remote_change_handler_;
  }

  fileapi::FileSystemURL CreateURL(const GURL& origin,
                                   const std::string& filename) {
    return CreateSyncableFileSystemURL(
        origin, base::FilePath::FromUTF8Unsafe(filename));
  }

  void ProcessRemoteChange(SyncStatusCode expected_status,
                           const fileapi::FileSystemURL& expected_url,
                           SyncFileStatus expected_sync_file_status,
                           SyncAction expected_sync_action,
                           SyncDirection expected_sync_direction) {
    SyncStatusCode actual_status = SYNC_STATUS_UNKNOWN;
    fileapi::FileSystemURL actual_url;

    if (expected_sync_file_status != SYNC_FILE_STATUS_UNKNOWN) {
      EXPECT_CALL(*mock_file_status_observer(),
                  OnFileStatusChanged(expected_url,
                                      expected_sync_file_status,
                                      expected_sync_action,
                                      expected_sync_direction))
          .Times(1);
    }

    sync_service_->ProcessRemoteChange(
        base::Bind(&DidProcessRemoteChange, &actual_status, &actual_url));
    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(expected_status, actual_status);
    EXPECT_EQ(expected_url, actual_url);
  }

  bool AppendIncrementalRemoteChangeByResourceId(
      const std::string& resource_id,
      const GURL& origin) {
    scoped_ptr<ResourceEntry> entry;
    EXPECT_EQ(google_apis::HTTP_SUCCESS,
              fake_drive_helper_->GetResourceEntry(resource_id, &entry));
    return sync_service_->AppendRemoteChange(origin, *entry, 12345);
  }

  bool AppendIncrementalRemoteChange(
      const GURL& origin,
      const base::FilePath& path,
      bool is_deleted,
      const std::string& resource_id,
      int64 changestamp,
      const std::string& remote_file_md5) {
    return sync_service_->AppendRemoteChangeInternal(
        origin, path, is_deleted, resource_id,
        changestamp, remote_file_md5, base::Time(),
        SYNC_FILE_TYPE_FILE);
  }

  std::string SetUpOriginRootDirectory(const char* extension_name) {
    EXPECT_TRUE(!sync_root_resource_id_.empty());

    std::string origin_root_resource_id;
    EXPECT_EQ(google_apis::HTTP_CREATED,
              fake_drive_helper_->AddFolder(
                  sync_root_resource_id_,
                  ExtensionNameToId(extension_name),
                  &origin_root_resource_id));

    metadata_store()->AddIncrementalSyncOrigin(
        ExtensionNameToGURL(extension_name), origin_root_resource_id);
    return origin_root_resource_id;
  }

  void TestRegisterNewOrigin();
  void TestRegisterExistingOrigin();
  void TestRegisterOriginWithSyncDisabled();
  void TestUnregisterOrigin();
  void TestUpdateRegisteredOrigins();
  void TestRemoteChange_NoChange();
  void TestRemoteChange_Busy();
  void TestRemoteChange_NewFile();
  void TestRemoteChange_UpdateFile();
  void TestRemoteChange_Override();
  void TestRemoteChange_Folder();

 private:
  content::TestBrowserThreadBundle thread_bundle_;

  scoped_ptr<TestingProfile> profile_;

  std::string sync_root_resource_id_;

#if defined OS_CHROMEOS
  chromeos::ScopedTestDeviceSettingsService test_device_settings_service_;
  chromeos::ScopedTestCrosSettings test_cros_settings_;
  chromeos::ScopedTestUserManager test_user_manager_;
#endif

  scoped_ptr<DriveFileSyncService> sync_service_;

  // Not owned.
  ExtensionService* extension_service_;

  FakeDriveService* fake_drive_service_;
  scoped_ptr<FakeDriveServiceHelper> fake_drive_helper_;

  StrictMock<MockFileStatusObserver> mock_file_status_observer_;
  StrictMock<MockRemoteChangeProcessor> mock_remote_processor_;

  scoped_ptr<APIUtil> api_util_;
  scoped_ptr<DriveMetadataStore> metadata_store_;

  DISALLOW_COPY_AND_ASSIGN(DriveFileSyncServiceFakeTest);
};

#if !defined(OS_ANDROID)

void DriveFileSyncServiceFakeTest::TestRegisterNewOrigin() {
  SetUpDriveSyncService(true);
  bool done = false;
  sync_service()->RegisterOriginForTrackingChanges(
      ExtensionNameToGURL(kExtensionName1),
      base::Bind(&ExpectEqStatus, &done, SYNC_STATUS_OK));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(done);

  VerifySizeOfRegisteredOrigins(0u, 1u, 0u);
  EXPECT_TRUE(!remote_change_handler().HasChanges());
}

void DriveFileSyncServiceFakeTest::TestRegisterExistingOrigin() {
  const std::string origin_resource_id =
      SetUpOriginRootDirectory(kExtensionName1);

  std::string file_id;
  EXPECT_EQ(google_apis::HTTP_SUCCESS,
            fake_drive_helper_->AddFile(
                origin_resource_id, "1.txt", "data1", &file_id));
  EXPECT_EQ(google_apis::HTTP_SUCCESS,
            fake_drive_helper_->AddFile(
                origin_resource_id, "2.txt", "data2", &file_id));
  EXPECT_EQ(google_apis::HTTP_SUCCESS,
            fake_drive_helper_->AddFile(
                origin_resource_id, "3.txt", "data3", &file_id));

  SetUpDriveSyncService(true);

  bool done = false;
  sync_service()->RegisterOriginForTrackingChanges(
      ExtensionNameToGURL(kExtensionName1),
      base::Bind(&ExpectEqStatus, &done, SYNC_STATUS_OK));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(done);

  // The origin should be registered as an incremental sync origin.
  VerifySizeOfRegisteredOrigins(0u, 1u, 0u);

  // There are 3 items to sync.
  EXPECT_EQ(3u, remote_change_handler().ChangesSize());
}

void DriveFileSyncServiceFakeTest::TestRegisterOriginWithSyncDisabled() {
  // Usually the sync service starts here, but since we're setting up a drive
  // service with sync disabled sync doesn't start (while register origin should
  // still return OK).
  SetUpDriveSyncService(false);

  bool done = false;
  sync_service()->RegisterOriginForTrackingChanges(
      ExtensionNameToGURL(kExtensionName1),
      base::Bind(&ExpectEqStatus, &done, SYNC_STATUS_OK));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(done);

  // We must not have started batch sync for the newly registered origin,
  // so it should still be in the batch_sync_origins.
  VerifySizeOfRegisteredOrigins(1u, 0u, 0u);
  EXPECT_TRUE(!remote_change_handler().HasChanges());
}

void DriveFileSyncServiceFakeTest::TestUnregisterOrigin() {
  SetUpOriginRootDirectory(kExtensionName1);
  SetUpOriginRootDirectory(kExtensionName2);

  SetUpDriveSyncService(true);

  VerifySizeOfRegisteredOrigins(0u, 2u, 0u);
  EXPECT_EQ(0u, remote_change_handler().ChangesSize());

  bool done = false;
  sync_service()->UnregisterOriginForTrackingChanges(
      ExtensionNameToGURL(kExtensionName1),
      base::Bind(&ExpectEqStatus, &done, SYNC_STATUS_OK));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(done);

  VerifySizeOfRegisteredOrigins(0u, 1u, 0u);
  EXPECT_TRUE(!remote_change_handler().HasChanges());
}

void DriveFileSyncServiceFakeTest::TestUpdateRegisteredOrigins() {
  SetUpOriginRootDirectory(kExtensionName1);
  SetUpOriginRootDirectory(kExtensionName2);
  SetUpDriveSyncService(true);

  // [1] Both extensions and origins are enabled. Nothing to do.
  VerifySizeOfRegisteredOrigins(0u, 2u, 0u);
  UpdateRegisteredOrigins();
  VerifySizeOfRegisteredOrigins(0u, 2u, 0u);

  // [2] Extension 1 should move to disabled list.
  DisableExtension(ExtensionNameToId(kExtensionName1));
  UpdateRegisteredOrigins();
  VerifySizeOfRegisteredOrigins(0u, 1u, 1u);

  // [3] Make sure that state remains the same, nothing should change.
  UpdateRegisteredOrigins();
  VerifySizeOfRegisteredOrigins(0u, 1u, 1u);

  // [4] Uninstall Extension 2.
  UninstallExtension(ExtensionNameToId(kExtensionName2));
  UpdateRegisteredOrigins();
  VerifySizeOfRegisteredOrigins(0u, 0u, 1u);

  // [5] Re-enable Extension 1. It moves back to batch and not to incremental.
  EnableExtension(ExtensionNameToId(kExtensionName1));
  UpdateRegisteredOrigins();
  VerifySizeOfRegisteredOrigins(1u, 0u, 0u);
}

void DriveFileSyncServiceFakeTest::TestRemoteChange_NoChange() {
  SetUpDriveSyncService(true);

  ProcessRemoteChange(SYNC_STATUS_NO_CHANGE_TO_SYNC,
                      fileapi::FileSystemURL(),
                      SYNC_FILE_STATUS_UNKNOWN,
                      SYNC_ACTION_NONE,
                      SYNC_DIRECTION_NONE);
  VerifySizeOfRegisteredOrigins(0u, 0u, 0u);
  EXPECT_TRUE(!remote_change_handler().HasChanges());
}

void DriveFileSyncServiceFakeTest::TestRemoteChange_Busy() {
  const char kFileName[] = "File 1.txt";
  const GURL origin = ExtensionNameToGURL(kExtensionName1);

  const std::string origin_resource_id =
      SetUpOriginRootDirectory(kExtensionName1);

  EXPECT_CALL(*mock_remote_processor(),
              PrepareForProcessRemoteChange(CreateURL(origin, kFileName), _))
      .WillOnce(PrepareForRemoteChange_Busy());
  EXPECT_CALL(*mock_remote_processor(),
              ClearLocalChanges(CreateURL(origin, kFileName), _))
      .WillOnce(InvokeCompletionCallback());

  SetUpDriveSyncService(true);

  std::string resource_id;
  EXPECT_EQ(google_apis::HTTP_SUCCESS,
            fake_drive_helper_->AddFile(
                origin_resource_id, kFileName, "test data", &resource_id));
  EXPECT_TRUE(AppendIncrementalRemoteChangeByResourceId(resource_id, origin));

  ProcessRemoteChange(SYNC_STATUS_FILE_BUSY,
                      CreateURL(origin, kFileName),
                      SYNC_FILE_STATUS_UNKNOWN,
                      SYNC_ACTION_NONE,
                      SYNC_DIRECTION_NONE);
}

void DriveFileSyncServiceFakeTest::TestRemoteChange_NewFile() {
  const char kFileName[] = "File 1.txt";
  const GURL origin = ExtensionNameToGURL(kExtensionName1);

  const std::string origin_resource_id =
      SetUpOriginRootDirectory(kExtensionName1);

  EXPECT_CALL(*mock_remote_processor(),
              PrepareForProcessRemoteChange(CreateURL(origin, kFileName), _))
      .WillOnce(PrepareForRemoteChange_NotFound());
  EXPECT_CALL(*mock_remote_processor(),
              ClearLocalChanges(CreateURL(origin, kFileName), _))
      .WillOnce(InvokeCompletionCallback());

  EXPECT_CALL(*mock_remote_processor(),
              ApplyRemoteChange(_, _, CreateURL(origin, kFileName), _))
      .WillOnce(InvokeDidApplyRemoteChange());

  SetUpDriveSyncService(true);

  std::string resource_id;
  EXPECT_EQ(google_apis::HTTP_SUCCESS,
            fake_drive_helper_->AddFile(
                origin_resource_id, kFileName, "test data", &resource_id));
  EXPECT_TRUE(AppendIncrementalRemoteChangeByResourceId(resource_id, origin));

  ProcessRemoteChange(SYNC_STATUS_OK,
                      CreateURL(origin, kFileName),
                      SYNC_FILE_STATUS_SYNCED,
                      SYNC_ACTION_ADDED,
                      SYNC_DIRECTION_REMOTE_TO_LOCAL);
}

void DriveFileSyncServiceFakeTest::TestRemoteChange_UpdateFile() {
  const char kFileName[] = "File 1.txt";
  const GURL origin = ExtensionNameToGURL(kExtensionName1);

  const std::string origin_resource_id =
      SetUpOriginRootDirectory(kExtensionName1);

  EXPECT_CALL(*mock_remote_processor(),
              PrepareForProcessRemoteChange(CreateURL(origin, kFileName), _))
      .WillOnce(PrepareForRemoteChange_NotModified());
  EXPECT_CALL(*mock_remote_processor(),
              ClearLocalChanges(CreateURL(origin, kFileName), _))
      .WillOnce(InvokeCompletionCallback());

  EXPECT_CALL(*mock_remote_processor(),
              ApplyRemoteChange(_, _, CreateURL(origin, kFileName), _))
      .WillOnce(InvokeDidApplyRemoteChange());

  SetUpDriveSyncService(true);

  std::string resource_id;
  EXPECT_EQ(google_apis::HTTP_SUCCESS,
            fake_drive_helper_->AddFile(
                origin_resource_id, kFileName, "test data", &resource_id));
  EXPECT_TRUE(AppendIncrementalRemoteChangeByResourceId(resource_id, origin));

  ProcessRemoteChange(SYNC_STATUS_OK,
                      CreateURL(origin, kFileName),
                      SYNC_FILE_STATUS_SYNCED,
                      SYNC_ACTION_UPDATED,
                      SYNC_DIRECTION_REMOTE_TO_LOCAL);
}

void DriveFileSyncServiceFakeTest::TestRemoteChange_Override() {
  const base::FilePath kFilePath(FPL("File 1.txt"));
  const std::string kFileResourceId("file:2_file_resource_id");
  const std::string kFileResourceId2("file:2_file_resource_id_2");
  const GURL origin = ExtensionNameToGURL(kExtensionName1);

  SetUpOriginRootDirectory(kExtensionName1);
  SetUpDriveSyncService(true);

  EXPECT_TRUE(AppendIncrementalRemoteChange(
      origin, kFilePath, false /* is_deleted */,
      kFileResourceId, 2, "remote_file_md5"));

  // Expect to drop this change since there is another newer change on the
  // queue.
  EXPECT_FALSE(AppendIncrementalRemoteChange(
      origin, kFilePath, false /* is_deleted */,
      kFileResourceId, 1, "remote_file_md5_2"));

  // Expect to drop this change since it has the same md5 with the previous one.
  EXPECT_FALSE(AppendIncrementalRemoteChange(
      origin, kFilePath, false /* is_deleted */,
      kFileResourceId, 4, "remote_file_md5"));

  // This should not cause browser crash.
  EXPECT_FALSE(AppendIncrementalRemoteChange(
      origin, kFilePath, false /* is_deleted */,
      kFileResourceId, 4, "remote_file_md5"));

  // Expect to drop these changes since they have different resource IDs with
  // the previous ones.
  EXPECT_FALSE(AppendIncrementalRemoteChange(
      origin, kFilePath, false /* is_deleted */,
      kFileResourceId2, 5, "updated_file_md5"));
  EXPECT_FALSE(AppendIncrementalRemoteChange(
      origin, kFilePath, true /* is_deleted */,
      kFileResourceId2, 5, "deleted_file_md5"));

  // Push delete change.
  EXPECT_TRUE(AppendIncrementalRemoteChange(
      origin, kFilePath, true /* is_deleted */,
      kFileResourceId, 6, "deleted_file_md5"));

  // Expect to drop this delete change since it has a different resource ID with
  // the previous one.
  EXPECT_FALSE(AppendIncrementalRemoteChange(
      origin, kFilePath, true /* is_deleted */,
      kFileResourceId2, 7, "deleted_file_md5"));

  // Expect not to drop this change even if it has a different resource ID with
  // the previous one.
  EXPECT_TRUE(AppendIncrementalRemoteChange(
      origin, kFilePath, false /* is_deleted */,
      kFileResourceId2, 8, "updated_file_md5"));
}

void DriveFileSyncServiceFakeTest::TestRemoteChange_Folder() {
  const std::string origin_resource_id =
      SetUpOriginRootDirectory(kExtensionName1);
  SetUpDriveSyncService(true);

  std::string resource_id;
  EXPECT_EQ(google_apis::HTTP_CREATED,
            fake_drive_helper_->AddFolder(
                origin_resource_id, "test_dir", &resource_id));

  // Expect to drop this change for file.
  EXPECT_FALSE(AppendIncrementalRemoteChangeByResourceId(
      resource_id, ExtensionNameToGURL(kExtensionName1)));
}

TEST_F(DriveFileSyncServiceFakeTest, RegisterNewOrigin) {
  ASSERT_FALSE(IsDriveAPIDisabled());
  TestRegisterNewOrigin();
}

TEST_F(DriveFileSyncServiceFakeTest, RegisterNewOrigin_WAPI) {
  ScopedDisableDriveAPI disable_drive_api;
  TestRegisterNewOrigin();
}

TEST_F(DriveFileSyncServiceFakeTest, RegisterExistingOrigin) {
  ASSERT_FALSE(IsDriveAPIDisabled());
  TestRegisterExistingOrigin();
}

TEST_F(DriveFileSyncServiceFakeTest, RegisterExistingOrigin_WAPI) {
  ScopedDisableDriveAPI disable_drive_api;
  TestRegisterExistingOrigin();
}

TEST_F(DriveFileSyncServiceFakeTest, RegisterOriginWithSyncDisabled) {
  ASSERT_FALSE(IsDriveAPIDisabled());
  TestRegisterOriginWithSyncDisabled();
}

TEST_F(DriveFileSyncServiceFakeTest, RegisterOriginWithSyncDisabled_WAPI) {
  ScopedDisableDriveAPI disable_drive_api;
  TestRegisterOriginWithSyncDisabled();
}

TEST_F(DriveFileSyncServiceFakeTest, UnregisterOrigin) {
  ASSERT_FALSE(IsDriveAPIDisabled());
  TestUnregisterOrigin();
}

TEST_F(DriveFileSyncServiceFakeTest, UnregisterOrigin_WAPI) {
  ScopedDisableDriveAPI disable_drive_api;
  TestUnregisterOrigin();
}

TEST_F(DriveFileSyncServiceFakeTest, UpdateRegisteredOrigins) {
  ASSERT_FALSE(IsDriveAPIDisabled());
  TestUpdateRegisteredOrigins();
}

TEST_F(DriveFileSyncServiceFakeTest, UpdateRegisteredOrigins_WAPI) {
  ScopedDisableDriveAPI disable_drive_api;
  TestUpdateRegisteredOrigins();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_NoChange) {
  ASSERT_FALSE(IsDriveAPIDisabled());
  TestRemoteChange_NoChange();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_NoChange_WAPI) {
  ScopedDisableDriveAPI disable_drive_api;
  TestRemoteChange_NoChange();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_Busy) {
  ASSERT_FALSE(IsDriveAPIDisabled());
  TestRemoteChange_Busy();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_Busy_WAPI) {
  ScopedDisableDriveAPI disable_drive_api;
  TestRemoteChange_Busy();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_NewFile) {
  ASSERT_FALSE(IsDriveAPIDisabled());
  TestRemoteChange_NewFile();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_NewFile_WAPI) {
  ScopedDisableDriveAPI disable_drive_api;
  TestRemoteChange_NewFile();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_UpdateFile) {
  ASSERT_FALSE(IsDriveAPIDisabled());
  TestRemoteChange_UpdateFile();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_UpdateFile_WAPI) {
  ScopedDisableDriveAPI disable_drive_api;
  TestRemoteChange_UpdateFile();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_Override) {
  ASSERT_FALSE(IsDriveAPIDisabled());
  TestRemoteChange_Override();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_Override_WAPI) {
  ScopedDisableDriveAPI disable_drive_api;
  TestRemoteChange_Override();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_Folder) {
  ASSERT_FALSE(IsDriveAPIDisabled());
  TestRemoteChange_Folder();
}

TEST_F(DriveFileSyncServiceFakeTest, RemoteChange_Folder_WAPI) {
  ScopedDisableDriveAPI disable_drive_api;
  TestRemoteChange_Folder();
}

#endif  // !defined(OS_ANDROID)

}  // namespace sync_file_system
