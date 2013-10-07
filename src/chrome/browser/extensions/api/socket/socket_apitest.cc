// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/extensions/api/dns/host_resolver_wrapper.h"
#include "chrome/browser/extensions/api/dns/mock_host_resolver_creator.h"
#include "chrome/browser/extensions/api/socket/socket_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/spawned_test_server/spawned_test_server.h"

using extensions::Extension;

namespace utils = extension_function_test_utils;

namespace {

// TODO(jschuh): Hanging plugin tests. crbug.com/244653
#if defined(OS_WIN) && defined(ARCH_CPU_X86_64)
#define MAYBE(x) DISABLED_##x
#else
#define MAYBE(x) x
#endif

const std::string kHostname = "127.0.0.1";
const int kPort = 8888;

class SocketApiTest : public ExtensionApiTest {
 public:
  SocketApiTest() : resolver_event_(true, false),
                    resolver_creator_(
                        new extensions::MockHostResolverCreator()) {
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    extensions::HostResolverWrapper::GetInstance()->SetHostResolverForTesting(
        resolver_creator_->CreateMockHostResolver());
  }

  virtual void CleanUpOnMainThread() OVERRIDE {
    extensions::HostResolverWrapper::GetInstance()->
        SetHostResolverForTesting(NULL);
    resolver_creator_->DeleteMockHostResolver();
  }

 private:
  base::WaitableEvent resolver_event_;

  // The MockHostResolver asserts that it's used on the same thread on which
  // it's created, which is actually a stronger rule than its real counterpart.
  // But that's fine; it's good practice.
  scoped_refptr<extensions::MockHostResolverCreator> resolver_creator_;
};

#if !defined(DISABLE_NACL)
// TODO(yzshen): Build testing framework for all extensions APIs in Pepper. And
// move these Pepper API tests there.
class SocketPpapiTest : public SocketApiTest {
 public:
  SocketPpapiTest() {
  }
  virtual ~SocketPpapiTest() {
  }

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    SocketApiTest::SetUpCommandLine(command_line);
    // TODO(yzshen): It is better to use switches::kEnablePepperTesting.
    // However, that requires adding a new DEPS entry. Considering that we are
    // going to move the Pepper API tests to a new place, use a string literal
    // for now.
    command_line->AppendSwitch("enable-pepper-testing");

    PathService::Get(chrome::DIR_GEN_TEST_DATA, &app_dir_);
    app_dir_ = app_dir_.AppendASCII(
        "chrome/test/data/extensions/api_test/socket/ppapi/newlib");
  }

 protected:
  void LaunchTestingApp() {
    const Extension* extension = LoadExtension(app_dir_);
    ASSERT_TRUE(extension);

    chrome::AppLaunchParams params(browser()->profile(), extension,
                                   extension_misc::LAUNCH_NONE,
                                   NEW_WINDOW);
    params.command_line = CommandLine::ForCurrentProcess();
    chrome::OpenApplication(params);
  }

 private:
  base::FilePath app_dir_;
};
#endif

}  // namespace

IN_PROC_BROWSER_TEST_F(SocketApiTest, SocketUDPCreateGood) {
  scoped_refptr<extensions::SocketCreateFunction> socket_create_function(
      new extensions::SocketCreateFunction());
  scoped_refptr<Extension> empty_extension(utils::CreateEmptyExtension());

  socket_create_function->set_extension(empty_extension.get());
  socket_create_function->set_has_callback(true);

  scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
      socket_create_function.get(), "[\"udp\"]", browser(), utils::NONE));
  ASSERT_EQ(base::Value::TYPE_DICTIONARY, result->GetType());
  base::DictionaryValue *value =
      static_cast<base::DictionaryValue*>(result.get());
  int socketId = -1;
  EXPECT_TRUE(value->GetInteger("socketId", &socketId));
  EXPECT_TRUE(socketId > 0);
}

IN_PROC_BROWSER_TEST_F(SocketApiTest, SocketTCPCreateGood) {
  scoped_refptr<extensions::SocketCreateFunction> socket_create_function(
      new extensions::SocketCreateFunction());
  scoped_refptr<Extension> empty_extension(utils::CreateEmptyExtension());

  socket_create_function->set_extension(empty_extension.get());
  socket_create_function->set_has_callback(true);

  scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
      socket_create_function.get(), "[\"tcp\"]", browser(), utils::NONE));
  ASSERT_EQ(base::Value::TYPE_DICTIONARY, result->GetType());
  base::DictionaryValue *value =
      static_cast<base::DictionaryValue*>(result.get());
  int socketId = -1;
  EXPECT_TRUE(value->GetInteger("socketId", &socketId));
  ASSERT_TRUE(socketId > 0);
}

IN_PROC_BROWSER_TEST_F(SocketApiTest, GetNetworkList) {
  scoped_refptr<extensions::SocketGetNetworkListFunction> socket_function(
      new extensions::SocketGetNetworkListFunction());
  scoped_refptr<Extension> empty_extension(utils::CreateEmptyExtension());

  socket_function->set_extension(empty_extension.get());
  socket_function->set_has_callback(true);

  scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
      socket_function.get(), "[]", browser(), utils::NONE));
  ASSERT_EQ(base::Value::TYPE_LIST, result->GetType());

  // If we're invoking socket tests, all we can confirm is that we have at
  // least one address, but not what it is.
  base::ListValue *value = static_cast<base::ListValue*>(result.get());
  ASSERT_TRUE(value->GetSize() > 0);
}

