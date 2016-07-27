// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_NAVIGATION_HANDLE_H_
#define CONTENT_PUBLIC_BROWSER_NAVIGATION_HANDLE_H_

#include "content/common/content_export.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/common/referrer.h"
#include "net/base/net_errors.h"
#include "ui/base/page_transition_types.h"

class GURL;

namespace content {
class NavigationThrottle;
class RenderFrameHost;
class WebContents;

// A NavigationHandle tracks information related to a single navigation.
class CONTENT_EXPORT NavigationHandle {
 public:
  virtual ~NavigationHandle() {}

  // Parameters available at navigation start time -----------------------------
  //
  // These parameters are always available during the navigation. Note that
  // some may change during navigation (e.g. due to server redirects).

  // The URL the frame is navigating to. This may change during the navigation
  // when encountering a server redirect.
  virtual const GURL& GetURL() = 0;

  // Whether the navigation is taking place in the main frame or in a subframe.
  // This remains constant over the navigation lifetime.
  virtual bool IsInMainFrame() = 0;

  // The WebContents the navigation is taking place in.
  WebContents* GetWebContents();

  // The time the navigation started, recorded either in the renderer or in the
  // browser process. Corresponds to Navigation Timing API.
  virtual const base::TimeTicks& NavigationStart() = 0;

  // Parameters available at network request start time ------------------------
  //
  // The following parameters are only available when the network request is
  // made for the navigation (or at commit time if no network request is made).
  // This corresponds to NavigationThrottle::WillSendRequest. They should not
  // be queried before that.

  // Whether the navigation is a POST or a GET. This may change during the
  // navigation when encountering a server redirect.
  virtual bool IsPost() = 0;

  // Returns a sanitized version of the referrer for this request.
  virtual const Referrer& GetReferrer() = 0;

  // Whether the navigation was initiated by a user gesture. Note that this
  // will return false for browser-initiated navigations.
  // TODO(clamy): when PlzNavigate launches, this should return true for
  // browser-initiated navigations.
  virtual bool HasUserGesture() = 0;

  // Returns the page transition type.
  virtual ui::PageTransition GetPageTransition() = 0;

  // Whether the target URL cannot be handled by the browser's internal protocol
  // handlers.
  virtual bool IsExternalProtocol() = 0;

  // Navigation control flow --------------------------------------------------

  // The net error code if an error happened prior to commit. Otherwise it will
  // be net::OK.
  virtual net::Error GetNetErrorCode() = 0;

  // Returns the RenderFrameHost this navigation is taking place in. This can
  // only be accessed after the navigation is ready to commit.
  virtual RenderFrameHost* GetRenderFrameHost() = 0;

  // Whether the navigation happened in the same page. This is only known
  // after the navigation has committed. It is an error to call this method
  // before the navigation has committed.
  virtual bool IsSamePage() = 0;

  // Whether the navigation has committed. This returns true for either
  // successful commits or error pages that replace the previous page
  // (distinguished by |IsErrorPage|), and false for errors that leave the user
  // on the previous page.
  virtual bool HasCommitted() = 0;

  // Whether the navigation resulted in an error page.
  virtual bool IsErrorPage() = 0;

  // Resumes a navigation that was previously deferred by a NavigationThrottle.
  virtual void Resume() = 0;

  // Cancels a navigation that was previously deferred by a NavigationThrottle.
  // |result| should be equal to NavigationThrottle::CANCEL or
  // NavigationThrottle::CANCEL_AND_IGNORE.
  virtual void CancelDeferredNavigation(
      NavigationThrottle::ThrottleCheckResult result) = 0;

  // Testing methods ----------------------------------------------------------
  //
  // The following methods should be used exclusively for writing unit tests.

  static scoped_ptr<NavigationHandle> CreateNavigationHandleForTesting(
      const GURL& url,
      RenderFrameHost* render_frame_host);

  // Registers a NavigationThrottle for tests. The throttle can
  // modify the request, pause the request or cancel the request. This will
  // take ownership of the NavigationThrottle.
  // Note: in non-test cases, NavigationThrottles should not be added directly
  // but returned by the implementation of
  // ContentBrowserClient::CreateThrottlesForNavigation. This ensures proper
  // ordering of the throttles.
  virtual void RegisterThrottleForTesting(
      scoped_ptr<NavigationThrottle> navigation_throttle) = 0;

  // Simulates the network request starting.
  virtual NavigationThrottle::ThrottleCheckResult
  CallWillStartRequestForTesting(bool is_post,
                                 const Referrer& sanitized_referrer,
                                 bool has_user_gesture,
                                 ui::PageTransition transition,
                                 bool is_external_protocol) = 0;

  // Simulates the network request being redirected.
  virtual NavigationThrottle::ThrottleCheckResult
  CallWillRedirectRequestForTesting(const GURL& new_url,
                                    bool new_method_is_post,
                                    const GURL& new_referrer_url,
                                    bool new_is_external_protocol) = 0;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_NAVIGATION_HANDLE_H_
