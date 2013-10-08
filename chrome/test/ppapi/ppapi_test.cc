// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/ppapi/ppapi_test.h"

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/test_timeouts.h"
#include "build/build_config.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/infobars/confirm_infobar_delegate.h"
#include "chrome/browser/infobars/infobar.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/dom_operation_notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_paths.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_renderer_host.h"
#include "net/base/net_util.h"
#include "net/base/test_data_directory.h"
#include "ppapi/shared_impl/ppapi_switches.h"
#include "ui/gl/gl_switches.h"

#if defined(OS_WIN) && defined(USE_ASH)
#include "base/win/windows_version.h"
#endif

using content::DomOperationNotificationDetails;
using content::RenderViewHost;

namespace {

// Platform-specific filename relative to the chrome executable.
#if defined(OS_WIN)
const wchar_t library_name[] = L"ppapi_tests.dll";
#elif defined(OS_MACOSX)
const char library_name[] = "ppapi_tests.plugin";
#elif defined(OS_POSIX)
const char library_name[] = "libppapi_tests.so";
#endif

}  // namespace

PPAPITestMessageHandler::PPAPITestMessageHandler() {
}

TestMessageHandler::MessageResponse PPAPITestMessageHandler::HandleMessage(
    const std::string& json) {
 std::string trimmed;
 TrimString(json, "\"", &trimmed);
 if (trimmed == "...") {
   return CONTINUE;
 } else {
   message_ = trimmed;
   return DONE;
 }
}

void PPAPITestMessageHandler::Reset() {
  TestMessageHandler::Reset();
  message_.clear();
}

PPAPITestBase::InfoBarObserver::InfoBarObserver() {
  registrar_.Add(this, chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_ADDED,
                 content::NotificationService::AllSources());
}

PPAPITestBase::InfoBarObserver::~InfoBarObserver() {
  EXPECT_EQ(0u, expected_infobars_.size()) << "Missing an expected infobar";
}

void PPAPITestBase::InfoBarObserver::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  ASSERT_EQ(chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_ADDED, type);
  InfoBarDelegate* infobar =
      content::Details<InfoBarAddedDetails>(details).ptr();
  ConfirmInfoBarDelegate* confirm_infobar_delegate =
      infobar->AsConfirmInfoBarDelegate();
  ASSERT_TRUE(confirm_infobar_delegate);

  ASSERT_FALSE(expected_infobars_.empty()) << "Unexpected infobar";
  if (expected_infobars_.front())
    confirm_infobar_delegate->Accept();
  else
    confirm_infobar_delegate->Cancel();
  expected_infobars_.pop_front();

  // TODO(bauerb): We should close the infobar.
}

void PPAPITestBase::InfoBarObserver::ExpectInfoBarAndAccept(
    bool should_accept) {
  expected_infobars_.push_back(should_accept);
}

PPAPITestBase::PPAPITestBase() {
}

void PPAPITestBase::SetUpCommandLine(CommandLine* command_line) {
  // The test sends us the result via a cookie.
  command_line->AppendSwitch(switches::kEnableFileCookies);

  // Some stuff is hung off of the testing interface which is not enabled
  // by default.
  command_line->AppendSwitch(switches::kEnablePepperTesting);

  // Smooth scrolling confuses the scrollbar test.
  command_line->AppendSwitch(switches::kDisableSmoothScrolling);

  // For TestRequestOSFileHandle.
  command_line->AppendSwitch(switches::kUnlimitedStorage);
  command_line->AppendSwitchASCII(switches::kAllowNaClFileHandleAPI,
                                  "127.0.0.1");
}

void PPAPITestBase::SetUpOnMainThread() {
  // Always allow access to the PPAPI broker.
  browser()->profile()->GetHostContentSettingsMap()->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_PPAPI_BROKER, CONTENT_SETTING_ALLOW);
}

GURL PPAPITestBase::GetTestFileUrl(const std::string& test_case) {
  base::FilePath test_path;
  EXPECT_TRUE(PathService::Get(base::DIR_SOURCE_ROOT, &test_path));
  test_path = test_path.Append(FILE_PATH_LITERAL("ppapi"));
  test_path = test_path.Append(FILE_PATH_LITERAL("tests"));
  test_path = test_path.Append(FILE_PATH_LITERAL("test_case.html"));

  // Sanity check the file name.
  EXPECT_TRUE(base::PathExists(test_path));

  GURL test_url = net::FilePathToFileURL(test_path);

  GURL::Replacements replacements;
  std::string query = BuildQuery(std::string(), test_case);
  replacements.SetQuery(query.c_str(), url_parse::Component(0, query.size()));
  return test_url.ReplaceComponents(replacements);
}

void PPAPITestBase::RunTest(const std::string& test_case) {
  GURL url = GetTestFileUrl(test_case);
  RunTestURL(url);
}

