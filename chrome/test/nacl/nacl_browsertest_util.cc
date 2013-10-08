// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/nacl/nacl_browsertest_util.h"

#include <stdlib.h>
#include "base/command_line.h"
#include "base/environment.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/webplugininfo.h"
#include "net/base/net_util.h"

typedef TestMessageHandler::MessageResponse MessageResponse;

MessageResponse StructuredMessageHandler::HandleMessage(
    const std::string& json) {
  scoped_ptr<Value> value;
  base::JSONReader reader(base::JSON_ALLOW_TRAILING_COMMAS);
  // Automation messages are stringified before they are sent because the
  // automation channel cannot handle arbitrary objects.  This means we
  // need to decode the json twice to get the original message.
  value.reset(reader.ReadToValue(json));
  if (!value.get())
    return InternalError("Could parse automation JSON: " + json +
                         " because " + reader.GetErrorMessage());

  std::string temp;
  if (!value->GetAsString(&temp))
    return InternalError("Message was not a string: " + json);

  value.reset(reader.ReadToValue(temp));
  if (!value.get())
    return InternalError("Could not parse message JSON: " + temp +
                         " because " + reader.GetErrorMessage());

  DictionaryValue* msg;
  if (!value->GetAsDictionary(&msg))
    return InternalError("Message was not an object: " + temp);

  std::string type;
  if (!msg->GetString("type", &type))
    return MissingField("unknown", "type");

  return HandleStructuredMessage(type, msg);
}

MessageResponse StructuredMessageHandler::MissingField(
    const std::string& type,
    const std::string& field) {
  return InternalError(type + " message did not have field: " + field);
}

MessageResponse StructuredMessageHandler::InternalError(
    const std::string& reason) {
  SetError(reason);
  return DONE;
}

LoadTestMessageHandler::LoadTestMessageHandler()
    : test_passed_(false) {
}

void LoadTestMessageHandler::Log(const std::string& type,
                                 const std::string& message) {
  // TODO(ncbray) better logging.
  LOG(INFO) << type << " " << message;
}

MessageResponse LoadTestMessageHandler::HandleStructuredMessage(
   const std::string& type,
   DictionaryValue* msg) {
  if (type == "Log") {
    std::string message;
    if (!msg->GetString("message", &message))
      return MissingField(type, "message");
    Log("LOG", message);
    return CONTINUE;
  } else if (type == "Shutdown") {
    std::string message;
    if (!msg->GetString("message", &message))
      return MissingField(type, "message");
    if (!msg->GetBoolean("passed", &test_passed_))
      return MissingField(type, "passed");
    Log("SHUTDOWN", message);
    return DONE;
  } else {
    return InternalError("Unknown message type: " + type);
  }
}

// A message handler for nacl_integration tests ported to be browser_tests.
// nacl_integration tests report to their test jig using a series of RPC calls
// that are encoded as URL requests. When these tests run as browser_tests,
// they make the same RPC requests, but use the automation channel instead of
// URL requests. This message handler decodes and responds to these requests.
class NaClIntegrationMessageHandler : public StructuredMessageHandler {
 public:
  NaClIntegrationMessageHandler();

  void Log(const std::string& message);

  virtual MessageResponse HandleStructuredMessage(
      const std::string& type,
      base::DictionaryValue* msg) OVERRIDE;

  bool test_passed() const {
    return test_passed_;
  }

 private:
  bool test_passed_;

  DISALLOW_COPY_AND_ASSIGN(NaClIntegrationMessageHandler);
};

NaClIntegrationMessageHandler::NaClIntegrationMessageHandler()
    : test_passed_(false) {
}

void NaClIntegrationMessageHandler::Log(const std::string& message) {
  // TODO(ncbray) better logging.
  LOG(INFO) << "|||| " << message;
}

MessageResponse NaClIntegrationMessageHandler::HandleStructuredMessage(
    const std::string& type,
    DictionaryValue* msg) {
  if (type == "TestLog") {
    std::string message;
    if (!msg->GetString("message", &message))
      return MissingField(type, "message");
    Log(message);
    return CONTINUE;
  } else if (type == "Shutdown") {
    std::string message;
    if (!msg->GetString("message", &message))
      return MissingField(type, "message");
    if (!msg->GetBoolean("passed", &test_passed_))
      return MissingField(type, "passed");
    Log(message);
    return DONE;
  } else if (type == "Ping") {
    return CONTINUE;
  } else if (type == "JavaScriptIsAlive") {
    return CONTINUE;
  } else {
    return InternalError("Unknown message type: " + type);
  }
}

// NaCl browser tests serve files out of the build directory because nexes and
// pexes are artifacts of the build.  To keep things tidy, all test data is kept
// in a subdirectory.  Several variants of a test may be run, for example when
// linked against newlib and when linked against glibc.  These variants are kept
// in different subdirectories.  For example, the build directory will look
// something like this on Linux:
// out/
//     Release/
//             nacl_test_data/
//                            newlib/
//                            glibc/
static bool GetNaClVariantRoot(const base::FilePath::StringType& variant,
                               base::FilePath* document_root) {
  if (!ui_test_utils::GetRelativeBuildDirectory(document_root))
    return false;
  *document_root = document_root->Append(FILE_PATH_LITERAL("nacl_test_data"));
  *document_root = document_root->Append(variant);
  return true;
}

