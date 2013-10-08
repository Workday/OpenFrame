// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DRIVE_TEST_UTIL_H_
#define CHROME_BROWSER_CHROMEOS_DRIVE_TEST_UTIL_H_

#include <string>
#include <vector>

#include "chrome/browser/chromeos/drive/file_cache.h"
#include "chrome/browser/google_apis/test_util.h"
#include "net/base/completion_callback.h"
#include "net/base/io_buffer.h"
#include "net/base/network_change_notifier.h"
#include "net/base/test_completion_callback.h"

class PrefRegistrySimple;

namespace net {
class IOBuffer;
}  // namespace net

namespace drive {

namespace test_util {

// Disk space size used by FakeFreeDiskSpaceGetter.
const int64 kLotsOfSpace = internal::kMinFreeSpace * 10;

// Runs a task posted to the blocking pool, including subsequent tasks posted
// to the UI message loop and the blocking pool.
//
// A task is often posted to the blocking pool with PostTaskAndReply(). In
// that case, a task is posted back to the UI message loop, which can again
// post a task to the blocking pool. This function processes these tasks
// repeatedly.
void RunBlockingPoolTask();

// Test data type of file cache
struct TestCacheResource {
  TestCacheResource(const std::string& source_file,
                    const std::string& resource_id,
                    const std::string& md5,
                    bool is_pinned,
                    bool is_dirty);
  ~TestCacheResource(){}

  std::string source_file;
  std::string resource_id;
  std::string md5;
  bool is_pinned;
  bool is_dirty;
};

// Obtains default test data for FileCacheEntry.
std::vector<TestCacheResource> GetDefaultTestCacheResources();

// Helper to destroy objects which needs Destroy() to be called on destruction.
// Note: When using this helper, you should destruct objects before
// BrowserThread.
struct DestroyHelperForTests {
  template<typename T>
  void operator()(T* object) const {
    if (object) {
      object->Destroy();
      test_util::RunBlockingPoolTask();  // Finish destruction.
    }
  }
};

// Reads all the data from |reader| and copies to |content|. Returns net::Error
// code.
template<typename Reader>
int ReadAllData(Reader* reader, std::string* content) {
  const int kBufferSize = 10;
  scoped_refptr<net::IOBuffer> buffer(new net::IOBuffer(kBufferSize));
  while (true) {
    net::TestCompletionCallback callback;
    int result = reader->Read(buffer.get(), kBufferSize, callback.callback());
    result = callback.GetResult(result);
    if (result <= 0) {
      // Found an error or EOF. Return it. Note: net::OK is 0.
      return result;
    }
    content->append(buffer->data(), result);
  }
}

// Adds test cache |resources| to |cache|. Returns whether the operation
// succeeded or not.
bool PrepareTestCacheResources(
    internal::FileCache* cache,
    const std::vector<TestCacheResource>& resources);

// Registers Drive related preferences in |pref_registry|. Drive related
// preferences should be registered as TestingPrefServiceSimple will crash if
// unregistered preference is referenced.
void RegisterDrivePrefs(PrefRegistrySimple* pref_registry);

// Fake NetworkChangeNotifier implementation.
class FakeNetworkChangeNotifier : public net::NetworkChangeNotifier {
 public:
  FakeNetworkChangeNotifier();

  void SetConnectionType(ConnectionType type);

  // NetworkChangeNotifier override.
  virtual ConnectionType GetCurrentConnectionType() const OVERRIDE;

 private:
  net::NetworkChangeNotifier::ConnectionType type_;
};

}  // namespace test_util
}  // namespace drive

#endif  // CHROME_BROWSER_CHROMEOS_DRIVE_TEST_UTIL_H_
