// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/at_exit.h"
#include "base/bind.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "mojo/application/public/cpp/application_delegate.h"
#include "mojo/shell/application_loader.h"
#include "mojo/shell/application_manager.h"
#include "mojo/shell/capability_filter_unittest.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace shell {
namespace test {

class ConnectionValidator;

// This class models an application who will use the shell to interact with a
// system service. The shell may limit this application's visibility of the full
// set of interfaces exposed by that service.
class TestApplication : public ApplicationDelegate {
 public:
  TestApplication();
  ~TestApplication() override;

 private:
  // Overridden from ApplicationDelegate:
  void Initialize(ApplicationImpl* app) override;
  bool ConfigureIncomingConnection(ApplicationConnection* connection) override;

  void ConnectionClosed(const std::string& service_url);

  ApplicationImpl* app_;
  ValidatorPtr validator_;
  scoped_ptr<ApplicationConnection> connection1_;
  scoped_ptr<ApplicationConnection> connection2_;

  DISALLOW_COPY_AND_ASSIGN(TestApplication);
};

class TestLoader : public ApplicationLoader {
 public:
  explicit TestLoader(ApplicationDelegate* delegate);
  ~TestLoader() override;

 private:
  // Overridden from ApplicationLoader:
  void Load(const GURL& url, InterfaceRequest<Application> request) override;

  scoped_ptr<ApplicationDelegate> delegate_;
  scoped_ptr<ApplicationImpl> app_;

  DISALLOW_COPY_AND_ASSIGN(TestLoader);
};

class CapabilityFilterTest : public testing::Test {
 public:
   CapabilityFilterTest();
   ~CapabilityFilterTest() override;

 protected:
  template <class T>
  void CreateLoader(const std::string& url) {
    application_manager_->SetLoaderForURL(
        make_scoped_ptr(new TestLoader(new T)), GURL(url));
  }

  void RunBlockingTest();
  void RunWildcardTest();

  // Implement to provide an implementation of PackageManager for the test's
  // ApplicationManager.
  virtual PackageManager* CreatePackageManager() = 0;

  // Overridden from testing::Test:
  void SetUp() override;
  void TearDown() override;

  base::MessageLoop* loop() { return &loop_;  }
  ApplicationManager* application_manager() {
    return application_manager_.get();
  }
  ConnectionValidator* validator() { return validator_; }

 private:
  void RunApplication(const std::string& url, const CapabilityFilter& filter);
  void InitValidator(const std::set<std::string>& expectations);
  void RunTest();

  template<class T>
  scoped_ptr<ApplicationDelegate> CreateApplicationDelegate() {
    return scoped_ptr<ApplicationDelegate>(new T);
  }

  base::ShadowingAtExitManager at_exit_;
  base::MessageLoop loop_;
  scoped_ptr<ApplicationManager> application_manager_;
  ConnectionValidator* validator_;

  DISALLOW_COPY_AND_ASSIGN(CapabilityFilterTest);
};

}  // namespace test
}  // namespace shell
}  // namespace mojo
