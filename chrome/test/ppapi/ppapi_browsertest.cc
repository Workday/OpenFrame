// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/ppapi/ppapi_test.h"

#include "base/test/test_timeouts.h"
#include "build/build_config.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/javascript_test_observer.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/test_renderer_host.h"

using content::RenderViewHost;

// This macro finesses macro expansion to do what we want.
#define STRIP_PREFIXES(test_name) StripPrefixes(#test_name)
// Turn the given token into a string. This allows us to use precompiler stuff
// to turn names into DISABLED_Foo, but still pass a string to RunTest.
#define STRINGIFY(test_name) #test_name
#define LIST_TEST(test_name) STRINGIFY(test_name) ","

// Use these macros to run the tests for a specific interface.
// Most interfaces should be tested with both macros.
#define TEST_PPAPI_IN_PROCESS(test_name) \
    IN_PROC_BROWSER_TEST_F(PPAPITest, test_name) { \
      RunTest(STRIP_PREFIXES(test_name)); \
    }
#define TEST_PPAPI_OUT_OF_PROCESS(test_name) \
    IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, test_name) { \
      RunTest(STRIP_PREFIXES(test_name)); \
    }

// Similar macros that test over HTTP.
#define TEST_PPAPI_IN_PROCESS_VIA_HTTP(test_name) \
    IN_PROC_BROWSER_TEST_F(PPAPITest, test_name) { \
      RunTestViaHTTP(STRIP_PREFIXES(test_name)); \
    }
#define TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(test_name) \
    IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, test_name) { \
      RunTestViaHTTP(STRIP_PREFIXES(test_name)); \
    }

// Similar macros that test with an SSL server.
#define TEST_PPAPI_IN_PROCESS_WITH_SSL_SERVER(test_name) \
    IN_PROC_BROWSER_TEST_F(PPAPITest, test_name) { \
      RunTestWithSSLServer(STRIP_PREFIXES(test_name)); \
    }
#define TEST_PPAPI_OUT_OF_PROCESS_WITH_SSL_SERVER(test_name) \
    IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, test_name) { \
      RunTestWithSSLServer(STRIP_PREFIXES(test_name)); \
    }

#if defined(DISABLE_NACL)
#define TEST_PPAPI_NACL(test_name)
#define TEST_PPAPI_NACL_DISALLOWED_SOCKETS(test_name)
#define TEST_PPAPI_NACL_WITH_SSL_SERVER(test_name)

#elif defined(ARCH_CPU_ARM_FAMILY)
// NaCl glibc tests are not included in ARM as there is no glibc support
// on ARM today.
#define TEST_PPAPI_NACL(test_name) \
    IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, test_name) { \
      RunTestViaHTTP(STRIP_PREFIXES(test_name)); \
    } \
    IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, test_name) { \
      RunTestViaHTTP(STRIP_PREFIXES(test_name)); \
    }

#define TEST_PPAPI_NACL_DISALLOWED_SOCKETS(test_name) \
    IN_PROC_BROWSER_TEST_F(PPAPINaClTestDisallowedSockets, test_name) { \
      RunTestViaHTTP(STRIP_PREFIXES(test_name)); \
    }

#define TEST_PPAPI_NACL_WITH_SSL_SERVER(test_name) \
    IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, test_name) { \
      RunTestWithSSLServer(STRIP_PREFIXES(test_name)); \
    } \
    IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, test_name) { \
      RunTestWithSSLServer(STRIP_PREFIXES(test_name)); \
    }

#else

// NaCl based PPAPI tests
#define TEST_PPAPI_NACL(test_name) \
    IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, test_name) { \
      RunTestViaHTTP(STRIP_PREFIXES(test_name)); \
    } \
    IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, test_name) { \
      RunTestViaHTTP(STRIP_PREFIXES(test_name)); \
    } \
    IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, test_name) { \
      RunTestViaHTTP(STRIP_PREFIXES(test_name)); \
    }

// NaCl based PPAPI tests with disallowed socket API
#define TEST_PPAPI_NACL_DISALLOWED_SOCKETS(test_name) \
    IN_PROC_BROWSER_TEST_F(PPAPINaClTestDisallowedSockets, test_name) { \
      RunTestViaHTTP(STRIP_PREFIXES(test_name)); \
    }

// NaCl based PPAPI tests with SSL server
#define TEST_PPAPI_NACL_WITH_SSL_SERVER(test_name) \
    IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, test_name) { \
      RunTestWithSSLServer(STRIP_PREFIXES(test_name)); \
    } \
    IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, test_name) { \
      RunTestWithSSLServer(STRIP_PREFIXES(test_name)); \
    } \
    IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, test_name) { \
      RunTestWithSSLServer(STRIP_PREFIXES(test_name)); \
    }

#endif


// NaCl glibc tests are not included in ARM as there is no glibc support
// on ARM today.
#if defined(ARCH_CPU_ARM_FAMILY)
#define MAYBE_GLIBC(test_name) DISABLED_##test_name
#else
#define MAYBE_GLIBC(test_name) test_name
#endif


//
// Interface tests.
//

// Disable tests under ASAN.  http://crbug.com/104832.
// This is a bit heavy handed, but the majority of these tests fail under ASAN.
// See bug for history.
#if !defined(ADDRESS_SANITIZER)

TEST_PPAPI_IN_PROCESS(Broker)
// Flaky, http://crbug.com/111355
TEST_PPAPI_OUT_OF_PROCESS(DISABLED_Broker)

IN_PROC_BROWSER_TEST_F(PPAPIBrokerInfoBarTest, Accept) {
  // Accepting the infobar should grant permission to access the PPAPI broker.
  InfoBarObserver observer;
  observer.ExpectInfoBarAndAccept(true);

  // PPB_Broker_Trusted::IsAllowed should return false before the infobar is
  // popped and true after the infobar is popped.
  RunTest("Broker_IsAllowedPermissionDenied");
  RunTest("Broker_ConnectPermissionGranted");
  RunTest("Broker_IsAllowedPermissionGranted");

  // It should also set a content settings exception for the site.
  GURL url = GetTestFileUrl("Broker_ConnectPermissionGranted");
  HostContentSettingsMap* content_settings =
      browser()->profile()->GetHostContentSettingsMap();
  EXPECT_EQ(CONTENT_SETTING_ALLOW,
            content_settings->GetContentSetting(
                url, url, CONTENT_SETTINGS_TYPE_PPAPI_BROKER, std::string()));
}

IN_PROC_BROWSER_TEST_F(PPAPIBrokerInfoBarTest, Deny) {
  // Canceling the infobar should deny permission to access the PPAPI broker.
  InfoBarObserver observer;
  observer.ExpectInfoBarAndAccept(false);

  // PPB_Broker_Trusted::IsAllowed should return false before and after the
  // infobar is popped.
  RunTest("Broker_IsAllowedPermissionDenied");
  RunTest("Broker_ConnectPermissionDenied");
  RunTest("Broker_IsAllowedPermissionDenied");

  // It should also set a content settings exception for the site.
  GURL url = GetTestFileUrl("Broker_ConnectPermissionDenied");
  HostContentSettingsMap* content_settings =
      browser()->profile()->GetHostContentSettingsMap();
  EXPECT_EQ(CONTENT_SETTING_BLOCK,
            content_settings->GetContentSetting(
                url, url, CONTENT_SETTINGS_TYPE_PPAPI_BROKER, std::string()));
}

IN_PROC_BROWSER_TEST_F(PPAPIBrokerInfoBarTest, Blocked) {
  // Block access to the PPAPI broker.
  browser()->profile()->GetHostContentSettingsMap()->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_PPAPI_BROKER, CONTENT_SETTING_BLOCK);

  // We shouldn't see an infobar.
  InfoBarObserver observer;

  RunTest("Broker_ConnectPermissionDenied");
  RunTest("Broker_IsAllowedPermissionDenied");
}

IN_PROC_BROWSER_TEST_F(PPAPIBrokerInfoBarTest, Allowed) {
  // Always allow access to the PPAPI broker.
  browser()->profile()->GetHostContentSettingsMap()->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_PPAPI_BROKER, CONTENT_SETTING_ALLOW);

  // We shouldn't see an infobar.
  InfoBarObserver observer;

  RunTest("Broker_ConnectPermissionGranted");
  RunTest("Broker_IsAllowedPermissionGranted");
}

TEST_PPAPI_IN_PROCESS(Console)
TEST_PPAPI_OUT_OF_PROCESS(Console)
TEST_PPAPI_NACL(Console)

TEST_PPAPI_IN_PROCESS(Core)
TEST_PPAPI_OUT_OF_PROCESS(Core)
TEST_PPAPI_NACL(Core)

TEST_PPAPI_IN_PROCESS(TraceEvent)
TEST_PPAPI_OUT_OF_PROCESS(TraceEvent)
TEST_PPAPI_NACL(TraceEvent)

TEST_PPAPI_IN_PROCESS(InputEvent)
TEST_PPAPI_OUT_OF_PROCESS(InputEvent)
TEST_PPAPI_NACL(InputEvent)

