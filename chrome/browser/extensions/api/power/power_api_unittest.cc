// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/power/power_api.h"

#include <deque>
#include <string>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/power/power_api_manager.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/power_save_blocker.h"

namespace utils = extension_function_test_utils;

namespace extensions {

namespace {

// Args commonly passed to PowerSaveBlockerStubManager::CallFunction().
const char kDisplayArgs[] = "[\"display\"]";
const char kSystemArgs[] = "[\"system\"]";
const char kEmptyArgs[] = "[]";

// Different actions that can be performed as a result of a
// PowerSaveBlocker being created or destroyed.
enum Request {
  BLOCK_APP_SUSPENSION,
  UNBLOCK_APP_SUSPENSION,
  BLOCK_DISPLAY_SLEEP,
  UNBLOCK_DISPLAY_SLEEP,
  // Returned by PowerSaveBlockerStubManager::PopFirstRequest() when no
  // requests are present.
  NONE,
};

// Stub implementation of content::PowerSaveBlocker that just runs a
// callback on destruction.
class PowerSaveBlockerStub : public content::PowerSaveBlocker {
 public:
  explicit PowerSaveBlockerStub(base::Closure unblock_callback)
      : unblock_callback_(unblock_callback) {
  }

  virtual ~PowerSaveBlockerStub() {
    unblock_callback_.Run();
  }

 private:
  base::Closure unblock_callback_;

  DISALLOW_COPY_AND_ASSIGN(PowerSaveBlockerStub);
};

// Manages PowerSaveBlockerStub objects.  Tests can instantiate this class
// to make PowerApiManager's calls to create PowerSaveBlockers record the
// actions that would've been performed instead of actually blocking and
// unblocking power management.
class PowerSaveBlockerStubManager {
 public:
  PowerSaveBlockerStubManager() : weak_ptr_factory_(this) {
    // Use base::Unretained since callbacks with return values can't use
    // weak pointers.
    PowerApiManager::GetInstance()->SetCreateBlockerFunctionForTesting(
        base::Bind(&PowerSaveBlockerStubManager::CreateStub,
                   base::Unretained(this)));
  }

  ~PowerSaveBlockerStubManager() {
    PowerApiManager::GetInstance()->SetCreateBlockerFunctionForTesting(
        PowerApiManager::CreateBlockerFunction());
  }

  // Removes and returns the first item from |requests_|.  Returns NONE if
  // |requests_| is empty.
  Request PopFirstRequest() {
    if (requests_.empty())
      return NONE;

    Request request = requests_.front();
    requests_.pop_front();
    return request;
  }

 private:
  // Creates a new PowerSaveBlockerStub of type |type|.
  scoped_ptr<content::PowerSaveBlocker> CreateStub(
      content::PowerSaveBlocker::PowerSaveBlockerType type,
      const std::string& reason) {
    Request unblock_request = NONE;
    switch (type) {
      case content::PowerSaveBlocker::kPowerSaveBlockPreventAppSuspension:
        requests_.push_back(BLOCK_APP_SUSPENSION);
        unblock_request = UNBLOCK_APP_SUSPENSION;
        break;
      case content::PowerSaveBlocker::kPowerSaveBlockPreventDisplaySleep:
        requests_.push_back(BLOCK_DISPLAY_SLEEP);
        unblock_request = UNBLOCK_DISPLAY_SLEEP;
        break;
    }
    return scoped_ptr<content::PowerSaveBlocker>(
        new PowerSaveBlockerStub(
            base::Bind(&PowerSaveBlockerStubManager::AppendRequest,
                       weak_ptr_factory_.GetWeakPtr(),
                       unblock_request)));
  }

  void AppendRequest(Request request) {
    requests_.push_back(request);
  }

  // Requests in chronological order.
  std::deque<Request> requests_;

  base::WeakPtrFactory<PowerSaveBlockerStubManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(PowerSaveBlockerStubManager);
};

}  // namespace

class PowerApiTest : public BrowserWithTestWindowTest {
 public:
  virtual void SetUp() OVERRIDE {
    BrowserWithTestWindowTest::SetUp();
    manager_.reset(new PowerSaveBlockerStubManager);
    extension_ = utils::CreateEmptyExtensionWithLocation(
        extensions::Manifest::UNPACKED);
  }

 protected:
  // Shorthand for PowerRequestKeepAwakeFunction and
  // PowerReleaseKeepAwakeFunction.
  enum FunctionType {
    REQUEST,
    RELEASE,
  };

  // Calls the function described by |type| with |args|, a JSON list of
  // arguments, on behalf of |extension|.
  bool CallFunction(FunctionType type,
                    const std::string& args,
                    extensions::Extension* extension) {
    scoped_refptr<UIThreadExtensionFunction> function(
        type == REQUEST ?
        static_cast<UIThreadExtensionFunction*>(
            new PowerRequestKeepAwakeFunction) :
        static_cast<UIThreadExtensionFunction*>(
            new PowerReleaseKeepAwakeFunction));
    function->set_extension(extension);
    return utils::RunFunction(function.get(), args, browser(), utils::NONE);
  }

  // Send a notification to PowerApiManager saying that |extension| has
  // been unloaded.
  void UnloadExtension(extensions::Extension* extension) {
    UnloadedExtensionInfo details(
        extension, extension_misc::UNLOAD_REASON_UNINSTALL);
    PowerApiManager::GetInstance()->Observe(
        chrome::NOTIFICATION_EXTENSION_UNLOADED,
        content::Source<Profile>(browser()->profile()),
        content::Details<UnloadedExtensionInfo>(&details));
  }

