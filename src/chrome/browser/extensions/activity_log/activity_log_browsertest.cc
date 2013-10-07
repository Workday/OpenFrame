// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/stringprintf.h"
#include "chrome/browser/extensions/activity_log/activity_log.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/prerender/prerender_handle.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace extensions {

// Only the prerender tests are in this file. To add tests for activity
// logging please see:
//    chrome/test/data/extensions/api_test/activity_log_private/README

class ActivityLogPrerenderTest : public ExtensionApiTest {
 protected:
  // Make sure the activity log is turned on.
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    ExtensionBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kEnableExtensionActivityLogging);
    command_line->AppendSwitch(switches::kEnableExtensionActivityLogTesting);
    command_line->AppendSwitchASCII(switches::kPrerenderMode,
                                    switches::kPrerenderModeSwitchValueEnabled);
  }

  static void Prerender_Arguments(
      const std::string& extension_id,
      int port,
      scoped_ptr<std::vector<scoped_refptr<Action> > > i) {
    // This is to exit RunLoop (base::MessageLoop::current()->Run()) below
    base::MessageLoop::current()->PostTask(
        FROM_HERE, base::MessageLoop::QuitClosure());

    ASSERT_TRUE(i->size());
    scoped_refptr<Action> last = i->front();

    std::string args = base::StringPrintf(
        "ID=%s CATEGORY=content_script API= ARGS=[\"/google_cs.js\"] "
        "PAGE_URL=http://www.google.com.bo:%d/test.html "
        "PAGE_TITLE=\"www.google.com.bo:%d/test.html\" "
        "OTHER={\"prerender\":true}",
        extension_id.c_str(), port, port);
    // TODO: Replace PrintForDebug with field testing
    // when this feature will be available
    ASSERT_EQ(args, last->PrintForDebug());
  }
};

IN_PROC_BROWSER_TEST_F(ActivityLogPrerenderTest, TestScriptInjected) {
  host_resolver()->AddRule("*", "127.0.0.1");
  StartEmbeddedTestServer();
  int port = embedded_test_server()->port();

  // Get the extension (chrome/test/data/extensions/activity_log)
  const Extension* ext =
      LoadExtension(test_data_dir_.AppendASCII("activity_log"));
  ASSERT_TRUE(ext);

  ActivityLog* activity_log = ActivityLog::GetInstance(profile());
  ASSERT_TRUE(activity_log);

  //Disable rate limiting in PrerenderManager
  prerender::PrerenderManager* prerender_manager =
      prerender::PrerenderManagerFactory::GetForProfile(profile());
  ASSERT_TRUE(prerender_manager);
  prerender_manager->mutable_config().rate_limit_enabled = false;
  // Increase maximum size of prerenderer, otherwise this test fails
  // on Windows XP.
  prerender_manager->mutable_config().max_bytes = 1000 * 1024 * 1024;

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();;
  ASSERT_TRUE(web_contents);

  content::WindowedNotificationObserver page_observer(
      content::NOTIFICATION_LOAD_STOP,
      content::NotificationService::AllSources());

  GURL url(base::StringPrintf(
      "http://www.google.com.bo:%d/test.html",
      port));

  const gfx::Size kSize(640, 480);
  scoped_ptr<prerender::PrerenderHandle> prerender_handle(
      prerender_manager->AddPrerenderFromLocalPredictor(
          url,
          web_contents->GetController().GetDefaultSessionStorageNamespace(),
          kSize));

  page_observer.Wait();

  activity_log->GetActions(
      ext->id(), 0, base::Bind(
          ActivityLogPrerenderTest::Prerender_Arguments, ext->id(), port));

  // Allow invocation of Prerender_Arguments
  base::MessageLoop::current()->Run();
}

}  // namespace extensions