// Flaky on Linux and Windows. http://crbug.com/135403
#if defined(OS_LINUX) || defined(OS_WIN)
#define MAYBE_ImeInputEvent DISABLED_ImeInputEvent
#else
#define MAYBE_ImeInputEvent ImeInputEvent
#endif

TEST_PPAPI_IN_PROCESS(MAYBE_ImeInputEvent)
TEST_PPAPI_OUT_OF_PROCESS(MAYBE_ImeInputEvent)
TEST_PPAPI_NACL(MAYBE_ImeInputEvent)

// "Instance" tests are really InstancePrivate tests. InstancePrivate is not
// supported in NaCl, so these tests are only run trusted.
// Also note that these tests are run separately on purpose (versus collapsed
// in to one IN_PROC_BROWSER_TEST_F macro), because some of them have leaks
// on purpose that will look like failures to tests that are run later.
TEST_PPAPI_IN_PROCESS(Instance_ExecuteScript);
TEST_PPAPI_OUT_OF_PROCESS(Instance_ExecuteScript)

// We run and reload the RecursiveObjects test to ensure that the InstanceObject
// (and others) are properly cleaned up after the first run.
IN_PROC_BROWSER_TEST_F(PPAPITest, Instance_RecursiveObjects) {
  RunTestAndReload("Instance_RecursiveObjects");
}
// TODO(dmichael): Make it work out-of-process (or at least see whether we
//                 care).
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest,
                       DISABLED_Instance_RecursiveObjects) {
  RunTestAndReload("Instance_RecursiveObjects");
}
TEST_PPAPI_IN_PROCESS(Instance_LeakedObjectDestructors);
TEST_PPAPI_OUT_OF_PROCESS(Instance_LeakedObjectDestructors);

IN_PROC_BROWSER_TEST_F(PPAPITest,
                       Instance_ExecuteScriptAtInstanceShutdown) {
  // In other tests, we use one call to RunTest so that the tests can all run
  // in one plugin instance. This saves time on loading the plugin (especially
  // for NaCl). Here, we actually want to destroy the Instance, to test whether
  // the destructor can run ExecuteScript successfully. That's why we have two
  // separate calls to RunTest; the second one forces a navigation which
  // destroys the instance from the prior RunTest.
  // See test_instance_deprecated.cc for more information.
  RunTest("Instance_SetupExecuteScriptAtInstanceShutdown");
  RunTest("Instance_ExecuteScriptAtInstanceShutdown");
}
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest,
                       Instance_ExecuteScriptAtInstanceShutdown) {
  // (See the comment for the in-process version of this test above)
  RunTest("Instance_SetupExecuteScriptAtInstanceShutdown");
  RunTest("Instance_ExecuteScriptAtInstanceShutdown");
}

TEST_PPAPI_IN_PROCESS(Graphics2D)
TEST_PPAPI_OUT_OF_PROCESS(Graphics2D)
// Graphics2D_Dev isn't supported in NaCl, only test the other interfaces
// TODO(jhorwich) Enable when Graphics2D_Dev interfaces are proxied in NaCl.
TEST_PPAPI_NACL(Graphics2D_InvalidResource)
TEST_PPAPI_NACL(Graphics2D_InvalidSize)
TEST_PPAPI_NACL(Graphics2D_Humongous)
TEST_PPAPI_NACL(Graphics2D_InitToZero)
TEST_PPAPI_NACL(Graphics2D_Describe)
TEST_PPAPI_NACL(Graphics2D_Paint)
TEST_PPAPI_NACL(Graphics2D_Scroll)
TEST_PPAPI_NACL(Graphics2D_Replace)
TEST_PPAPI_NACL(Graphics2D_Flush)
TEST_PPAPI_NACL(Graphics2D_FlushOffscreenUpdate)
TEST_PPAPI_NACL(Graphics2D_BindNull)

#if defined(OS_WIN) && !defined(USE_AURA)
// These tests fail with the test compositor which is what's used by default for
// browser tests on Windows Aura. Renable when the software compositor is
// available.
// In-process and NaCl tests are having flaky failures on Win: crbug.com/242252
TEST_PPAPI_IN_PROCESS(DISABLED_Graphics3D)
TEST_PPAPI_OUT_OF_PROCESS(Graphics3D)
TEST_PPAPI_NACL(DISABLED_Graphics3D)
#endif

TEST_PPAPI_IN_PROCESS(ImageData)
TEST_PPAPI_OUT_OF_PROCESS(ImageData)
TEST_PPAPI_NACL(ImageData)

TEST_PPAPI_IN_PROCESS(BrowserFont)
TEST_PPAPI_OUT_OF_PROCESS(BrowserFont)

TEST_PPAPI_IN_PROCESS(Buffer)
TEST_PPAPI_OUT_OF_PROCESS(Buffer)

