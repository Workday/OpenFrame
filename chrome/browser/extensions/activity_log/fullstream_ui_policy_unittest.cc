// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/cancelable_callback.h"
#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/simple_test_clock.h"
#include "chrome/browser/extensions/activity_log/activity_log.h"
#include "chrome/browser/extensions/activity_log/fullstream_ui_policy.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_builder.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#endif

namespace extensions {

class FullStreamUIPolicyTest : public testing::Test {
 public:
  FullStreamUIPolicyTest()
      : thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP),
        saved_cmdline_(CommandLine::NO_PROGRAM) {
#if defined OS_CHROMEOS
    test_user_manager_.reset(new chromeos::ScopedTestUserManager());
#endif
    CommandLine command_line(CommandLine::NO_PROGRAM);
    saved_cmdline_ = *CommandLine::ForCurrentProcess();
    profile_.reset(new TestingProfile());
    CommandLine::ForCurrentProcess()->AppendSwitch(
        switches::kEnableExtensionActivityLogging);
    CommandLine::ForCurrentProcess()->AppendSwitch(
        switches::kEnableExtensionActivityLogTesting);
    extension_service_ = static_cast<TestExtensionSystem*>(
        ExtensionSystem::Get(profile_.get()))->CreateExtensionService
            (&command_line, base::FilePath(), false);
  }

  virtual ~FullStreamUIPolicyTest() {
#if defined OS_CHROMEOS
    test_user_manager_.reset();
#endif
    base::RunLoop().RunUntilIdle();
    profile_.reset(NULL);
    base::RunLoop().RunUntilIdle();
    // Restore the original command line and undo the affects of SetUp().
    *CommandLine::ForCurrentProcess() = saved_cmdline_;
  }

  // A helper function to call ReadData on a policy object and wait for the
  // results to be processed.
  void CheckReadData(
      ActivityLogPolicy* policy,
      const std::string& extension_id,
      const int day,
      const base::Callback<void(scoped_ptr<Action::ActionVector>)>& checker) {
    // Submit a request to the policy to read back some data, and call the
    // checker function when results are available.  This will happen on the
    // database thread.
    policy->ReadData(
        extension_id,
        day,
        base::Bind(&FullStreamUIPolicyTest::CheckWrapper,
                   checker,
                   base::MessageLoop::current()->QuitClosure()));

    // Set up a timeout that will trigger after 5 seconds; if we haven't
    // received any results by then assume that the test is broken.
    base::CancelableClosure timeout(
        base::Bind(&FullStreamUIPolicyTest::TimeoutCallback));
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE, timeout.callback(), base::TimeDelta::FromSeconds(5));

    // Wait for results; either the checker or the timeout callbacks should
    // cause the main loop to exit.
    base::MessageLoop::current()->Run();

    timeout.Cancel();
  }

  static void CheckWrapper(
      const base::Callback<void(scoped_ptr<Action::ActionVector>)>& checker,
      const base::Closure& done,
      scoped_ptr<Action::ActionVector> results) {
    checker.Run(results.Pass());
    done.Run();
  }

  static void TimeoutCallback() {
    base::MessageLoop::current()->QuitWhenIdle();
    FAIL() << "Policy test timed out waiting for results";
  }

  static void RetrieveActions_LogAndFetchActions(
      scoped_ptr<std::vector<scoped_refptr<Action> > > i) {
    ASSERT_EQ(2, static_cast<int>(i->size()));
  }

  static void Arguments_Present(scoped_ptr<Action::ActionVector> i) {
    scoped_refptr<Action> last = i->front();
    std::string args =
        "ID=odlameecjipmbmbejkplpemijjgpljce CATEGORY=api_call "
        "API=extension.connect ARGS=[\"hello\",\"world\"]";
    ASSERT_EQ(args, last->PrintForDebug());
  }

  static void Arguments_GetTodaysActions(
      scoped_ptr<Action::ActionVector> actions) {
    std::string api_print =
        "ID=punky CATEGORY=api_call API=brewster ARGS=[\"woof\"]";
    std::string dom_print =
        "ID=punky CATEGORY=dom_access API=lets ARGS=[\"vamoose\"] "
        "PAGE_URL=http://www.google.com/";
    ASSERT_EQ(2, static_cast<int>(actions->size()));
    ASSERT_EQ(dom_print, actions->at(0)->PrintForDebug());
    ASSERT_EQ(api_print, actions->at(1)->PrintForDebug());
  }

  static void Arguments_GetOlderActions(
      scoped_ptr<Action::ActionVector> actions) {
    std::string api_print =
        "ID=punky CATEGORY=api_call API=brewster ARGS=[\"woof\"]";
    std::string dom_print =
        "ID=punky CATEGORY=dom_access API=lets ARGS=[\"vamoose\"] "
        "PAGE_URL=http://www.google.com/";
    ASSERT_EQ(2, static_cast<int>(actions->size()));
    ASSERT_EQ(dom_print, actions->at(0)->PrintForDebug());
    ASSERT_EQ(api_print, actions->at(1)->PrintForDebug());
  }

 protected:
  ExtensionService* extension_service_;
  scoped_ptr<TestingProfile> profile_;
  content::TestBrowserThreadBundle thread_bundle_;
  // Used to preserve a copy of the original command line.
  // The test framework will do this itself as well. However, by then,
  // it is too late to call ActivityLog::RecomputeLoggingIsEnabled() in
  // TearDown().
  CommandLine saved_cmdline_;

