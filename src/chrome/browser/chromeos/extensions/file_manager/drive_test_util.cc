// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/file_manager/drive_test_util.h"

#include "base/files/file_path.h"
#include "base/run_loop.h"
#include "chrome/browser/chromeos/drive/drive_integration_service.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "webkit/browser/fileapi/external_mount_points.h"

namespace file_manager {
namespace test_util {

namespace {

// Helper class used to wait for |OnFileSystemMounted| event from a drive file
// system.
class DriveMountPointWaiter : public drive::DriveIntegrationServiceObserver {
 public:
  explicit DriveMountPointWaiter(
      drive::DriveIntegrationService* integration_service)
      : integration_service_(integration_service) {
    integration_service_->AddObserver(this);
  }

  virtual ~DriveMountPointWaiter() {
    integration_service_->RemoveObserver(this);
  }

  // DriveIntegrationServiceObserver override.
  virtual void OnFileSystemMounted() OVERRIDE {
    // Note that it is OK for |run_loop_.Quit| to be called before
    // |run_loop_.Run|. In this case |Run| will return immediately.
    run_loop_.Quit();
  }

  // Runs loop until the file system is mounted.
  void Wait() {
    run_loop_.Run();
  }

 private:
  drive::DriveIntegrationService* integration_service_;
  base::RunLoop run_loop_;
};

}  // namespace

void WaitUntilDriveMountPointIsAdded(Profile* profile) {
  DCHECK(profile);

  // Drive mount point is added by the browser when the drive system service
  // is first initialized. It is done asynchronously after some other parts of
  // the service are initialized (e.g. resource metadata and cache), thus racy
  // with the test start. To handle this raciness, the test verifies that
  // drive mount point is added before continuing. If this is not the case,
  // drive file system is observed for FileSystemMounted event (by
  // |mount_point_waiter|) and test continues once the event is encountered.
  drive::DriveIntegrationService* integration_service =
      drive::DriveIntegrationServiceFactory::FindForProfileRegardlessOfStates(
          profile);
  DCHECK(integration_service);

  const std::string drive_mount_point_name =
      drive::util::GetDriveMountPointPath().BaseName().AsUTF8Unsafe();
  base::FilePath ignored;
  // GetRegisteredPath succeeds iff the mount point exists.
  if (content::BrowserContext::GetMountPoints(profile)->
      GetRegisteredPath(drive_mount_point_name, &ignored))
    return;

  DriveMountPointWaiter mount_point_waiter(integration_service);
  LOG(INFO) << "Waiting for drive mount point to get mounted.";
  mount_point_waiter.Wait();
  LOG(INFO) << "Drive mount point found.";
}

}  // namespace test_util
}  // namespace file_manager