// TCPSocket tests.
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, TCPSocket) {
  RunTestViaHTTP(
      LIST_TEST(TCPSocket_Connect)
      LIST_TEST(TCPSocket_ReadWrite)
      LIST_TEST(TCPSocket_SetOption)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, TCPSocket) {
  RunTestViaHTTP(
      LIST_TEST(TCPSocket_Connect)
      LIST_TEST(TCPSocket_ReadWrite)
      LIST_TEST(TCPSocket_SetOption)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(TCPSocket)) {
  RunTestViaHTTP(
      LIST_TEST(TCPSocket_Connect)
      LIST_TEST(TCPSocket_ReadWrite)
      LIST_TEST(TCPSocket_SetOption)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, TCPSocket) {
  RunTestViaHTTP(
      LIST_TEST(TCPSocket_Connect)
      LIST_TEST(TCPSocket_ReadWrite)
      LIST_TEST(TCPSocket_SetOption)
  );
}

TEST_PPAPI_OUT_OF_PROCESS_WITH_SSL_SERVER(TCPSocketPrivate)
TEST_PPAPI_IN_PROCESS_WITH_SSL_SERVER(TCPSocketPrivate)
TEST_PPAPI_NACL_WITH_SSL_SERVER(TCPSocketPrivate)

TEST_PPAPI_OUT_OF_PROCESS_WITH_SSL_SERVER(TCPSocketPrivateTrusted)
TEST_PPAPI_IN_PROCESS_WITH_SSL_SERVER(TCPSocketPrivateTrusted)

// UDPSocket tests.
// UDPSocket_Broadcast is disabled for OSX because it requires root permissions
// on OSX 10.7+.
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, UDPSocket) {
  RunTestViaHTTP(
      LIST_TEST(UDPSocket_ReadWrite)
      LIST_TEST(UDPSocket_SetOption)
#if !defined(OS_MACOSX)
      LIST_TEST(UDPSocket_Broadcast)
#endif
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, UDPSocket) {
  RunTestViaHTTP(
      LIST_TEST(UDPSocket_ReadWrite)
      LIST_TEST(UDPSocket_SetOption)
#if !defined(OS_MACOSX)
      LIST_TEST(UDPSocket_Broadcast)
#endif
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(UDPSocket)) {
  RunTestViaHTTP(
      LIST_TEST(UDPSocket_ReadWrite)
      LIST_TEST(UDPSocket_SetOption)
#if !defined(OS_MACOSX)
      LIST_TEST(UDPSocket_Broadcast)
#endif
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, UDPSocket) {
  RunTestViaHTTP(
      LIST_TEST(UDPSocket_ReadWrite)
      LIST_TEST(UDPSocket_SetOption)
#if !defined(OS_MACOSX)
      LIST_TEST(UDPSocket_Broadcast)
#endif
  );
}

// UDPSocketPrivate tests.
// UDPSocketPrivate_Broadcast is disabled for OSX because it requires
// root permissions on OSX 10.7+.
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(UDPSocketPrivate_Connect)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(UDPSocketPrivate_ConnectFailure)
#if !defined(OS_MACOSX)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(UDPSocketPrivate_Broadcast)
#endif  // !defined(OS_MACOSX)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(UDPSocketPrivate_SetSocketFeatureErrors)
TEST_PPAPI_NACL(UDPSocketPrivate_Connect)
TEST_PPAPI_NACL(UDPSocketPrivate_ConnectFailure)
#if !defined(OS_MACOSX)
TEST_PPAPI_NACL(UDPSocketPrivate_Broadcast)
#endif  // !defined(OS_MACOSX)
TEST_PPAPI_NACL(UDPSocketPrivate_SetSocketFeatureErrors)

TEST_PPAPI_NACL_DISALLOWED_SOCKETS(HostResolverPrivateDisallowed)
TEST_PPAPI_NACL_DISALLOWED_SOCKETS(TCPServerSocketPrivateDisallowed)
TEST_PPAPI_NACL_DISALLOWED_SOCKETS(TCPSocketPrivateDisallowed)
TEST_PPAPI_NACL_DISALLOWED_SOCKETS(UDPSocketPrivateDisallowed)

TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(TCPServerSocketPrivate)
TEST_PPAPI_NACL(TCPServerSocketPrivate)

// HostResolver tests.
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, HostResolver) {
  RunTestViaHTTP(
      LIST_TEST(HostResolver_Empty)
      LIST_TEST(HostResolver_Resolve)
      LIST_TEST(HostResolver_ResolveIPv4)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, HostResolver) {
  RunTestViaHTTP(
      LIST_TEST(HostResolver_Empty)
      LIST_TEST(HostResolver_Resolve)
      LIST_TEST(HostResolver_ResolveIPv4)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(HostResolver)) {
  RunTestViaHTTP(
      LIST_TEST(HostResolver_Empty)
      LIST_TEST(HostResolver_Resolve)
      LIST_TEST(HostResolver_ResolveIPv4)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, HostResolver) {
  RunTestViaHTTP(
      LIST_TEST(HostResolver_Empty)
      LIST_TEST(HostResolver_Resolve)
      LIST_TEST(HostResolver_ResolveIPv4)
  );
}

TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(HostResolverPrivate_Resolve)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(HostResolverPrivate_ResolveIPv4)
TEST_PPAPI_NACL(HostResolverPrivate_Resolve)
TEST_PPAPI_NACL(HostResolverPrivate_ResolveIPv4)

// URLLoader tests.
IN_PROC_BROWSER_TEST_F(PPAPITest, URLLoader) {
  RunTestViaHTTP(
      LIST_TEST(URLLoader_BasicGET)
      LIST_TEST(URLLoader_BasicPOST)
      LIST_TEST(URLLoader_BasicFilePOST)
      LIST_TEST(URLLoader_BasicFileRangePOST)
      LIST_TEST(URLLoader_CompoundBodyPOST)
      LIST_TEST(URLLoader_EmptyDataPOST)
      LIST_TEST(URLLoader_BinaryDataPOST)
      LIST_TEST(URLLoader_CustomRequestHeader)
      LIST_TEST(URLLoader_FailsBogusContentLength)
      LIST_TEST(URLLoader_StreamToFile)
      LIST_TEST(URLLoader_UntrustedSameOriginRestriction)
      LIST_TEST(URLLoader_TrustedSameOriginRestriction)
      LIST_TEST(URLLoader_UntrustedCrossOriginRequest)
      LIST_TEST(URLLoader_TrustedCrossOriginRequest)
      LIST_TEST(URLLoader_UntrustedJavascriptURLRestriction)
      // TODO(bbudge) Fix Javascript URLs for trusted loaders.
      // http://crbug.com/103062
      LIST_TEST(DISABLED_URLLoader_TrustedJavascriptURLRestriction)
      LIST_TEST(URLLoader_UntrustedHttpRequests)
      LIST_TEST(URLLoader_TrustedHttpRequests)
      LIST_TEST(URLLoader_FollowURLRedirect)
      LIST_TEST(URLLoader_AuditURLRedirect)
      LIST_TEST(URLLoader_AbortCalls)
      LIST_TEST(URLLoader_UntendedLoad)
      LIST_TEST(URLLoader_PrefetchBufferThreshold)
  );
}
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, URLLoader) {
  RunTestViaHTTP(
      LIST_TEST(URLLoader_BasicGET)
      LIST_TEST(URLLoader_BasicPOST)
      LIST_TEST(URLLoader_BasicFilePOST)
      LIST_TEST(URLLoader_BasicFileRangePOST)
      LIST_TEST(URLLoader_CompoundBodyPOST)
      LIST_TEST(URLLoader_EmptyDataPOST)
      LIST_TEST(URLLoader_BinaryDataPOST)
      LIST_TEST(URLLoader_CustomRequestHeader)
      LIST_TEST(URLLoader_FailsBogusContentLength)
      LIST_TEST(URLLoader_StreamToFile)
      LIST_TEST(URLLoader_UntrustedSameOriginRestriction)
      LIST_TEST(URLLoader_TrustedSameOriginRestriction)
      LIST_TEST(URLLoader_UntrustedCrossOriginRequest)
      LIST_TEST(URLLoader_TrustedCrossOriginRequest)
      LIST_TEST(URLLoader_UntrustedJavascriptURLRestriction)
      // TODO(bbudge) Fix Javascript URLs for trusted loaders.
      // http://crbug.com/103062
      LIST_TEST(DISABLED_URLLoader_TrustedJavascriptURLRestriction)
      LIST_TEST(URLLoader_UntrustedHttpRequests)
      LIST_TEST(URLLoader_TrustedHttpRequests)
      LIST_TEST(URLLoader_FollowURLRedirect)
      LIST_TEST(URLLoader_AuditURLRedirect)
      LIST_TEST(URLLoader_AbortCalls)
      LIST_TEST(URLLoader_UntendedLoad)
      LIST_TEST(URLLoader_PrefetchBufferThreshold)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, URLLoader) {
  RunTestViaHTTP(
      LIST_TEST(URLLoader_BasicGET)
      LIST_TEST(URLLoader_BasicPOST)
      LIST_TEST(URLLoader_BasicFilePOST)
      LIST_TEST(URLLoader_BasicFileRangePOST)
      LIST_TEST(URLLoader_CompoundBodyPOST)
      LIST_TEST(URLLoader_EmptyDataPOST)
      LIST_TEST(URLLoader_BinaryDataPOST)
      LIST_TEST(URLLoader_CustomRequestHeader)
      LIST_TEST(URLLoader_FailsBogusContentLength)
      LIST_TEST(URLLoader_StreamToFile)
      LIST_TEST(URLLoader_UntrustedSameOriginRestriction)
      // We don't support Trusted APIs in NaCl.
      LIST_TEST(DISABLED_URLLoader_TrustedSameOriginRestriction)
      LIST_TEST(URLLoader_UntrustedCrossOriginRequest)
      LIST_TEST(DISABLED_URLLoader_TrustedCrossOriginRequest)
      LIST_TEST(URLLoader_UntrustedJavascriptURLRestriction)
      // TODO(bbudge) Fix Javascript URLs for trusted loaders.
      // http://crbug.com/103062
      LIST_TEST(DISABLED_URLLoader_TrustedJavascriptURLRestriction)
      LIST_TEST(URLLoader_UntrustedHttpRequests)
      LIST_TEST(DISABLED_URLLoader_TrustedHttpRequests)
      LIST_TEST(URLLoader_FollowURLRedirect)
      LIST_TEST(URLLoader_AuditURLRedirect)
      LIST_TEST(URLLoader_AbortCalls)
      LIST_TEST(URLLoader_UntendedLoad)
      LIST_TEST(URLLoader_PrefetchBufferThreshold)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(URLLoader)) {
  RunTestViaHTTP(
      LIST_TEST(URLLoader_BasicGET)
      LIST_TEST(URLLoader_BasicPOST)
      LIST_TEST(URLLoader_BasicFilePOST)
      LIST_TEST(URLLoader_BasicFileRangePOST)
      LIST_TEST(URLLoader_CompoundBodyPOST)
      LIST_TEST(URLLoader_EmptyDataPOST)
      LIST_TEST(URLLoader_BinaryDataPOST)
      LIST_TEST(URLLoader_CustomRequestHeader)
      LIST_TEST(URLLoader_FailsBogusContentLength)
      LIST_TEST(URLLoader_StreamToFile)
      LIST_TEST(URLLoader_UntrustedSameOriginRestriction)
      // We don't support Trusted APIs in NaCl.
      LIST_TEST(DISABLED_URLLoader_TrustedSameOriginRestriction)
      LIST_TEST(URLLoader_UntrustedCrossOriginRequest)
      LIST_TEST(DISABLED_URLLoader_TrustedCrossOriginRequest)
      LIST_TEST(URLLoader_UntrustedJavascriptURLRestriction)
      // TODO(bbudge) Fix Javascript URLs for trusted loaders.
      // http://crbug.com/103062
      LIST_TEST(DISABLED_URLLoader_TrustedJavascriptURLRestriction)
      LIST_TEST(URLLoader_UntrustedHttpRequests)
      LIST_TEST(DISABLED_URLLoader_TrustedHttpRequests)
      LIST_TEST(URLLoader_FollowURLRedirect)
      LIST_TEST(URLLoader_AuditURLRedirect)
      LIST_TEST(URLLoader_AbortCalls)
      LIST_TEST(URLLoader_UntendedLoad)
      LIST_TEST(URLLoader_PrefetchBufferThreshold)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, URLLoader) {
  RunTestViaHTTP(
      LIST_TEST(URLLoader_BasicGET)
      LIST_TEST(URLLoader_BasicPOST)
      LIST_TEST(URLLoader_BasicFilePOST)
      LIST_TEST(URLLoader_BasicFileRangePOST)
      LIST_TEST(URLLoader_CompoundBodyPOST)
      LIST_TEST(URLLoader_EmptyDataPOST)
      LIST_TEST(URLLoader_BinaryDataPOST)
      LIST_TEST(URLLoader_CustomRequestHeader)
      LIST_TEST(URLLoader_FailsBogusContentLength)
      LIST_TEST(URLLoader_StreamToFile)
      LIST_TEST(URLLoader_UntrustedSameOriginRestriction)
      // We don't support Trusted APIs in NaCl.
      LIST_TEST(DISABLED_URLLoader_TrustedSameOriginRestriction)
      LIST_TEST(URLLoader_UntrustedCrossOriginRequest)
      LIST_TEST(DISABLED_URLLoader_TrustedCrossOriginRequest)
      LIST_TEST(URLLoader_UntrustedJavascriptURLRestriction)
      // TODO(bbudge) Fix Javascript URLs for trusted loaders.
      // http://crbug.com/103062
      LIST_TEST(DISABLED_URLLoader_TrustedJavascriptURLRestriction)
      LIST_TEST(URLLoader_UntrustedHttpRequests)
      LIST_TEST(DISABLED_URLLoader_TrustedHttpRequests)
      LIST_TEST(URLLoader_FollowURLRedirect)
      LIST_TEST(URLLoader_AuditURLRedirect)
      LIST_TEST(URLLoader_AbortCalls)
      LIST_TEST(URLLoader_UntendedLoad)
      LIST_TEST(URLLoader_PrefetchBufferThreshold)
  );
}

// URLRequestInfo tests.
TEST_PPAPI_IN_PROCESS_VIA_HTTP(URLRequest_CreateAndIsURLRequestInfo)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(URLRequest_CreateAndIsURLRequestInfo)

// Timing out on Windows. http://crbug.com/129571
#if defined(OS_WIN)
#define MAYBE_URLRequest_CreateAndIsURLRequestInfo \
  DISABLED_URLRequest_CreateAndIsURLRequestInfo
#else
#define MAYBE_URLRequest_CreateAndIsURLRequestInfo \
    URLRequest_CreateAndIsURLRequestInfo
#endif
TEST_PPAPI_NACL(MAYBE_URLRequest_CreateAndIsURLRequestInfo)

TEST_PPAPI_IN_PROCESS_VIA_HTTP(URLRequest_SetProperty)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(URLRequest_SetProperty)
// http://crbug.com/167150
TEST_PPAPI_NACL(DISABLED_URLRequest_SetProperty)
TEST_PPAPI_IN_PROCESS_VIA_HTTP(URLRequest_AppendDataToBody)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(URLRequest_AppendDataToBody)
TEST_PPAPI_NACL(URLRequest_AppendDataToBody)
TEST_PPAPI_IN_PROCESS_VIA_HTTP(URLRequest_AppendFileToBody)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(URLRequest_AppendFileToBody)
TEST_PPAPI_NACL(URLRequest_AppendFileToBody)
TEST_PPAPI_IN_PROCESS_VIA_HTTP(URLRequest_Stress)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(URLRequest_Stress)
TEST_PPAPI_NACL(URLRequest_Stress)

TEST_PPAPI_IN_PROCESS(PaintAggregator)
TEST_PPAPI_OUT_OF_PROCESS(PaintAggregator)
TEST_PPAPI_NACL(PaintAggregator)

// TODO(danakj): http://crbug.com/115286
TEST_PPAPI_IN_PROCESS(DISABLED_Scrollbar)
// http://crbug.com/89961
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, DISABLED_Scrollbar) {
  RunTest("Scrollbar");
}
// TODO(danakj): http://crbug.com/115286
TEST_PPAPI_NACL(DISABLED_Scrollbar)

TEST_PPAPI_IN_PROCESS(URLUtil)
TEST_PPAPI_OUT_OF_PROCESS(URLUtil)

TEST_PPAPI_IN_PROCESS(CharSet)
TEST_PPAPI_OUT_OF_PROCESS(CharSet)

TEST_PPAPI_IN_PROCESS(Crypto)
TEST_PPAPI_OUT_OF_PROCESS(Crypto)

TEST_PPAPI_IN_PROCESS(Var)
TEST_PPAPI_OUT_OF_PROCESS(Var)
TEST_PPAPI_NACL(Var)

// Flaky on mac, http://crbug.com/121107
#if defined(OS_MACOSX)
#define MAYBE_VarDeprecated DISABLED_VarDeprecated
#else
#define MAYBE_VarDeprecated VarDeprecated
#endif

TEST_PPAPI_IN_PROCESS(VarDeprecated)
TEST_PPAPI_OUT_OF_PROCESS(MAYBE_VarDeprecated)

// Windows defines 'PostMessage', so we have to undef it.
#ifdef PostMessage
#undef PostMessage
#endif
// PostMessage tests.
IN_PROC_BROWSER_TEST_F(PPAPITest, PostMessage) {
  RunTestViaHTTP(
      LIST_TEST(PostMessage_SendInInit)
      LIST_TEST(PostMessage_SendingData)
      LIST_TEST(PostMessage_SendingArrayBuffer)
      LIST_TEST(DISABLED_PostMessage_SendingArray)
      LIST_TEST(DISABLED_PostMessage_SendingDictionary)
      LIST_TEST(DISABLED_PostMessage_SendingComplexVar)
      LIST_TEST(PostMessage_MessageEvent)
      LIST_TEST(PostMessage_NoHandler)
      LIST_TEST(PostMessage_ExtraParam)
  );
}

// Flaky: crbug.com/269530
#if defined(OS_WIN)
#define MAYBE_PostMessage DISABLED_PostMessage
#else
#define MAYBE_PostMessage PostMessage
#endif
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, MAYBE_PostMessage) {
  RunTestViaHTTP(
      LIST_TEST(PostMessage_SendInInit)
      LIST_TEST(PostMessage_SendingData)
      LIST_TEST(PostMessage_SendingArrayBuffer)
      LIST_TEST(PostMessage_SendingArray)
      LIST_TEST(PostMessage_SendingDictionary)
      LIST_TEST(PostMessage_SendingComplexVar)
      LIST_TEST(PostMessage_MessageEvent)
      LIST_TEST(PostMessage_NoHandler)
      LIST_TEST(PostMessage_ExtraParam)
      LIST_TEST(PostMessage_NonMainThread)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, PostMessage) {
  RunTestViaHTTP(
      LIST_TEST(PostMessage_SendInInit)
      LIST_TEST(PostMessage_SendingData)
      LIST_TEST(PostMessage_SendingArrayBuffer)
      LIST_TEST(PostMessage_SendingArray)
      LIST_TEST(PostMessage_SendingDictionary)
      LIST_TEST(PostMessage_SendingComplexVar)
      LIST_TEST(PostMessage_MessageEvent)
      LIST_TEST(PostMessage_NoHandler)
      LIST_TEST(PostMessage_ExtraParam)
      LIST_TEST(PostMessage_NonMainThread)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(PostMessage)) {
  RunTestViaHTTP(
      LIST_TEST(PostMessage_SendInInit)
      LIST_TEST(PostMessage_SendingData)
      LIST_TEST(PostMessage_SendingArrayBuffer)
      LIST_TEST(PostMessage_SendingArray)
      LIST_TEST(PostMessage_SendingDictionary)
      LIST_TEST(PostMessage_SendingComplexVar)
      LIST_TEST(PostMessage_MessageEvent)
      LIST_TEST(PostMessage_NoHandler)
      LIST_TEST(PostMessage_ExtraParam)
      LIST_TEST(PostMessage_NonMainThread)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, PostMessage) {
  RunTestViaHTTP(
      LIST_TEST(PostMessage_SendInInit)
      LIST_TEST(PostMessage_SendingData)
      LIST_TEST(PostMessage_SendingArrayBuffer)
      LIST_TEST(PostMessage_SendingArray)
      LIST_TEST(PostMessage_SendingDictionary)
      LIST_TEST(PostMessage_SendingComplexVar)
      LIST_TEST(PostMessage_MessageEvent)
      LIST_TEST(PostMessage_NoHandler)
      LIST_TEST(PostMessage_ExtraParam)
      LIST_TEST(PostMessage_NonMainThread)
  );
}

TEST_PPAPI_IN_PROCESS(Memory)
TEST_PPAPI_OUT_OF_PROCESS(Memory)
TEST_PPAPI_NACL(Memory)

TEST_PPAPI_IN_PROCESS(VideoDecoder)
TEST_PPAPI_OUT_OF_PROCESS(VideoDecoder)

// FileIO tests.
IN_PROC_BROWSER_TEST_F(PPAPITest, FileIO) {
  RunTestViaHTTP(
      LIST_TEST(FileIO_Open)
      LIST_TEST(FileIO_OpenDirectory)
      LIST_TEST(FileIO_AbortCalls)
      LIST_TEST(FileIO_ParallelReads)
      LIST_TEST(FileIO_ParallelWrites)
      LIST_TEST(FileIO_NotAllowMixedReadWrite)
      LIST_TEST(FileIO_ReadWriteSetLength)
      LIST_TEST(FileIO_ReadToArrayWriteSetLength)
      LIST_TEST(FileIO_TouchQuery)
      LIST_TEST(FileIO_WillWriteWillSetLength)
      LIST_TEST(FileIO_RequestOSFileHandle)
      LIST_TEST(FileIO_RequestOSFileHandleWithOpenExclusive)
      LIST_TEST(FileIO_Mmap)
  );
}
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, FileIO) {
  RunTestViaHTTP(
      LIST_TEST(FileIO_Open)
      LIST_TEST(FileIO_AbortCalls)
      LIST_TEST(FileIO_ParallelReads)
      LIST_TEST(FileIO_ParallelWrites)
      LIST_TEST(FileIO_NotAllowMixedReadWrite)
      LIST_TEST(FileIO_ReadWriteSetLength)
      LIST_TEST(FileIO_ReadToArrayWriteSetLength)
      LIST_TEST(FileIO_TouchQuery)
      LIST_TEST(FileIO_WillWriteWillSetLength)
      LIST_TEST(FileIO_RequestOSFileHandle)
      LIST_TEST(FileIO_RequestOSFileHandleWithOpenExclusive)
      LIST_TEST(FileIO_Mmap)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, FileIO) {
  RunTestViaHTTP(
      LIST_TEST(FileIO_Open)
      LIST_TEST(FileIO_AbortCalls)
      LIST_TEST(FileIO_ParallelReads)
      LIST_TEST(FileIO_ParallelWrites)
      LIST_TEST(FileIO_NotAllowMixedReadWrite)
      LIST_TEST(FileIO_ReadWriteSetLength)
      LIST_TEST(FileIO_ReadToArrayWriteSetLength)
      LIST_TEST(FileIO_TouchQuery)
      // The following test requires PPB_FileIO_Trusted, not available in NaCl.
      LIST_TEST(DISABLED_FileIO_WillWriteWillSetLength)
      LIST_TEST(FileIO_RequestOSFileHandle)
      LIST_TEST(FileIO_RequestOSFileHandleWithOpenExclusive)
      LIST_TEST(FileIO_Mmap)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(FileIO)) {
  RunTestViaHTTP(
      LIST_TEST(FileIO_Open)
      LIST_TEST(FileIO_AbortCalls)
      LIST_TEST(FileIO_ParallelReads)
      LIST_TEST(FileIO_ParallelWrites)
      LIST_TEST(FileIO_NotAllowMixedReadWrite)
      LIST_TEST(FileIO_ReadWriteSetLength)
      LIST_TEST(FileIO_ReadToArrayWriteSetLength)
      LIST_TEST(FileIO_TouchQuery)
      // The following test requires PPB_FileIO_Trusted, not available in NaCl.
      LIST_TEST(DISABLED_FileIO_WillWriteWillSetLength)
      LIST_TEST(FileIO_RequestOSFileHandle)
      LIST_TEST(FileIO_RequestOSFileHandleWithOpenExclusive)
      LIST_TEST(FileIO_Mmap)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, FileIO) {
  RunTestViaHTTP(
      LIST_TEST(FileIO_Open)
      LIST_TEST(FileIO_AbortCalls)
      LIST_TEST(FileIO_ParallelReads)
      LIST_TEST(FileIO_ParallelWrites)
      LIST_TEST(FileIO_NotAllowMixedReadWrite)
      LIST_TEST(FileIO_ReadWriteSetLength)
      LIST_TEST(FileIO_ReadToArrayWriteSetLength)
      LIST_TEST(FileIO_TouchQuery)
      // The following test requires PPB_FileIO_Trusted, not available in NaCl.
      LIST_TEST(DISABLED_FileIO_WillWriteWillSetLength)
      LIST_TEST(FileIO_RequestOSFileHandle)
      LIST_TEST(FileIO_RequestOSFileHandleWithOpenExclusive)
      LIST_TEST(FileIO_Mmap)
  );
}

IN_PROC_BROWSER_TEST_F(PPAPITest, FileRef) {
  RunTestViaHTTP(
      LIST_TEST(FileRef_Create)
      LIST_TEST(FileRef_GetFileSystemType)
      LIST_TEST(FileRef_GetName)
      LIST_TEST(FileRef_GetPath)
      LIST_TEST(FileRef_GetParent)
      LIST_TEST(FileRef_MakeDirectory)
      LIST_TEST(FileRef_QueryAndTouchFile)
      LIST_TEST(FileRef_DeleteFileAndDirectory)
      LIST_TEST(FileRef_RenameFileAndDirectory)
      // TODO(teravest): Add in-process support.
      // LIST_TEST(FileRef_Query)
      LIST_TEST(FileRef_FileNameEscaping)
      // TODO(teravest): Add in-process support.
      // LIST_TEST(FileRef_ReadDirectoryEntries)
  );
}
// OutOfProcessPPAPITest.FileRef times out fairly often.
// http://crbug.com/241646
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, FileRef) {
  RunTestViaHTTP(
      LIST_TEST(FileRef_Create)
      LIST_TEST(FileRef_GetFileSystemType)
      LIST_TEST(FileRef_GetName)
      LIST_TEST(FileRef_GetPath)
      LIST_TEST(FileRef_GetParent)
      LIST_TEST(FileRef_MakeDirectory)
      LIST_TEST(FileRef_QueryAndTouchFile)
      LIST_TEST(FileRef_DeleteFileAndDirectory)
      LIST_TEST(FileRef_RenameFileAndDirectory)
      LIST_TEST(FileRef_Query)
      LIST_TEST(FileRef_FileNameEscaping)
      LIST_TEST(DISABLED_FileRef_ReadDirectoryEntries)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, FileRef) {
  RunTestViaHTTP(
      LIST_TEST(FileRef_Create)
      LIST_TEST(FileRef_GetFileSystemType)
      LIST_TEST(FileRef_GetName)
      LIST_TEST(FileRef_GetPath)
      LIST_TEST(FileRef_GetParent)
      LIST_TEST(FileRef_MakeDirectory)
      LIST_TEST(FileRef_QueryAndTouchFile)
      LIST_TEST(FileRef_DeleteFileAndDirectory)
      LIST_TEST(FileRef_RenameFileAndDirectory)
      LIST_TEST(FileRef_Query)
      LIST_TEST(FileRef_FileNameEscaping)
      LIST_TEST(DISABLED_FileRef_ReadDirectoryEntries)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(FileRef)) {
  RunTestViaHTTP(
      LIST_TEST(FileRef_Create)
      LIST_TEST(FileRef_GetFileSystemType)
      LIST_TEST(FileRef_GetName)
      LIST_TEST(FileRef_GetPath)
      LIST_TEST(FileRef_GetParent)
      LIST_TEST(FileRef_MakeDirectory)
      LIST_TEST(FileRef_QueryAndTouchFile)
      LIST_TEST(FileRef_DeleteFileAndDirectory)
      LIST_TEST(FileRef_RenameFileAndDirectory)
      LIST_TEST(FileRef_Query)
      LIST_TEST(FileRef_FileNameEscaping)
      LIST_TEST(DISABLED_FileRef_ReadDirectoryEntries)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, FileRef) {
  RunTestViaHTTP(
      LIST_TEST(FileRef_Create)
      LIST_TEST(FileRef_GetFileSystemType)
      LIST_TEST(FileRef_GetName)
      LIST_TEST(FileRef_GetPath)
      LIST_TEST(FileRef_GetParent)
      LIST_TEST(FileRef_MakeDirectory)
      LIST_TEST(FileRef_QueryAndTouchFile)
      LIST_TEST(FileRef_DeleteFileAndDirectory)
      LIST_TEST(FileRef_RenameFileAndDirectory)
      LIST_TEST(FileRef_Query)
      LIST_TEST(FileRef_FileNameEscaping)
      LIST_TEST(DISABLED_FileRef_ReadDirectoryEntries)
  );
}

TEST_PPAPI_IN_PROCESS_VIA_HTTP(FileSystem)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(FileSystem)

// PPAPINaClTest.FileSystem times out consistently on Windows and Mac.
// http://crbug.com/130372
#if defined(OS_MACOSX) || defined(OS_WIN)
#define MAYBE_FileSystem DISABLED_FileSystem
#else
#define MAYBE_FileSystem FileSystem
#endif

TEST_PPAPI_NACL(MAYBE_FileSystem)

#if defined(OS_MACOSX)
// http://crbug.com/103912
#define MAYBE_Fullscreen DISABLED_Fullscreen
#elif defined(OS_LINUX)
// http://crbug.com/146008
#define MAYBE_Fullscreen DISABLED_Fullscreen
#else
#define MAYBE_Fullscreen Fullscreen
#endif

TEST_PPAPI_IN_PROCESS_VIA_HTTP(MAYBE_Fullscreen)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(MAYBE_Fullscreen)
TEST_PPAPI_NACL(MAYBE_Fullscreen)

TEST_PPAPI_IN_PROCESS(X509CertificatePrivate)
TEST_PPAPI_OUT_OF_PROCESS(X509CertificatePrivate)

// There is no proxy. This is used for PDF metrics reporting, and PDF only
// runs in process, so there's currently no need for a proxy.
TEST_PPAPI_IN_PROCESS(UMA)

// NetAddress tests
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, NetAddress) {
  RunTestViaHTTP(
      LIST_TEST(NetAddress_IPv4Address)
      LIST_TEST(NetAddress_IPv6Address)
      LIST_TEST(NetAddress_DescribeAsString)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, NetAddress) {
  RunTestViaHTTP(
      LIST_TEST(NetAddress_IPv4Address)
      LIST_TEST(NetAddress_IPv6Address)
      LIST_TEST(NetAddress_DescribeAsString)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(NetAddress)) {
  RunTestViaHTTP(
      LIST_TEST(NetAddress_IPv4Address)
      LIST_TEST(NetAddress_IPv6Address)
      LIST_TEST(NetAddress_DescribeAsString)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, NetAddress) {
  RunTestViaHTTP(
      LIST_TEST(NetAddress_IPv4Address)
      LIST_TEST(NetAddress_IPv6Address)
      LIST_TEST(NetAddress_DescribeAsString)
  );
}

IN_PROC_BROWSER_TEST_F(PPAPITest, NetAddressPrivate) {
  RunTestViaHTTP(
      LIST_TEST(NetAddressPrivate_AreEqual)
      LIST_TEST(NetAddressPrivate_AreHostsEqual)
      LIST_TEST(NetAddressPrivate_Describe)
      LIST_TEST(NetAddressPrivate_ReplacePort)
      LIST_TEST(NetAddressPrivate_GetAnyAddress)
      LIST_TEST(NetAddressPrivate_DescribeIPv6)
      LIST_TEST(NetAddressPrivate_GetFamily)
      LIST_TEST(NetAddressPrivate_GetPort)
      LIST_TEST(NetAddressPrivate_GetAddress)
      LIST_TEST(NetAddressPrivate_GetScopeID)
  );
}
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, NetAddressPrivate) {
  RunTestViaHTTP(
      LIST_TEST(NetAddressPrivate_AreEqual)
      LIST_TEST(NetAddressPrivate_AreHostsEqual)
      LIST_TEST(NetAddressPrivate_Describe)
      LIST_TEST(NetAddressPrivate_ReplacePort)
      LIST_TEST(NetAddressPrivate_GetAnyAddress)
      LIST_TEST(NetAddressPrivate_DescribeIPv6)
      LIST_TEST(NetAddressPrivate_GetFamily)
      LIST_TEST(NetAddressPrivate_GetPort)
      LIST_TEST(NetAddressPrivate_GetAddress)
      LIST_TEST(NetAddressPrivate_GetScopeID)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, NetAddressPrivate) {
  RunTestViaHTTP(
      LIST_TEST(NetAddressPrivateUntrusted_AreEqual)
      LIST_TEST(NetAddressPrivateUntrusted_AreHostsEqual)
      LIST_TEST(NetAddressPrivateUntrusted_Describe)
      LIST_TEST(NetAddressPrivateUntrusted_ReplacePort)
      LIST_TEST(NetAddressPrivateUntrusted_GetAnyAddress)
      LIST_TEST(NetAddressPrivateUntrusted_GetFamily)
      LIST_TEST(NetAddressPrivateUntrusted_GetPort)
      LIST_TEST(NetAddressPrivateUntrusted_GetAddress)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(NetAddressPrivate)) {
  RunTestViaHTTP(
      LIST_TEST(NetAddressPrivateUntrusted_AreEqual)
      LIST_TEST(NetAddressPrivateUntrusted_AreHostsEqual)
      LIST_TEST(NetAddressPrivateUntrusted_Describe)
      LIST_TEST(NetAddressPrivateUntrusted_ReplacePort)
      LIST_TEST(NetAddressPrivateUntrusted_GetAnyAddress)
      LIST_TEST(NetAddressPrivateUntrusted_GetFamily)
      LIST_TEST(NetAddressPrivateUntrusted_GetPort)
      LIST_TEST(NetAddressPrivateUntrusted_GetAddress)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, NetAddressPrivate) {
  RunTestViaHTTP(
      LIST_TEST(NetAddressPrivateUntrusted_AreEqual)
      LIST_TEST(NetAddressPrivateUntrusted_AreHostsEqual)
      LIST_TEST(NetAddressPrivateUntrusted_Describe)
      LIST_TEST(NetAddressPrivateUntrusted_ReplacePort)
      LIST_TEST(NetAddressPrivateUntrusted_GetAnyAddress)
      LIST_TEST(NetAddressPrivateUntrusted_GetFamily)
      LIST_TEST(NetAddressPrivateUntrusted_GetPort)
      LIST_TEST(NetAddressPrivateUntrusted_GetAddress)
  );
}

// NetworkMonitor tests.
IN_PROC_BROWSER_TEST_F(PPAPITest, NetworkMonitor) {
  RunTestViaHTTP(
      LIST_TEST(NetworkMonitorPrivate_Basic)
      LIST_TEST(NetworkMonitorPrivate_2Monitors)
      LIST_TEST(NetworkMonitorPrivate_DeleteInCallback)
      LIST_TEST(NetworkMonitorPrivate_ListObserver)
  );
}
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, NetworkMonitor) {
  RunTestViaHTTP(
      LIST_TEST(NetworkMonitorPrivate_Basic)
      LIST_TEST(NetworkMonitorPrivate_2Monitors)
      LIST_TEST(NetworkMonitorPrivate_DeleteInCallback)
      LIST_TEST(NetworkMonitorPrivate_ListObserver)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, NetworkMonitor) {
  RunTestViaHTTP(
      LIST_TEST(NetworkMonitorPrivate_Basic)
      LIST_TEST(NetworkMonitorPrivate_2Monitors)
      LIST_TEST(NetworkMonitorPrivate_DeleteInCallback)
      LIST_TEST(NetworkMonitorPrivate_ListObserver)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(NetworkMonitor)) {
  RunTestViaHTTP(
      LIST_TEST(NetworkMonitorPrivate_Basic)
      LIST_TEST(NetworkMonitorPrivate_2Monitors)
      LIST_TEST(NetworkMonitorPrivate_DeleteInCallback)
      LIST_TEST(NetworkMonitorPrivate_ListObserver)
  );
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, NetworkMonitor) {
  RunTestViaHTTP(
      LIST_TEST(NetworkMonitorPrivate_Basic)
      LIST_TEST(NetworkMonitorPrivate_2Monitors)
      LIST_TEST(NetworkMonitorPrivate_DeleteInCallback)
      LIST_TEST(NetworkMonitorPrivate_ListObserver)
  );
}

// Flash tests.
IN_PROC_BROWSER_TEST_F(PPAPITest, Flash) {
  RunTestViaHTTP(
      LIST_TEST(Flash_SetInstanceAlwaysOnTop)
      LIST_TEST(Flash_GetCommandLineArgs)
  );
}
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, Flash) {
  RunTestViaHTTP(
      LIST_TEST(Flash_SetInstanceAlwaysOnTop)
      LIST_TEST(Flash_GetCommandLineArgs)
  );
}

// In-process WebSocket tests
IN_PROC_BROWSER_TEST_F(PPAPITest, WebSocket) {
  RunTestWithWebSocketServer(
      LIST_TEST(WebSocket_IsWebSocket)
      LIST_TEST(WebSocket_UninitializedPropertiesAccess)
      LIST_TEST(WebSocket_InvalidConnect)
      LIST_TEST(WebSocket_Protocols)
      LIST_TEST(WebSocket_GetURL)
      LIST_TEST(WebSocket_ValidConnect)
      LIST_TEST(WebSocket_InvalidClose)
      LIST_TEST(WebSocket_ValidClose)
      LIST_TEST(WebSocket_GetProtocol)
      LIST_TEST(WebSocket_TextSendReceive)
      LIST_TEST(WebSocket_BinarySendReceive)
      LIST_TEST(WebSocket_StressedSendReceive)
      LIST_TEST(WebSocket_BufferedAmount)
      LIST_TEST(WebSocket_AbortCallsWithCallback)
      LIST_TEST(WebSocket_AbortSendMessageCall)
      LIST_TEST(WebSocket_AbortCloseCall)
      LIST_TEST(WebSocket_AbortReceiveMessageCall)
      LIST_TEST(WebSocket_CcInterfaces)
      LIST_TEST(WebSocket_UtilityInvalidConnect)
      LIST_TEST(WebSocket_UtilityProtocols)
      LIST_TEST(WebSocket_UtilityGetURL)
      LIST_TEST(WebSocket_UtilityValidConnect)
      LIST_TEST(WebSocket_UtilityInvalidClose)
      LIST_TEST(WebSocket_UtilityValidClose)
      LIST_TEST(WebSocket_UtilityGetProtocol)
      LIST_TEST(WebSocket_UtilityTextSendReceive)
      LIST_TEST(WebSocket_UtilityBinarySendReceive)
      LIST_TEST(WebSocket_UtilityBufferedAmount));
}

// Out-of-process WebSocket tests
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, WebSocket) {
  RunTestWithWebSocketServer(
      LIST_TEST(WebSocket_IsWebSocket)
      LIST_TEST(WebSocket_UninitializedPropertiesAccess)
      LIST_TEST(WebSocket_InvalidConnect)
      LIST_TEST(WebSocket_Protocols)
      LIST_TEST(WebSocket_GetURL)
      LIST_TEST(WebSocket_ValidConnect)
      LIST_TEST(WebSocket_InvalidClose)
      LIST_TEST(WebSocket_ValidClose)
      LIST_TEST(WebSocket_GetProtocol)
      LIST_TEST(WebSocket_TextSendReceive)
      LIST_TEST(WebSocket_BinarySendReceive)
      LIST_TEST(WebSocket_StressedSendReceive)
      LIST_TEST(WebSocket_BufferedAmount)
      LIST_TEST(WebSocket_AbortCallsWithCallback)
      LIST_TEST(WebSocket_AbortSendMessageCall)
      LIST_TEST(WebSocket_AbortCloseCall)
      LIST_TEST(WebSocket_AbortReceiveMessageCall)
      LIST_TEST(WebSocket_CcInterfaces)
      LIST_TEST(WebSocket_UtilityInvalidConnect)
      LIST_TEST(WebSocket_UtilityProtocols)
      LIST_TEST(WebSocket_UtilityGetURL)
      LIST_TEST(WebSocket_UtilityValidConnect)
      LIST_TEST(WebSocket_UtilityInvalidClose)
      LIST_TEST(WebSocket_UtilityValidClose)
      LIST_TEST(WebSocket_UtilityGetProtocol)
      LIST_TEST(WebSocket_UtilityTextSendReceive)
      LIST_TEST(WebSocket_UtilityBinarySendReceive)
      LIST_TEST(WebSocket_UtilityBufferedAmount));
}

// NaClNewlib WebSocket tests
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, WebSocket) {
  RunTestWithWebSocketServer(
      LIST_TEST(WebSocket_IsWebSocket)
      LIST_TEST(WebSocket_UninitializedPropertiesAccess)
      LIST_TEST(WebSocket_InvalidConnect)
      LIST_TEST(WebSocket_Protocols)
      LIST_TEST(WebSocket_GetURL)
      LIST_TEST(WebSocket_ValidConnect)
      LIST_TEST(WebSocket_InvalidClose)
      LIST_TEST(WebSocket_ValidClose)
      LIST_TEST(WebSocket_GetProtocol)
      LIST_TEST(WebSocket_TextSendReceive)
      LIST_TEST(WebSocket_BinarySendReceive)
      LIST_TEST(WebSocket_StressedSendReceive)
      LIST_TEST(WebSocket_BufferedAmount)
      LIST_TEST(WebSocket_AbortCallsWithCallback)
      LIST_TEST(WebSocket_AbortSendMessageCall)
      LIST_TEST(WebSocket_AbortCloseCall)
      LIST_TEST(WebSocket_AbortReceiveMessageCall)
      LIST_TEST(WebSocket_CcInterfaces)
      LIST_TEST(WebSocket_UtilityInvalidConnect)
      LIST_TEST(WebSocket_UtilityProtocols)
      LIST_TEST(WebSocket_UtilityGetURL)
      LIST_TEST(WebSocket_UtilityValidConnect)
      LIST_TEST(WebSocket_UtilityInvalidClose)
      LIST_TEST(WebSocket_UtilityValidClose)
      LIST_TEST(WebSocket_UtilityGetProtocol)
      LIST_TEST(WebSocket_UtilityTextSendReceive)
      LIST_TEST(WebSocket_UtilityBinarySendReceive)
      LIST_TEST(WebSocket_UtilityBufferedAmount));
}

// NaClGLibc WebSocket tests
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(WebSocket)) {
  RunTestWithWebSocketServer(
      LIST_TEST(WebSocket_IsWebSocket)
      LIST_TEST(WebSocket_UninitializedPropertiesAccess)
      LIST_TEST(WebSocket_InvalidConnect)
      LIST_TEST(WebSocket_Protocols)
      LIST_TEST(WebSocket_GetURL)
      LIST_TEST(WebSocket_ValidConnect)
      LIST_TEST(WebSocket_InvalidClose)
      LIST_TEST(WebSocket_ValidClose)
      LIST_TEST(WebSocket_GetProtocol)
      LIST_TEST(WebSocket_TextSendReceive)
      LIST_TEST(WebSocket_BinarySendReceive)
      LIST_TEST(WebSocket_StressedSendReceive)
      LIST_TEST(WebSocket_BufferedAmount)
      LIST_TEST(WebSocket_AbortCallsWithCallback)
      LIST_TEST(WebSocket_AbortSendMessageCall)
      LIST_TEST(WebSocket_AbortCloseCall)
      LIST_TEST(WebSocket_AbortReceiveMessageCall)
      LIST_TEST(WebSocket_CcInterfaces)
      LIST_TEST(WebSocket_UtilityInvalidConnect)
      LIST_TEST(WebSocket_UtilityProtocols)
      LIST_TEST(WebSocket_UtilityGetURL)
      LIST_TEST(WebSocket_UtilityValidConnect)
      LIST_TEST(WebSocket_UtilityInvalidClose)
      LIST_TEST(WebSocket_UtilityValidClose)
      LIST_TEST(WebSocket_UtilityGetProtocol)
      LIST_TEST(WebSocket_UtilityTextSendReceive)
      LIST_TEST(WebSocket_UtilityBinarySendReceive)
      LIST_TEST(WebSocket_UtilityBufferedAmount));
}

// PNaCl WebSocket tests
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, WebSocket) {
  RunTestWithWebSocketServer(
      LIST_TEST(WebSocket_IsWebSocket)
      LIST_TEST(WebSocket_UninitializedPropertiesAccess)
      LIST_TEST(WebSocket_InvalidConnect)
      LIST_TEST(WebSocket_Protocols)
      LIST_TEST(WebSocket_GetURL)
      LIST_TEST(WebSocket_ValidConnect)
      LIST_TEST(WebSocket_InvalidClose)
      LIST_TEST(WebSocket_ValidClose)
      LIST_TEST(WebSocket_GetProtocol)
      LIST_TEST(WebSocket_TextSendReceive)
      LIST_TEST(WebSocket_BinarySendReceive)
      LIST_TEST(WebSocket_StressedSendReceive)
      LIST_TEST(WebSocket_BufferedAmount)
      LIST_TEST(WebSocket_AbortCallsWithCallback)
      LIST_TEST(WebSocket_AbortSendMessageCall)
      LIST_TEST(WebSocket_AbortCloseCall)
      LIST_TEST(WebSocket_AbortReceiveMessageCall)
      LIST_TEST(WebSocket_CcInterfaces)
      LIST_TEST(WebSocket_UtilityInvalidConnect)
      LIST_TEST(WebSocket_UtilityProtocols)
      LIST_TEST(WebSocket_UtilityGetURL)
      LIST_TEST(WebSocket_UtilityValidConnect)
      LIST_TEST(WebSocket_UtilityInvalidClose)
      LIST_TEST(WebSocket_UtilityValidClose)
      LIST_TEST(WebSocket_UtilityGetProtocol)
      LIST_TEST(WebSocket_UtilityTextSendReceive)
      LIST_TEST(WebSocket_UtilityBinarySendReceive)
      LIST_TEST(WebSocket_UtilityBufferedAmount));
}


// In-process AudioConfig tests
IN_PROC_BROWSER_TEST_F(PPAPITest, AudioConfig) {
  RunTest(
      LIST_TEST(AudioConfig_RecommendSampleRate)
      LIST_TEST(AudioConfig_ValidConfigs)
      LIST_TEST(AudioConfig_InvalidConfigs));
}

// Out-of-process AudioConfig tests
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, AudioConfig) {
  RunTest(
      LIST_TEST(AudioConfig_RecommendSampleRate)
      LIST_TEST(AudioConfig_ValidConfigs)
      LIST_TEST(AudioConfig_InvalidConfigs));
}

// NaClNewlib AudioConfig tests
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, AudioConfig) {
  RunTestViaHTTP(
      LIST_TEST(AudioConfig_RecommendSampleRate)
      LIST_TEST(AudioConfig_ValidConfigs)
      LIST_TEST(AudioConfig_InvalidConfigs));
}

// NaClGLibc AudioConfig tests
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(AudioConfig)) {
  RunTestViaHTTP(
      LIST_TEST(AudioConfig_RecommendSampleRate)
      LIST_TEST(AudioConfig_ValidConfigs)
      LIST_TEST(AudioConfig_InvalidConfigs));
}

// PNaCl AudioConfig tests
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, AudioConfig) {
  RunTestViaHTTP(
      LIST_TEST(AudioConfig_RecommendSampleRate)
      LIST_TEST(AudioConfig_ValidConfigs)
      LIST_TEST(AudioConfig_InvalidConfigs));
}


IN_PROC_BROWSER_TEST_F(PPAPITest, Audio) {
  RunTest(LIST_TEST(Audio_Creation)
          LIST_TEST(Audio_DestroyNoStop)
          LIST_TEST(Audio_Failures)
          LIST_TEST(Audio_AudioCallback1)
          LIST_TEST(Audio_AudioCallback2));
}
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, Audio) {
  RunTest(LIST_TEST(Audio_Creation)
          LIST_TEST(Audio_DestroyNoStop)
          LIST_TEST(Audio_Failures)
          LIST_TEST(Audio_AudioCallback1)
          LIST_TEST(Audio_AudioCallback2));
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, Audio) {
  RunTestViaHTTP(LIST_TEST(Audio_Creation)
                 LIST_TEST(Audio_DestroyNoStop)
                 LIST_TEST(Audio_Failures)
                 LIST_TEST(Audio_AudioCallback1)
                 LIST_TEST(Audio_AudioCallback2));
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(Audio)) {
  RunTestViaHTTP(LIST_TEST(Audio_Creation)
                 LIST_TEST(Audio_DestroyNoStop)
                 LIST_TEST(Audio_Failures)
                 LIST_TEST(Audio_AudioCallback1)
                 LIST_TEST(Audio_AudioCallback2));
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, Audio) {
  RunTestViaHTTP(LIST_TEST(Audio_Creation)
                 LIST_TEST(Audio_DestroyNoStop)
                 LIST_TEST(Audio_Failures)
                 LIST_TEST(Audio_AudioCallback1)
                 LIST_TEST(Audio_AudioCallback2));
}

TEST_PPAPI_IN_PROCESS(View_CreatedVisible);
TEST_PPAPI_OUT_OF_PROCESS(View_CreatedVisible);
TEST_PPAPI_NACL(View_CreatedVisible);
// This test ensures that plugins created in a background tab have their
// initial visibility set to false. We don't bother testing in-process for this
// custom test since the out of process code also exercises in-process.

IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, View_CreateInvisible) {
  // Make a second tab in the foreground.
  GURL url = GetTestFileUrl("View_CreatedInvisible");
  chrome::NavigateParams params(browser(), url, content::PAGE_TRANSITION_LINK);
  params.disposition = NEW_BACKGROUND_TAB;
  ui_test_utils::NavigateToURL(&params);
}

// This test messes with tab visibility so is custom.
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, View_PageHideShow) {
  // The plugin will be loaded in the foreground tab and will send us a message.
  PPAPITestMessageHandler handler;
  JavascriptTestObserver observer(
      browser()->tab_strip_model()->GetActiveWebContents()->GetRenderViewHost(),
      &handler);

  GURL url = GetTestFileUrl("View_PageHideShow");
  ui_test_utils::NavigateToURL(browser(), url);

  ASSERT_TRUE(observer.Run()) << handler.error_message();
  EXPECT_STREQ("TestPageHideShow:Created", handler.message().c_str());
  observer.Reset();

  // Make a new tab to cause the original one to hide, this should trigger the
  // next phase of the test.
  chrome::NavigateParams params(browser(), GURL(content::kAboutBlankURL),
                                content::PAGE_TRANSITION_LINK);
  params.disposition = NEW_FOREGROUND_TAB;
  ui_test_utils::NavigateToURL(&params);

  // Wait until the test acks that it got hidden.
  ASSERT_TRUE(observer.Run()) << handler.error_message();
  EXPECT_STREQ("TestPageHideShow:Hidden", handler.message().c_str());
  observer.Reset();

  // Switch back to the test tab.
  browser()->tab_strip_model()->ActivateTabAt(0, true);

  ASSERT_TRUE(observer.Run()) << handler.error_message();
  EXPECT_STREQ("PASS", handler.message().c_str());
}

// Tests that if a plugin accepts touch events, the browser knows to send touch
// events to the renderer.
IN_PROC_BROWSER_TEST_F(PPAPITest, InputEvent_AcceptTouchEvent) {
  std::string positive_tests[] = { "InputEvent_AcceptTouchEvent_1",
                                   "InputEvent_AcceptTouchEvent_2",
                                   "InputEvent_AcceptTouchEvent_3",
                                   "InputEvent_AcceptTouchEvent_4"
                                 };

  for (size_t i = 0; i < arraysize(positive_tests); ++i) {
    RenderViewHost* host = browser()->tab_strip_model()->
        GetActiveWebContents()->GetRenderViewHost();
    RunTest(positive_tests[i]);
    EXPECT_TRUE(content::RenderViewHostTester::HasTouchEventHandler(host));
  }
}

IN_PROC_BROWSER_TEST_F(PPAPITest, View) {
  RunTest(LIST_TEST(View_SizeChange)
          LIST_TEST(View_ClipChange));
}
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, View) {
  RunTest(LIST_TEST(View_SizeChange)
          LIST_TEST(View_ClipChange));
}
IN_PROC_BROWSER_TEST_F(PPAPINaClNewlibTest, View) {
  RunTestViaHTTP(LIST_TEST(View_SizeChange)
                 LIST_TEST(View_ClipChange));
}
IN_PROC_BROWSER_TEST_F(PPAPINaClGLibcTest, MAYBE_GLIBC(View)) {
  RunTestViaHTTP(LIST_TEST(View_SizeChange)
                 LIST_TEST(View_ClipChange));
}
IN_PROC_BROWSER_TEST_F(PPAPINaClPNaClTest, View) {
  RunTestViaHTTP(LIST_TEST(View_SizeChange)
                 LIST_TEST(View_ClipChange));
}

IN_PROC_BROWSER_TEST_F(PPAPITest, ResourceArray) {
  RunTest(LIST_TEST(ResourceArray_Basics)
          LIST_TEST(ResourceArray_OutOfRangeAccess)
          LIST_TEST(ResourceArray_EmptyArray)
          LIST_TEST(ResourceArray_InvalidElement));
}
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, ResourceArray) {
  RunTest(LIST_TEST(ResourceArray_Basics)
          LIST_TEST(ResourceArray_OutOfRangeAccess)
          LIST_TEST(ResourceArray_EmptyArray)
          LIST_TEST(ResourceArray_InvalidElement));
}

IN_PROC_BROWSER_TEST_F(PPAPITest, FlashMessageLoop) {
  RunTest(LIST_TEST(FlashMessageLoop_Basics)
          LIST_TEST(FlashMessageLoop_RunWithoutQuit));
}
IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, FlashMessageLoop) {
  RunTest(LIST_TEST(FlashMessageLoop_Basics)
          LIST_TEST(FlashMessageLoop_RunWithoutQuit));
}

TEST_PPAPI_IN_PROCESS(MouseCursor)
TEST_PPAPI_OUT_OF_PROCESS(MouseCursor)
TEST_PPAPI_NACL(MouseCursor)

// PPB_NetworkProxy is not supported in-process.
TEST_PPAPI_OUT_OF_PROCESS(NetworkProxy)
TEST_PPAPI_NACL(NetworkProxy)

TEST_PPAPI_OUT_OF_PROCESS(TrueTypeFont)
TEST_PPAPI_NACL(TrueTypeFont)

TEST_PPAPI_OUT_OF_PROCESS(VideoDestination)
TEST_PPAPI_NACL(VideoDestination)

TEST_PPAPI_OUT_OF_PROCESS(VideoSource)
TEST_PPAPI_NACL(VideoSource)

// PPB_Printing only implemented for out of process.
TEST_PPAPI_OUT_OF_PROCESS(Printing)

// PPB_MessageLoop is only supported out-of-process.
// TODO(dmichael): Enable for NaCl with the IPC proxy. crbug.com/116317
TEST_PPAPI_OUT_OF_PROCESS(MessageLoop_Basics)
// MessageLoop_Post starts a thread so only run it if pepper threads are
// enabled.
#ifdef ENABLE_PEPPER_THREADING
TEST_PPAPI_OUT_OF_PROCESS(MessageLoop_Post)
#endif

// Going forward, Flash APIs will only work out-of-process.
TEST_PPAPI_OUT_OF_PROCESS(Flash_GetLocalTimeZoneOffset)
TEST_PPAPI_OUT_OF_PROCESS(Flash_GetProxyForURL)
TEST_PPAPI_OUT_OF_PROCESS(Flash_GetSetting)
TEST_PPAPI_OUT_OF_PROCESS(Flash_SetCrashData)
// http://crbug.com/176822
#if !defined(OS_WIN)
TEST_PPAPI_OUT_OF_PROCESS(FlashClipboard)
#endif
TEST_PPAPI_OUT_OF_PROCESS(FlashFile)
// Mac/Aura reach NOTIMPLEMENTED/time out.
// mac: http://crbug.com/96767
// aura: http://crbug.com/104384
#if defined(OS_MACOSX)
#define MAYBE_FlashFullscreen DISABLED_FlashFullscreen
#else
#define MAYBE_FlashFullscreen FlashFullscreen
#endif
TEST_PPAPI_OUT_OF_PROCESS(MAYBE_FlashFullscreen)

TEST_PPAPI_OUT_OF_PROCESS(PDF)

IN_PROC_BROWSER_TEST_F(OutOfProcessPPAPITest, FlashDRM) {
  RunTest(
#if (defined(OS_WIN) && defined(ENABLE_RLZ)) || defined(OS_CHROMEOS)
          // Only implemented on Windows and ChromeOS currently.
          LIST_TEST(FlashDRM_GetDeviceID)
#endif
          LIST_TEST(FlashDRM_GetHmonitor)
          LIST_TEST(FlashDRM_GetVoucherFile));
}

TEST_PPAPI_IN_PROCESS(TalkPrivate)
TEST_PPAPI_OUT_OF_PROCESS(TalkPrivate)

#endif // ADDRESS_SANITIZER