#if defined OS_CHROMEOS
  chromeos::ScopedTestDeviceSettingsService test_device_settings_service_;
  chromeos::ScopedTestCrosSettings test_cros_settings_;
  scoped_ptr<chromeos::ScopedTestUserManager> test_user_manager_;
#endif
};

TEST_F(FullStreamUIPolicyTest, Construct) {
  ActivityLogPolicy* policy = new FullStreamUIPolicy(profile_.get());
  scoped_refptr<const Extension> extension =
      ExtensionBuilder()
          .SetManifest(DictionaryBuilder()
                       .Set("name", "Test extension")
                       .Set("version", "1.0.0")
                       .Set("manifest_version", 2))
          .Build();
  extension_service_->AddExtension(extension.get());
  scoped_ptr<base::ListValue> args(new base::ListValue());
  scoped_refptr<Action> action = new Action(extension->id(),
                                            base::Time::Now(),
                                            Action::ACTION_API_CALL,
                                            "tabs.testMethod");
  action->set_args(args.Pass());
  policy->ProcessAction(action);
  policy->Close();
}

TEST_F(FullStreamUIPolicyTest, LogAndFetchActions) {
  ActivityLogPolicy* policy = new FullStreamUIPolicy(profile_.get());
  scoped_refptr<const Extension> extension =
      ExtensionBuilder()
          .SetManifest(DictionaryBuilder()
                       .Set("name", "Test extension")
                       .Set("version", "1.0.0")
                       .Set("manifest_version", 2))
          .Build();
  extension_service_->AddExtension(extension.get());
  GURL gurl("http://www.google.com");

  // Write some API calls
  scoped_refptr<Action> action_api = new Action(extension->id(),
                                                base::Time::Now(),
                                                Action::ACTION_API_CALL,
                                                "tabs.testMethod");
  action_api->set_args(make_scoped_ptr(new base::ListValue()));
  policy->ProcessAction(action_api);

  scoped_refptr<Action> action_dom = new Action(extension->id(),
                                                base::Time::Now(),
                                                Action::ACTION_DOM_ACCESS,
                                                "document.write");
  action_dom->set_args(make_scoped_ptr(new base::ListValue()));
  action_dom->set_page_url(gurl);
  policy->ProcessAction(action_dom);

  CheckReadData(
      policy,
      extension->id(),
      0,
      base::Bind(&FullStreamUIPolicyTest::RetrieveActions_LogAndFetchActions));

  policy->Close();
}

TEST_F(FullStreamUIPolicyTest, LogWithArguments) {
  ActivityLogPolicy* policy = new FullStreamUIPolicy(profile_.get());
  scoped_refptr<const Extension> extension =
      ExtensionBuilder()
          .SetManifest(DictionaryBuilder()
                       .Set("name", "Test extension")
                       .Set("version", "1.0.0")
                       .Set("manifest_version", 2))
          .Build();
  extension_service_->AddExtension(extension.get());

  scoped_ptr<base::ListValue> args(new base::ListValue());
  args->Set(0, new base::StringValue("hello"));
  args->Set(1, new base::StringValue("world"));
  scoped_refptr<Action> action = new Action(extension->id(),
                                            base::Time::Now(),
                                            Action::ACTION_API_CALL,
                                            "extension.connect");
  action->set_args(args.Pass());

  policy->ProcessAction(action);
  CheckReadData(policy,
                extension->id(),
                0,
                base::Bind(&FullStreamUIPolicyTest::Arguments_Present));
  policy->Close();
}