static void AddPnaclParm(const base::FilePath::StringType& url,
                         base::FilePath::StringType* url_with_parm) {
  if (url.find(FILE_PATH_LITERAL("?")) == base::FilePath::StringType::npos) {
    *url_with_parm = url + FILE_PATH_LITERAL("?pnacl=1");
  } else {
    *url_with_parm = url + FILE_PATH_LITERAL("&pnacl=1");
  }
}

NaClBrowserTestBase::NaClBrowserTestBase() {
}

NaClBrowserTestBase::~NaClBrowserTestBase() {
}

void NaClBrowserTestBase::SetUpCommandLine(CommandLine* command_line) {
  command_line->AppendSwitch(switches::kEnableNaCl);
}

void NaClBrowserTestBase::SetUpInProcessBrowserTestFixture() {
  // Sanity check.
  base::FilePath plugin_lib;
  ASSERT_TRUE(PathService::Get(chrome::FILE_NACL_PLUGIN, &plugin_lib));
  ASSERT_TRUE(base::PathExists(plugin_lib)) << plugin_lib.value();

  ASSERT_TRUE(StartTestServer()) << "Cannot start test server.";
}

bool NaClBrowserTestBase::GetDocumentRoot(base::FilePath* document_root) {
  return GetNaClVariantRoot(Variant(), document_root);
}

bool NaClBrowserTestBase::IsPnacl() {
  return false;
}

GURL NaClBrowserTestBase::TestURL(
    const base::FilePath::StringType& url_fragment) {
  base::FilePath expanded_url = base::FilePath(FILE_PATH_LITERAL("files"));
  expanded_url = expanded_url.Append(url_fragment);
  return test_server_->GetURL(expanded_url.MaybeAsASCII());
}

bool NaClBrowserTestBase::RunJavascriptTest(const GURL& url,
                                            TestMessageHandler* handler) {
  JavascriptTestObserver observer(
      browser()->tab_strip_model()->GetActiveWebContents()->GetRenderViewHost(),
      handler);
  ui_test_utils::NavigateToURL(browser(), url);
  return observer.Run();
}

void NaClBrowserTestBase::RunLoadTest(
    const base::FilePath::StringType& test_file) {
  LoadTestMessageHandler handler;
  base::FilePath::StringType test_file_with_parm = test_file;
  if (IsPnacl()) {
    AddPnaclParm(test_file, &test_file_with_parm);
  }
  bool ok = RunJavascriptTest(TestURL(test_file_with_parm), &handler);
  ASSERT_TRUE(ok) << handler.error_message();
  ASSERT_TRUE(handler.test_passed()) << "Test failed.";
}

void NaClBrowserTestBase::RunNaClIntegrationTest(
    const base::FilePath::StringType& url_fragment) {
  NaClIntegrationMessageHandler handler;
  base::FilePath::StringType url_fragment_with_parm = url_fragment;
  if (IsPnacl()) {
    AddPnaclParm(url_fragment, &url_fragment_with_parm);
  }
  bool ok = RunJavascriptTest(TestURL(url_fragment_with_parm), &handler);
  ASSERT_TRUE(ok) << handler.error_message();
  ASSERT_TRUE(handler.test_passed()) << "Test failed.";
}

bool NaClBrowserTestBase::StartTestServer() {
  // Launch the web server.
  base::FilePath document_root;
  if (!GetDocumentRoot(&document_root))
    return false;
  test_server_.reset(new net::SpawnedTestServer(
                         net::SpawnedTestServer::TYPE_HTTP,
                         net::SpawnedTestServer::kLocalhost,
                         document_root));
  return test_server_->Start();
}

base::FilePath::StringType NaClBrowserTestNewlib::Variant() {
  return FILE_PATH_LITERAL("newlib");
}

base::FilePath::StringType NaClBrowserTestGLibc::Variant() {
  return FILE_PATH_LITERAL("glibc");
}

base::FilePath::StringType NaClBrowserTestPnacl::Variant() {
  return FILE_PATH_LITERAL("pnacl");
}

bool NaClBrowserTestPnacl::IsPnacl() {
  return true;
}

void NaClBrowserTestPnacl::SetUpCommandLine(CommandLine* command_line) {
  NaClBrowserTestBase::SetUpCommandLine(command_line);
  command_line->AppendSwitch(switches::kEnablePnacl);
}

NaClBrowserTestPnaclWithOldCache::NaClBrowserTestPnaclWithOldCache() {
  scoped_ptr<base::Environment> env(base::Environment::Create());
  env->SetVar("PNACL_USE_OLD_CACHE", "true");
}

base::FilePath::StringType NaClBrowserTestStatic::Variant() {
  return FILE_PATH_LITERAL("static");
}

bool NaClBrowserTestStatic::GetDocumentRoot(base::FilePath* document_root) {
  *document_root = base::FilePath(FILE_PATH_LITERAL("chrome/test/data/nacl"));
  return true;
}