void PPAPITestBase::RunTestAndReload(const std::string& test_case) {
  GURL url = GetTestFileUrl(test_case);
  RunTestURL(url);
  // If that passed, we simply run the test again, which navigates again.
  RunTestURL(url);
}

void PPAPITestBase::RunTestViaHTTP(const std::string& test_case) {
  base::FilePath document_root;
  ASSERT_TRUE(ui_test_utils::GetRelativeBuildDirectory(&document_root));
  base::FilePath http_document_root;
  ASSERT_TRUE(ui_test_utils::GetRelativeBuildDirectory(&http_document_root));
  net::SpawnedTestServer http_server(net::SpawnedTestServer::TYPE_HTTP,
                                     net::SpawnedTestServer::kLocalhost,
                                     document_root);
  ASSERT_TRUE(http_server.Start());
  RunTestURL(GetTestURL(http_server, test_case, std::string()));
}

void PPAPITestBase::RunTestWithSSLServer(const std::string& test_case) {
  base::FilePath http_document_root;
  ASSERT_TRUE(ui_test_utils::GetRelativeBuildDirectory(&http_document_root));
  net::SpawnedTestServer http_server(net::SpawnedTestServer::TYPE_HTTP,
                                     net::SpawnedTestServer::kLocalhost,
                                     http_document_root);
  net::SpawnedTestServer ssl_server(net::SpawnedTestServer::TYPE_HTTPS,
                                    net::BaseTestServer::SSLOptions(),
                                    http_document_root);
  // Start the servers in parallel.
  ASSERT_TRUE(http_server.StartInBackground());
  ASSERT_TRUE(ssl_server.StartInBackground());
  // Wait until they are both finished before continuing.
  ASSERT_TRUE(http_server.BlockUntilStarted());
  ASSERT_TRUE(ssl_server.BlockUntilStarted());

  uint16_t port = ssl_server.host_port_pair().port();
  RunTestURL(GetTestURL(http_server,
                        test_case,
                        base::StringPrintf("ssl_server_port=%d", port)));
}

void PPAPITestBase::RunTestWithWebSocketServer(const std::string& test_case) {
  base::FilePath http_document_root;
  ASSERT_TRUE(ui_test_utils::GetRelativeBuildDirectory(&http_document_root));
  net::SpawnedTestServer http_server(net::SpawnedTestServer::TYPE_HTTP,
                                     net::SpawnedTestServer::kLocalhost,
                                     http_document_root);
  net::SpawnedTestServer ws_server(net::SpawnedTestServer::TYPE_WS,
                                   net::SpawnedTestServer::kLocalhost,
                                   net::GetWebSocketTestDataDirectory());
  // Start the servers in parallel.
  ASSERT_TRUE(http_server.StartInBackground());
  ASSERT_TRUE(ws_server.StartInBackground());
  // Wait until they are both finished before continuing.
  ASSERT_TRUE(http_server.BlockUntilStarted());
  ASSERT_TRUE(ws_server.BlockUntilStarted());

  std::string host = ws_server.host_port_pair().HostForURL();
  uint16_t port = ws_server.host_port_pair().port();
  RunTestURL(GetTestURL(http_server,
                        test_case,
                        base::StringPrintf(
                            "websocket_host=%s&websocket_port=%d",
                            host.c_str(),
                            port)));
}

void PPAPITestBase::RunTestIfAudioOutputAvailable(
    const std::string& test_case) {
  RunTest(test_case);
}

void PPAPITestBase::RunTestViaHTTPIfAudioOutputAvailable(
    const std::string& test_case) {
  RunTestViaHTTP(test_case);
}

std::string PPAPITestBase::StripPrefixes(const std::string& test_name) {
  const char* const prefixes[] = {
      "FAILS_", "FLAKY_", "DISABLED_", "SLOW_" };
  for (size_t i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); ++i)
    if (test_name.find(prefixes[i]) == 0)
      return test_name.substr(strlen(prefixes[i]));
  return test_name;
}

void PPAPITestBase::RunTestURL(const GURL& test_url) {
#if defined(OS_WIN) && defined(USE_ASH)
  // PPAPITests are broken in Ash browser tests (http://crbug.com/263548).
  if (base::win::GetVersion() >= base::win::VERSION_WIN8) {
    LOG(WARNING) << "PPAPITests are disabled for Ash browser tests.";
    return;
  }
#endif

  // See comment above TestingInstance in ppapi/test/testing_instance.h.
  // Basically it sends messages using the DOM automation controller. The
  // value of "..." means it's still working and we should continue to wait,
  // any other value indicates completion (in this case it will start with
  // "PASS" or "FAIL"). This keeps us from timing out on waits for long tests.
  PPAPITestMessageHandler handler;
  JavascriptTestObserver observer(
      browser()->tab_strip_model()->GetActiveWebContents()->GetRenderViewHost(),
      &handler);

  ui_test_utils::NavigateToURL(browser(), test_url);

  ASSERT_TRUE(observer.Run()) << handler.error_message();
  EXPECT_STREQ("PASS", handler.message().c_str());
}