IN_PROC_BROWSER_TEST_F(SocketApiTest, SocketUDPExtension) {
  scoped_ptr<net::SpawnedTestServer> test_server(
      new net::SpawnedTestServer(
          net::SpawnedTestServer::TYPE_UDP_ECHO,
          net::SpawnedTestServer::kLocalhost,
          base::FilePath(FILE_PATH_LITERAL("net/data"))));
  EXPECT_TRUE(test_server->Start());

  net::HostPortPair host_port_pair = test_server->host_port_pair();
  int port = host_port_pair.port();
  ASSERT_TRUE(port > 0);

  // Test that sendTo() is properly resolving hostnames.
  host_port_pair.set_host("LOCALhost");

  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());

  ExtensionTestMessageListener listener("info_please", true);

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("socket/api")));
  EXPECT_TRUE(listener.WaitUntilSatisfied());
  listener.Reply(
      base::StringPrintf("udp:%s:%d", host_port_pair.host().c_str(), port));

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(SocketApiTest, SocketTCPExtension) {
  scoped_ptr<net::SpawnedTestServer> test_server(
      new net::SpawnedTestServer(
          net::SpawnedTestServer::TYPE_TCP_ECHO,
          net::SpawnedTestServer::kLocalhost,
          base::FilePath(FILE_PATH_LITERAL("net/data"))));
  EXPECT_TRUE(test_server->Start());

  net::HostPortPair host_port_pair = test_server->host_port_pair();
  int port = host_port_pair.port();
  ASSERT_TRUE(port > 0);

  // Test that connect() is properly resolving hostnames.
  host_port_pair.set_host("lOcAlHoSt");

  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());

  ExtensionTestMessageListener listener("info_please", true);

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("socket/api")));
  EXPECT_TRUE(listener.WaitUntilSatisfied());
  listener.Reply(
      base::StringPrintf("tcp:%s:%d", host_port_pair.host().c_str(), port));

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(SocketApiTest, SocketTCPServerExtension) {
  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());
  ExtensionTestMessageListener listener("info_please", true);
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("socket/api")));
  EXPECT_TRUE(listener.WaitUntilSatisfied());
  listener.Reply(
      base::StringPrintf("tcp_server:%s:%d", kHostname.c_str(), kPort));

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(SocketApiTest, SocketTCPServerUnbindOnUnload) {
  ResultCatcher catcher;
  const Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII("socket/unload"));
  ASSERT_TRUE(extension);
  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();

  UnloadExtension(extension->id());

  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("socket/unload")))
      << message_;
  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(SocketApiTest, SocketMulticast) {
  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());
  ExtensionTestMessageListener listener("info_please", true);
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("socket/api")));
  EXPECT_TRUE(listener.WaitUntilSatisfied());
  listener.Reply(
      base::StringPrintf("multicast:%s:%d", kHostname.c_str(), kPort));

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

#if !defined(DISABLE_NACL)
IN_PROC_BROWSER_TEST_F(SocketPpapiTest, MAYBE(UDP)) {
  scoped_ptr<net::SpawnedTestServer> test_server(
      new net::SpawnedTestServer(
          net::SpawnedTestServer::TYPE_UDP_ECHO,
          net::SpawnedTestServer::kLocalhost,
          base::FilePath(FILE_PATH_LITERAL("net/data"))));
  EXPECT_TRUE(test_server->Start());

  net::HostPortPair host_port_pair = test_server->host_port_pair();
  int port = host_port_pair.port();
  ASSERT_TRUE(port > 0);

  // Test that sendTo() is properly resolving hostnames.
  host_port_pair.set_host("LOCALhost");

  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());

  ExtensionTestMessageListener listener("info_please", true);

  LaunchTestingApp();

  EXPECT_TRUE(listener.WaitUntilSatisfied());
  listener.Reply(
      base::StringPrintf("udp:%s:%d", host_port_pair.host().c_str(), port));

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(SocketPpapiTest, MAYBE(TCP)) {
  scoped_ptr<net::SpawnedTestServer> test_server(
      new net::SpawnedTestServer(
          net::SpawnedTestServer::TYPE_TCP_ECHO,
          net::SpawnedTestServer::kLocalhost,
          base::FilePath(FILE_PATH_LITERAL("net/data"))));
  EXPECT_TRUE(test_server->Start());

  net::HostPortPair host_port_pair = test_server->host_port_pair();
  int port = host_port_pair.port();
  ASSERT_TRUE(port > 0);

  // Test that connect() is properly resolving hostnames.
  host_port_pair.set_host("lOcAlHoSt");

  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());

  ExtensionTestMessageListener listener("info_please", true);

  LaunchTestingApp();

  EXPECT_TRUE(listener.WaitUntilSatisfied());
  listener.Reply(
      base::StringPrintf("tcp:%s:%d", host_port_pair.host().c_str(), port));

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(SocketPpapiTest, MAYBE(TCPServer)) {
  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());
  ExtensionTestMessageListener listener("info_please", true);

  LaunchTestingApp();

  EXPECT_TRUE(listener.WaitUntilSatisfied());
  listener.Reply(
      base::StringPrintf("tcp_server:%s:%d", kHostname.c_str(), kPort));

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}

IN_PROC_BROWSER_TEST_F(SocketPpapiTest, MAYBE(Multicast)) {
  ResultCatcher catcher;
  catcher.RestrictToProfile(browser()->profile());
  ExtensionTestMessageListener listener("info_please", true);

  LaunchTestingApp();

  EXPECT_TRUE(listener.WaitUntilSatisfied());
  listener.Reply(
      base::StringPrintf("multicast:%s:%d", kHostname.c_str(), kPort));

  EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
}
#endif
