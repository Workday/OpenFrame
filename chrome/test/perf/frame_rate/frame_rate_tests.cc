// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/test_timeouts.h"
#include "base/test/trace_event_analyzer.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/automation/automation_proxy.h"
#include "chrome/test/automation/tab_proxy.h"
#include "chrome/test/perf/perf_test.h"
#include "chrome/test/ui/javascript_test_util.h"
#include "chrome/test/ui/ui_perf_test.h"
#include "net/base/net_util.h"
#include "testing/perf/perf_test.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_switches.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif

namespace {

enum FrameRateTestFlags {
  kUseGpu             = 1 << 0, // Only execute test if --enable-gpu, and verify
                                // that test ran on GPU. This is required for
                                // tests that run on GPU.
  kForceGpuComposited = 1 << 1, // Force the test to use the compositor.
  kDisableVsync       = 1 << 2, // Do not limit framerate to vertical refresh.
                                // when on GPU, nor to 60hz when not on GPU.
  kUseReferenceBuild  = 1 << 3, // Run test using the reference chrome build.
  kInternal           = 1 << 4, // Test uses internal test data.
  kHasRedirect        = 1 << 5, // Test page contains an HTML redirect.
  kIsGpuCanvasTest    = 1 << 6  // Test uses GPU accelerated canvas features.
};

class FrameRateTest
  : public UIPerfTest
  , public ::testing::WithParamInterface<int> {
 public:
  FrameRateTest() {
    show_window_ = true;
    dom_automation_enabled_ = true;
  }

  bool HasFlag(FrameRateTestFlags flag) const {
    return (GetParam() & flag) == flag;
  }

  bool IsGpuAvailable() const {
    return CommandLine::ForCurrentProcess()->HasSwitch("enable-gpu");
  }

  std::string GetSuffixForTestFlags() {
    std::string suffix;
    if (HasFlag(kForceGpuComposited))
      suffix += "_comp";
    if (HasFlag(kUseGpu))
      suffix += "_gpu";
    if (HasFlag(kDisableVsync))
      suffix += "_novsync";
    if (HasFlag(kUseReferenceBuild))
      suffix += "_ref";
    return suffix;
  }

  virtual base::FilePath GetDataPath(const std::string& name) {
    // Make sure the test data is checked out.
    base::FilePath test_path;
    PathService::Get(chrome::DIR_TEST_DATA, &test_path);
    test_path = test_path.Append(FILE_PATH_LITERAL("perf"));
    test_path = test_path.Append(FILE_PATH_LITERAL("frame_rate"));
    if (HasFlag(kInternal)) {
      test_path = test_path.Append(FILE_PATH_LITERAL("private"));
    } else {
      test_path = test_path.Append(FILE_PATH_LITERAL("content"));
    }
    test_path = test_path.AppendASCII(name);
    return test_path;
  }

  virtual void SetUp() {
    if (HasFlag(kUseReferenceBuild))
      UseReferenceBuild();

    // Turn on chrome.Interval to get higher-resolution timestamps on frames.
    launch_arguments_.AppendSwitch(switches::kEnableBenchmarking);

    // Some of the tests may launch http requests through JSON or AJAX
    // which causes a security error (cross domain request) when the page
    // is loaded from the local file system ( file:// ). The following switch
    // fixes that error.
    launch_arguments_.AppendSwitch(switches::kAllowFileAccessFromFiles);

    if (!HasFlag(kUseGpu)) {
      launch_arguments_.AppendSwitch(switches::kDisableAcceleratedCompositing);
      launch_arguments_.AppendSwitch(switches::kDisableExperimentalWebGL);
      launch_arguments_.AppendSwitch(switches::kDisableAccelerated2dCanvas);
    }

    if (HasFlag(kDisableVsync))
      launch_arguments_.AppendSwitch(switches::kDisableGpuVsync);

    UIPerfTest::SetUp();
  }

  bool DidRunOnGpu(const std::string& json_events) {
    using trace_analyzer::Query;
    using trace_analyzer::TraceAnalyzer;

    // Check trace for GPU accleration.
    scoped_ptr<TraceAnalyzer> analyzer(TraceAnalyzer::Create(json_events));

    gfx::GLImplementation gl_impl = gfx::kGLImplementationNone;
    const trace_analyzer::TraceEvent* gpu_event = analyzer->FindFirstOf(
        Query::EventNameIs("SwapBuffers") &&
        Query::EventHasNumberArg("GLImpl"));
    if (gpu_event)
      gl_impl = static_cast<gfx::GLImplementation>(
          gpu_event->GetKnownArgAsInt("GLImpl"));
    return (gl_impl == gfx::kGLImplementationDesktopGL ||
            gl_impl == gfx::kGLImplementationEGLGLES2);
  }

  void RunTest(const std::string& name) {
#if defined(USE_AURA)
    if (!HasFlag(kUseGpu)) {
      printf("Test skipped, Aura always runs with GPU\n");
      return;
    }
#endif
#if defined(OS_WIN)
    if (HasFlag(kUseGpu) && HasFlag(kIsGpuCanvasTest) &&
        base::win::OSInfo::GetInstance()->version() == base::win::VERSION_XP) {
      // crbug.com/128208
      LOG(WARNING) << "Test skipped: GPU canvas tests do not run on XP.";
      return;
    }
#endif

    if (HasFlag(kUseGpu) && !IsGpuAvailable()) {
      printf("Test skipped: requires gpu. Pass --enable-gpu on the command "
             "line if use of GPU is desired.\n");
      return;
    }

    // Verify flag combinations.
    ASSERT_TRUE(HasFlag(kUseGpu) || !HasFlag(kForceGpuComposited));
    ASSERT_TRUE(!HasFlag(kUseGpu) || IsGpuAvailable());

    base::FilePath test_path = GetDataPath(name);
    ASSERT_TRUE(base::DirectoryExists(test_path))
        << "Missing test directory: " << test_path.value();

    test_path = test_path.Append(FILE_PATH_LITERAL("test.html"));

    scoped_refptr<TabProxy> tab(GetActiveTab());
    ASSERT_TRUE(tab.get());

    // TODO(jbates): remove this check when ref builds are updated.
    if (!HasFlag(kUseReferenceBuild))
      ASSERT_TRUE(automation()->BeginTracing("test_gpu"));

    if (HasFlag(kHasRedirect)) {
      // If the test file is known to contain an html redirect, we must block
      // until the second navigation is complete and reacquire the active tab
      // in order to avoid a race condition.
      // If the following assertion is triggered due to a timeout, it is
      // possible that the current test does not re-direct and therefore should
      // not have the kHasRedirect flag turned on.
      ASSERT_EQ(AUTOMATION_MSG_NAVIGATION_SUCCESS,
        tab->NavigateToURLBlockUntilNavigationsComplete(
        net::FilePathToFileURL(test_path), 2));
      tab = GetActiveTab();
      ASSERT_TRUE(tab.get());
    } else {
      ASSERT_EQ(AUTOMATION_MSG_NAVIGATION_SUCCESS,
        tab->NavigateToURL(net::FilePathToFileURL(test_path)));
    }

    // Block until initialization completes
    // If the following assertion fails intermittently, it could be due to a
    // race condition caused by an html redirect. If that is the case, verify
    // that flag kHasRedirect is enabled for the current test.
    ASSERT_TRUE(WaitUntilJavaScriptCondition(
        tab.get(),
        std::wstring(),
        L"window.domAutomationController.send(__initialized);",
        TestTimeouts::large_test_timeout()));

    if (HasFlag(kForceGpuComposited)) {
      ASSERT_TRUE(tab->NavigateToURLAsync(
        GURL("javascript:__make_body_composited();")));
    }

    // Start the tests.
    ASSERT_TRUE(tab->NavigateToURLAsync(GURL("javascript:__start_all();")));

    // Block until the tests completes.
    ASSERT_TRUE(WaitUntilJavaScriptCondition(
        tab.get(),
        std::wstring(),
        L"window.domAutomationController.send(!__running_all);",
        TestTimeouts::large_test_timeout()));

    // TODO(jbates): remove this check when ref builds are updated.
    if (!HasFlag(kUseReferenceBuild)) {
      std::string json_events;
      ASSERT_TRUE(automation()->EndTracing(&json_events));

      bool did_run_on_gpu = DidRunOnGpu(json_events);
      bool expect_gpu = HasFlag(kUseGpu);
      EXPECT_EQ(expect_gpu, did_run_on_gpu);
    }

    // Read out the results.
    std::wstring json;
    ASSERT_TRUE(tab->ExecuteAndExtractString(
        std::wstring(),
        L"window.domAutomationController.send("
        L"JSON.stringify(__calc_results_total()));",
        &json));

    std::map<std::string, std::string> results;
    ASSERT_TRUE(JsonDictionaryToMap(base::WideToUTF8(json), &results));

    ASSERT_TRUE(results.find("mean") != results.end());
    ASSERT_TRUE(results.find("sigma") != results.end());
    ASSERT_TRUE(results.find("gestures") != results.end());
    ASSERT_TRUE(results.find("means") != results.end());
    ASSERT_TRUE(results.find("sigmas") != results.end());

    std::string trace_name = "interval" + GetSuffixForTestFlags();
    printf("GESTURES %s: %s= [%s] [%s] [%s]\n", name.c_str(),
                                                trace_name.c_str(),
                                                results["gestures"].c_str(),
                                                results["means"].c_str(),
                                                results["sigmas"].c_str());

    std::string mean_and_error = results["mean"] + "," + results["sigma"];
    perf_test::PrintResultMeanAndError(name,
                                       std::string(),
                                       trace_name,
                                       mean_and_error,
                                       "milliseconds-per-frame",
                                       true);

    // Navigate back to NTP so that we can quit without timing out during the
    // wait-for-idle stage in test framework.
    EXPECT_EQ(tab->GoBack(), AUTOMATION_MSG_NAVIGATION_SUCCESS);
  }
};

// Must use a different class name to avoid test instantiation conflicts
// with FrameRateTest. An alias is good enough. The alias names must match
// the pattern FrameRate*Test* for them to get picked up by the test bots.
typedef FrameRateTest FrameRateCompositingTest;

// Tests that trigger compositing with a -webkit-translateZ(0)
#define FRAME_RATE_TEST_WITH_AND_WITHOUT_ACCELERATED_COMPOSITING(content) \
TEST_P(FrameRateCompositingTest, content) { \
  RunTest(#content); \
}

INSTANTIATE_TEST_CASE_P(, FrameRateCompositingTest, ::testing::Values(
                        0,
                        kUseGpu | kForceGpuComposited,
                        kUseReferenceBuild,
                        kUseReferenceBuild | kUseGpu | kForceGpuComposited));

FRAME_RATE_TEST_WITH_AND_WITHOUT_ACCELERATED_COMPOSITING(blank);
FRAME_RATE_TEST_WITH_AND_WITHOUT_ACCELERATED_COMPOSITING(googleblog);

typedef FrameRateTest FrameRateNoVsyncCanvasInternalTest;

// Tests for animated 2D canvas content with and without disabling vsync
#define INTERNAL_FRAME_RATE_TEST_CANVAS_WITH_AND_WITHOUT_NOVSYNC(content) \
TEST_P(FrameRateNoVsyncCanvasInternalTest, content) { \
  RunTest(#content); \
}

INSTANTIATE_TEST_CASE_P(, FrameRateNoVsyncCanvasInternalTest, ::testing::Values(
    kInternal | kHasRedirect,
    kIsGpuCanvasTest | kInternal | kHasRedirect | kUseGpu,
    kIsGpuCanvasTest | kInternal | kHasRedirect | kUseGpu | kDisableVsync,
    kUseReferenceBuild | kInternal | kHasRedirect,
    kIsGpuCanvasTest | kUseReferenceBuild | kInternal | kHasRedirect | kUseGpu,
    kIsGpuCanvasTest | kUseReferenceBuild | kInternal | kHasRedirect | kUseGpu |
        kDisableVsync));

INTERNAL_FRAME_RATE_TEST_CANVAS_WITH_AND_WITHOUT_NOVSYNC(fishbowl)

typedef FrameRateTest FrameRateGpuCanvasInternalTest;

// Tests for animated 2D canvas content to be tested only with GPU
// acceleration.
// tests are run with and without Vsync
#define INTERNAL_FRAME_RATE_TEST_CANVAS_GPU(content) \
TEST_P(FrameRateGpuCanvasInternalTest, content) { \
  RunTest(#content); \
}

INSTANTIATE_TEST_CASE_P(, FrameRateGpuCanvasInternalTest, ::testing::Values(
    kIsGpuCanvasTest | kInternal | kHasRedirect | kUseGpu,
    kIsGpuCanvasTest | kInternal | kHasRedirect | kUseGpu | kDisableVsync,
    kIsGpuCanvasTest | kUseReferenceBuild | kInternal | kHasRedirect | kUseGpu,
    kIsGpuCanvasTest | kUseReferenceBuild | kInternal | kHasRedirect | kUseGpu |
        kDisableVsync));

INTERNAL_FRAME_RATE_TEST_CANVAS_GPU(fireflies)
INTERNAL_FRAME_RATE_TEST_CANVAS_GPU(FishIE)
INTERNAL_FRAME_RATE_TEST_CANVAS_GPU(speedreading)

}  // namespace