TEST_F(FullStreamUIPolicyTest, GetTodaysActions) {
  ActivityLogPolicy* policy = new FullStreamUIPolicy(profile_.get());

  // Use a mock clock to ensure that events are not recorded on the wrong day
  // when the test is run close to local midnight.  Note: Ownership is passed
  // to the policy, but we still keep a pointer locally.  The policy will take
  // care of destruction; this is safe since the policy outlives all our
  // accesses to the mock clock.
  base::SimpleTestClock* mock_clock = new base::SimpleTestClock();
  mock_clock->SetNow(base::Time::Now().LocalMidnight() +
                     base::TimeDelta::FromHours(12));
  policy->SetClockForTesting(scoped_ptr<base::Clock>(mock_clock));

  // Record some actions
  scoped_refptr<Action> action =
      new Action("punky",
                 mock_clock->Now() - base::TimeDelta::FromMinutes(40),
                 Action::ACTION_API_CALL,
                 "brewster");
  action->mutable_args()->AppendString("woof");
  policy->ProcessAction(action);

  action =
      new Action("punky", mock_clock->Now(), Action::ACTION_DOM_ACCESS, "lets");
  action->mutable_args()->AppendString("vamoose");
  action->set_page_url(GURL("http://www.google.com"));
  policy->ProcessAction(action);

  action = new Action(
      "scoobydoo", mock_clock->Now(), Action::ACTION_DOM_ACCESS, "lets");
  action->mutable_args()->AppendString("vamoose");
  action->set_page_url(GURL("http://www.google.com"));
  policy->ProcessAction(action);

  CheckReadData(
      policy,
      "punky",
      0,
      base::Bind(&FullStreamUIPolicyTest::Arguments_GetTodaysActions));
  policy->Close();
}

// Check that we can read back less recent actions in the db.
TEST_F(FullStreamUIPolicyTest, GetOlderActions) {
  ActivityLogPolicy* policy = new FullStreamUIPolicy(profile_.get());

  // Use a mock clock to ensure that events are not recorded on the wrong day
  // when the test is run close to local midnight.
  base::SimpleTestClock* mock_clock = new base::SimpleTestClock();
  mock_clock->SetNow(base::Time::Now().LocalMidnight() +
                     base::TimeDelta::FromHours(12));
  policy->SetClockForTesting(scoped_ptr<base::Clock>(mock_clock));

  // Record some actions
  scoped_refptr<Action> action =
      new Action("punky",
                 mock_clock->Now() - base::TimeDelta::FromDays(3) -
                     base::TimeDelta::FromMinutes(40),
                 Action::ACTION_API_CALL,
                 "brewster");
  action->mutable_args()->AppendString("woof");
  policy->ProcessAction(action);

  action = new Action("punky",
                      mock_clock->Now() - base::TimeDelta::FromDays(3),
                      Action::ACTION_DOM_ACCESS,
                      "lets");
  action->mutable_args()->AppendString("vamoose");
  action->set_page_url(GURL("http://www.google.com"));
  policy->ProcessAction(action);

  action = new Action("punky",
                      mock_clock->Now(),
                      Action::ACTION_DOM_ACCESS,
                      "lets");
  action->mutable_args()->AppendString("too new");
  action->set_page_url(GURL("http://www.google.com"));
  policy->ProcessAction(action);

  action = new Action("punky",
                      mock_clock->Now() - base::TimeDelta::FromDays(7),
                      Action::ACTION_DOM_ACCESS,
                      "lets");
  action->mutable_args()->AppendString("too old");
  action->set_page_url(GURL("http://www.google.com"));
  policy->ProcessAction(action);

  CheckReadData(
      policy,
      "punky",
      3,
      base::Bind(&FullStreamUIPolicyTest::Arguments_GetOlderActions));
  policy->Close();
}

}  // namespace extensions