GURL PPAPITestBase::GetTestURL(
    const net::SpawnedTestServer& http_server,
    const std::string& test_case,
    const std::string& extra_params) {
  std::string query = BuildQuery("files/test_case.html?", test_case);
  if (!extra_params.empty())
    query = base::StringPrintf("%s&%s", query.c_str(), extra_params.c_str());

  return http_server.GetURL(query);
}

PPAPITest::PPAPITest() : in_process_(true) {
}

void PPAPITest::SetUpCommandLine(CommandLine* command_line) {
  PPAPITestBase::SetUpCommandLine(command_line);

  // Append the switch to register the pepper plugin.
  // library name = <out dir>/<test_name>.<library_extension>
  // MIME type = application/x-ppapi-<test_name>
  base::FilePath plugin_dir;
  EXPECT_TRUE(PathService::Get(base::DIR_MODULE, &plugin_dir));

  base::FilePath plugin_lib = plugin_dir.Append(library_name);
  EXPECT_TRUE(base::PathExists(plugin_lib));
  base::FilePath::StringType pepper_plugin = plugin_lib.value();
  pepper_plugin.append(FILE_PATH_LITERAL(";application/x-ppapi-tests"));
  command_line->AppendSwitchNative(switches::kRegisterPepperPlugins,
                                   pepper_plugin);
  command_line->AppendSwitchASCII(switches::kAllowNaClSocketAPI, "127.0.0.1");

  if (in_process_)
    command_line->AppendSwitch(switches::kPpapiInProcess);
}

std::string PPAPITest::BuildQuery(const std::string& base,
                                  const std::string& test_case){
  return base::StringPrintf("%stestcase=%s", base.c_str(), test_case.c_str());
}

OutOfProcessPPAPITest::OutOfProcessPPAPITest() {
  in_process_ = false;
}

void OutOfProcessPPAPITest::SetUpCommandLine(CommandLine* command_line) {
  PPAPITest::SetUpCommandLine(command_line);
  command_line->AppendSwitch(switches::kUseFakeDeviceForMediaStream);
  command_line->AppendSwitch(switches::kUseFakeUIForMediaStream);
}

void PPAPINaClTest::SetUpCommandLine(CommandLine* command_line) {
  PPAPITestBase::SetUpCommandLine(command_line);

  base::FilePath plugin_lib;
  EXPECT_TRUE(PathService::Get(chrome::FILE_NACL_PLUGIN, &plugin_lib));
  EXPECT_TRUE(base::PathExists(plugin_lib));

  // Enable running NaCl outside of the store.
  command_line->AppendSwitch(switches::kEnableNaCl);
  command_line->AppendSwitch(switches::kEnablePnacl);
  command_line->AppendSwitchASCII(switches::kAllowNaClSocketAPI, "127.0.0.1");
  command_line->AppendSwitch(switches::kUseFakeDeviceForMediaStream);
  command_line->AppendSwitch(switches::kUseFakeUIForMediaStream);
}

// Append the correct mode and testcase string
std::string PPAPINaClNewlibTest::BuildQuery(const std::string& base,
                                            const std::string& test_case) {
  return base::StringPrintf("%smode=nacl_newlib&testcase=%s", base.c_str(),
                            test_case.c_str());
}

// Append the correct mode and testcase string
std::string PPAPINaClGLibcTest::BuildQuery(const std::string& base,
                                           const std::string& test_case) {
  return base::StringPrintf("%smode=nacl_glibc&testcase=%s", base.c_str(),
                            test_case.c_str());
}

// Append the correct mode and testcase string
std::string PPAPINaClPNaClTest::BuildQuery(const std::string& base,
                                           const std::string& test_case) {
  return base::StringPrintf("%smode=nacl_pnacl&testcase=%s", base.c_str(),
                            test_case.c_str());
}

void PPAPINaClTestDisallowedSockets::SetUpCommandLine(
    CommandLine* command_line) {
  PPAPITestBase::SetUpCommandLine(command_line);

  base::FilePath plugin_lib;
  EXPECT_TRUE(PathService::Get(chrome::FILE_NACL_PLUGIN, &plugin_lib));
  EXPECT_TRUE(base::PathExists(plugin_lib));

  // Enable running NaCl outside of the store.
  command_line->AppendSwitch(switches::kEnableNaCl);
  command_line->AppendSwitch(switches::kEnablePnacl);
}

// Append the correct mode and testcase string
std::string PPAPINaClTestDisallowedSockets::BuildQuery(
    const std::string& base,
    const std::string& test_case) {
  return base::StringPrintf("%smode=nacl_newlib&testcase=%s", base.c_str(),
                            test_case.c_str());
}

void PPAPIBrokerInfoBarTest::SetUpOnMainThread() {
  // The default content setting for the PPAPI broker is ASK. We purposefully
  // don't call PPAPITestBase::SetUpOnMainThread() to keep it that way.
}
