// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/threading/platform_thread.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/webrtc_ip_handling_policy.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "content/test/webrtc_content_browsertest_base.h"
#include "media/audio/audio_manager.h"
#include "media/base/media_switches.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace content {

namespace {
const char kPeerConnectionHtml[] = "/media/peerconnection-call.html";
}  // namespace

// Disable these test cases for Android since in some bots, there exists only
// the loopback interface.
#if defined(OS_ANDROID)
#define MAYBE_WebRtcBrowserIPPermissionGrantedTest \
    DISABLED_WebRtcBrowserIPPermissionGrantedTest
#define MAYBE_WebRtcBrowserIPPermissionDeniedTest \
    DISABLED_WebRtcBrowserIPPermissionDeniedTest
#define MAYBE_WebRtcBrowserIPPolicyPublicAndPrivateInterfacesTest \
    DISABLED_WebRtcBrowserIPPolicyPublicAndPrivateInterfacesTest
#define MAYBE_WebRtcBrowserIPPolicyPublicInterfaceOnlyTest \
    DISABLED_WebRtcBrowserIPPolicyPublicInterfaceOnlyTest
#define MAYBE_WebRtcBrowserIPPolicyDisableUdpTest \
    DISABLED_WebRtcBrowserIPPolicyDisableUdpTest
#else
#define MAYBE_WebRtcBrowserIPPermissionGrantedTest \
    WebRtcBrowserIPPermissionGrantedTest
#define MAYBE_WebRtcBrowserIPPermissionDeniedTest \
    WebRtcBrowserIPPermissionDeniedTest
#define MAYBE_WebRtcBrowserIPPolicyPublicAndPrivateInterfacesTest \
    WebRtcBrowserIPPolicyPublicAndPrivateInterfacesTest
#define MAYBE_WebRtcBrowserIPPolicyPublicInterfaceOnlyTest \
    WebRtcBrowserIPPolicyPublicInterfaceOnlyTest
#define MAYBE_WebRtcBrowserIPPolicyDisableUdpTest \
    WebRtcBrowserIPPolicyDisableUdpTest
#endif

// This class tests the scenario when permission to access mic or camera is
// denied.
class MAYBE_WebRtcBrowserIPPermissionGrantedTest
    : public WebRtcContentBrowserTest {
 public:
  MAYBE_WebRtcBrowserIPPermissionGrantedTest() {}
  ~MAYBE_WebRtcBrowserIPPermissionGrantedTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WebRtcContentBrowserTest::SetUpCommandLine(command_line);
    AppendUseFakeUIForMediaStreamFlag();
    command_line->AppendSwitchASCII(switches::kForceWebRtcIPHandlingPolicy,
                                    kWebRTCIPHandlingDefault);
  }
};

// Loopback interface is the non-default private interface. Test that when
// device permission is granted, we should have loopback candidates.
IN_PROC_BROWSER_TEST_F(MAYBE_WebRtcBrowserIPPermissionGrantedTest,
                       GatherLocalCandidates) {
  // Disable this test on XP, crbug.com/542416.
  if (OnWinXp())
    return;
  MakeTypicalCall("callWithDevicePermissionGranted();", kPeerConnectionHtml);
}

// This class tests the scenario when permission to access mic or camera is
// denied.
class MAYBE_WebRtcBrowserIPPermissionDeniedTest
    : public WebRtcContentBrowserTest {
 public:
  MAYBE_WebRtcBrowserIPPermissionDeniedTest() {}
  ~MAYBE_WebRtcBrowserIPPermissionDeniedTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WebRtcContentBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kForceWebRtcIPHandlingPolicy,
                                    kWebRTCIPHandlingDefault);
  }
};

// Test that when device permission is denied, only non-default interfaces are
// gathered even if the policy is "default".
IN_PROC_BROWSER_TEST_F(MAYBE_WebRtcBrowserIPPermissionDeniedTest,
                       GatherLocalCandidates) {
  // Disable this test on XP, crbug.com/542416.
  if (OnWinXp())
    return;
  MakeTypicalCall("callAndExpectNonLoopbackCandidates();", kPeerConnectionHtml);
}

// This class tests the scenario when ip handling policy is set to "public and
// private interfaces", the non-default private candidate is not gathered.
class MAYBE_WebRtcBrowserIPPolicyPublicAndPrivateInterfacesTest
    : public WebRtcContentBrowserTest {
 public:
  MAYBE_WebRtcBrowserIPPolicyPublicAndPrivateInterfacesTest() {}
  ~MAYBE_WebRtcBrowserIPPolicyPublicAndPrivateInterfacesTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WebRtcContentBrowserTest::SetUpCommandLine(command_line);
    AppendUseFakeUIForMediaStreamFlag();
    command_line->AppendSwitchASCII(
        switches::kForceWebRtcIPHandlingPolicy,
        kWebRTCIPHandlingDefaultPublicAndPrivateInterfaces);
  }
};

IN_PROC_BROWSER_TEST_F(
    MAYBE_WebRtcBrowserIPPolicyPublicAndPrivateInterfacesTest,
    GatherLocalCandidates) {
  // Disable this test on XP, crbug.com/542416.
  if (OnWinXp())
    return;
  MakeTypicalCall("callAndExpectNonLoopbackCandidates();", kPeerConnectionHtml);
}

// This class tests the scenario when ip handling policy is set to "public
// interface only", there is no candidate gathered as there is no stun server
// specified.
class MAYBE_WebRtcBrowserIPPolicyPublicInterfaceOnlyTest
    : public WebRtcContentBrowserTest {
 public:
  MAYBE_WebRtcBrowserIPPolicyPublicInterfaceOnlyTest() {}
  ~MAYBE_WebRtcBrowserIPPolicyPublicInterfaceOnlyTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WebRtcContentBrowserTest::SetUpCommandLine(command_line);
    AppendUseFakeUIForMediaStreamFlag();
    command_line->AppendSwitchASCII(
        switches::kForceWebRtcIPHandlingPolicy,
        kWebRTCIPHandlingDefaultPublicInterfaceOnly);
  }
};

IN_PROC_BROWSER_TEST_F(MAYBE_WebRtcBrowserIPPolicyPublicInterfaceOnlyTest,
                       GatherLocalCandidates) {
  // Disable this test on XP, crbug.com/542416.
  if (OnWinXp())
    return;
  MakeTypicalCall("callWithNoCandidateExpected();", kPeerConnectionHtml);
}

// This class tests the scenario when ip handling policy is set to "disable
// non-proxied udp", there is no candidate gathered as there is no stun server
// specified.
class MAYBE_WebRtcBrowserIPPolicyDisableUdpTest
    : public WebRtcContentBrowserTest {
 public:
  MAYBE_WebRtcBrowserIPPolicyDisableUdpTest() {}
  ~MAYBE_WebRtcBrowserIPPolicyDisableUdpTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    WebRtcContentBrowserTest::SetUpCommandLine(command_line);
    AppendUseFakeUIForMediaStreamFlag();
    command_line->AppendSwitchASCII(switches::kForceWebRtcIPHandlingPolicy,
                                    kWebRTCIPHandlingDisableNonProxiedUdp);
  }
};

IN_PROC_BROWSER_TEST_F(MAYBE_WebRtcBrowserIPPolicyDisableUdpTest,
                       GatherLocalCandidates) {
  // Disable this test on XP, crbug.com/542416.
  if (OnWinXp())
    return;
  MakeTypicalCall("callWithNoCandidateExpected();", kPeerConnectionHtml);
}

}  // namespace content
