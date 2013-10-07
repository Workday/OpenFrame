// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/values.h"
#include "chrome/browser/browser_process_impl.h"
#include "chrome/browser/extensions/api/api_function.h"
#include "chrome/browser/extensions/api/api_resource_manager.h"
#include "chrome/browser/extensions/api/socket/socket.h"
#include "chrome/browser/extensions/api/socket/socket_api.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/testing_browser_process.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace utils = extension_function_test_utils;

namespace extensions {

BrowserContextKeyedService* ApiResourceManagerTestFactory(
    content::BrowserContext* profile) {
  content::BrowserThread::ID id;
  CHECK(content::BrowserThread::GetCurrentThreadIdentifier(&id));
  return ApiResourceManager<Socket>::CreateApiResourceManagerForTest(
      static_cast<Profile*>(profile), id);
}

class SocketUnitTest : public BrowserWithTestWindowTest {
 public:
  virtual void SetUp() {
    BrowserWithTestWindowTest::SetUp();

    ApiResourceManager<Socket>::GetFactoryInstance()->SetTestingFactoryAndUse(
        browser()->profile(), ApiResourceManagerTestFactory);

    extension_ = utils::CreateEmptyExtensionWithLocation(
        extensions::Manifest::UNPACKED);
  }

  base::Value* RunFunctionWithExtension(
      UIThreadExtensionFunction* function, const std::string& args) {
    scoped_refptr<UIThreadExtensionFunction> delete_function(function);
    function->set_extension(extension_.get());
    return utils::RunFunctionAndReturnSingleResult(function, args, browser());
  }

  base::DictionaryValue* RunFunctionAndReturnDict(
      UIThreadExtensionFunction* function, const std::string& args) {
    base::Value* result = RunFunctionWithExtension(function, args);
    return result ? utils::ToDictionary(result) : NULL;
  }

 protected:
  scoped_refptr<extensions::Extension> extension_;
};

TEST_F(SocketUnitTest, Create) {
  // Get BrowserThread
  content::BrowserThread::ID id;
  CHECK(content::BrowserThread::GetCurrentThreadIdentifier(&id));

  // Create SocketCreateFunction and put it on BrowserThread
  SocketCreateFunction *function = new SocketCreateFunction();
  function->set_work_thread_id(id);

  // Run tests
  scoped_ptr<base::DictionaryValue> result(RunFunctionAndReturnDict(
      function, "[\"tcp\"]"));
  ASSERT_TRUE(result.get());
}

}  // namespace extensions