  scoped_ptr<PowerSaveBlockerStubManager> manager_;
  scoped_refptr<extensions::Extension> extension_;
};

TEST_F(PowerApiTest, RequestAndRelease) {
  // Simulate an extension making and releasing a "display" request and a
  // "system" request.
  ASSERT_TRUE(CallFunction(REQUEST, kDisplayArgs, extension_.get()));
  EXPECT_EQ(BLOCK_DISPLAY_SLEEP, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());
  ASSERT_TRUE(CallFunction(RELEASE, kEmptyArgs, extension_.get()));
  EXPECT_EQ(UNBLOCK_DISPLAY_SLEEP, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());

  ASSERT_TRUE(CallFunction(REQUEST, kSystemArgs, extension_.get()));
  EXPECT_EQ(BLOCK_APP_SUSPENSION, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());
  ASSERT_TRUE(CallFunction(RELEASE, kEmptyArgs, extension_.get()));
  EXPECT_EQ(UNBLOCK_APP_SUSPENSION, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());
}

TEST_F(PowerApiTest, RequestWithoutRelease) {
  // Simulate an extension calling requestKeepAwake() without calling
  // releaseKeepAwake().  The override should be automatically removed when
  // the extension is unloaded.
  ASSERT_TRUE(CallFunction(REQUEST, kDisplayArgs, extension_.get()));
  EXPECT_EQ(BLOCK_DISPLAY_SLEEP, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());

  UnloadExtension(extension_.get());
  EXPECT_EQ(UNBLOCK_DISPLAY_SLEEP, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());
}

TEST_F(PowerApiTest, ReleaseWithoutRequest) {
  // Simulate an extension calling releaseKeepAwake() without having
  // calling requestKeepAwake() earlier.  The call should be ignored.
  ASSERT_TRUE(CallFunction(RELEASE, kEmptyArgs, extension_.get()));
  EXPECT_EQ(NONE, manager_->PopFirstRequest());
}

TEST_F(PowerApiTest, UpgradeRequest) {
  // Simulate an extension calling requestKeepAwake("system") and then
  // requestKeepAwake("display").  When the second call is made, a
  // display-sleep-blocking request should be made before the initial
  // app-suspension-blocking request is released.
  ASSERT_TRUE(CallFunction(REQUEST, kSystemArgs, extension_.get()));
  EXPECT_EQ(BLOCK_APP_SUSPENSION, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());

  ASSERT_TRUE(CallFunction(REQUEST, kDisplayArgs, extension_.get()));
  EXPECT_EQ(BLOCK_DISPLAY_SLEEP, manager_->PopFirstRequest());
  EXPECT_EQ(UNBLOCK_APP_SUSPENSION, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());

  ASSERT_TRUE(CallFunction(RELEASE, kEmptyArgs, extension_.get()));
  EXPECT_EQ(UNBLOCK_DISPLAY_SLEEP, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());
}

TEST_F(PowerApiTest, DowngradeRequest) {
  // Simulate an extension calling requestKeepAwake("display") and then
  // requestKeepAwake("system").  When the second call is made, an
  // app-suspension-blocking request should be made before the initial
  // display-sleep-blocking request is released.
  ASSERT_TRUE(CallFunction(REQUEST, kDisplayArgs, extension_.get()));
  EXPECT_EQ(BLOCK_DISPLAY_SLEEP, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());

  ASSERT_TRUE(CallFunction(REQUEST, kSystemArgs, extension_.get()));
  EXPECT_EQ(BLOCK_APP_SUSPENSION, manager_->PopFirstRequest());
  EXPECT_EQ(UNBLOCK_DISPLAY_SLEEP, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());

  ASSERT_TRUE(CallFunction(RELEASE, kEmptyArgs, extension_.get()));
  EXPECT_EQ(UNBLOCK_APP_SUSPENSION, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());
}

TEST_F(PowerApiTest, MultipleExtensions) {
  // Simulate an extension blocking the display from sleeping.
  ASSERT_TRUE(CallFunction(REQUEST, kDisplayArgs, extension_.get()));
  EXPECT_EQ(BLOCK_DISPLAY_SLEEP, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());

  // Create a second extension that blocks system suspend.  No additional
  // PowerSaveBlocker is needed; the blocker from the first extension
  // already covers the behavior requested by the second extension.
  scoped_ptr<base::DictionaryValue> extension_value(
      utils::ParseDictionary("{\"name\": \"Test\", \"version\": \"1.0\"}"));
  scoped_refptr<extensions::Extension> extension2(
      utils::CreateExtension(extensions::Manifest::UNPACKED,
                             extension_value.get(), "second_extension"));
  ASSERT_TRUE(CallFunction(REQUEST, kSystemArgs, extension2.get()));
  EXPECT_EQ(NONE, manager_->PopFirstRequest());

  // When the first extension is unloaded, a new app-suspension blocker
  // should be created before the display-sleep blocker is destroyed.
  UnloadExtension(extension_.get());
  EXPECT_EQ(BLOCK_APP_SUSPENSION, manager_->PopFirstRequest());
  EXPECT_EQ(UNBLOCK_DISPLAY_SLEEP, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());

  // Make the first extension block display-sleep again.
  ASSERT_TRUE(CallFunction(REQUEST, kDisplayArgs, extension_.get()));
  EXPECT_EQ(BLOCK_DISPLAY_SLEEP, manager_->PopFirstRequest());
  EXPECT_EQ(UNBLOCK_APP_SUSPENSION, manager_->PopFirstRequest());
  EXPECT_EQ(NONE, manager_->PopFirstRequest());
}

}  // namespace extensions
