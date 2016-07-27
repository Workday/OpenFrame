// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FRAME_HOST_RENDER_FRAME_MESSAGE_FILTER_H_
#define CONTENT_BROWSER_FRAME_HOST_RENDER_FRAME_MESSAGE_FILTER_H_

#include <set>

#include "content/common/frame_replication_state.h"
#include "content/public/browser/browser_message_filter.h"
#include "content/public/common/three_d_api_types.h"
#include "net/cookies/canonical_cookie.h"
#include "third_party/WebKit/public/web/WebFrameOwnerProperties.h"
#include "third_party/WebKit/public/web/WebTreeScopeType.h"

#if defined(ENABLE_PLUGINS)
#include "content/common/pepper_renderer_instance_data.h"
#endif

class GURL;

namespace net {
class URLRequestContext;
class URLRequestContextGetter;
}

namespace content {
class BrowserContext;
class PluginServiceImpl;
class RenderWidgetHelper;
class ResourceContext;
struct WebPluginInfo;

// RenderFrameMessageFilter intercepts FrameHost messages on the IO thread
// that require low-latency processing. The canonical example of this is
// child-frame creation which is a sync IPC that provides the renderer
// with the routing id for a newly created RenderFrame.
//
// This object is created on the UI thread and used on the IO thread.
class RenderFrameMessageFilter : public BrowserMessageFilter {
 public:
  RenderFrameMessageFilter(int render_process_id,
                           PluginServiceImpl* plugin_service,
                           BrowserContext* browser_context,
                           net::URLRequestContextGetter* request_context,
                           RenderWidgetHelper* render_widget_helper);

  // IPC::MessageFilter methods:
  void OnChannelClosing() override;

  // BrowserMessageFilter methods:
  bool OnMessageReceived(const IPC::Message& message) override;

 private:
  class OpenChannelToNpapiPluginCallback;
  class OpenChannelToPpapiPluginCallback;
  class OpenChannelToPpapiBrokerCallback;

  ~RenderFrameMessageFilter() override;

  void OnCreateChildFrame(
      int parent_routing_id,
      blink::WebTreeScopeType scope,
      const std::string& frame_name,
      blink::WebSandboxFlags sandbox_flags,
      const blink::WebFrameOwnerProperties& frame_owner_properties,
      int* new_render_frame_id);
  void OnSetCookie(int render_frame_id,
                   const GURL& url,
                   const GURL& first_party_for_cookies,
                   const std::string& cookie);
  void OnGetCookies(int render_frame_id,
                    const GURL& url,
                    const GURL& first_party_for_cookies,
                    IPC::Message* reply_msg);
  void OnCookiesEnabled(int render_frame_id,
                        const GURL& url,
                        const GURL& first_party_for_cookies,
                        bool* cookies_enabled);

  // Check the policy for getting cookies. Gets the cookies if allowed.
  void CheckPolicyForCookies(int render_frame_id,
                             const GURL& url,
                             const GURL& first_party_for_cookies,
                             IPC::Message* reply_msg,
                             const net::CookieList& cookie_list);

  // Writes the cookies to reply messages, and sends the message.
  // Callback functions for getting cookies from cookie store.
  void SendGetCookiesResponse(IPC::Message* reply_msg,
                              const std::string& cookies);

  void OnAre3DAPIsBlocked(int render_frame_id,
                          const GURL& top_origin_url,
                          ThreeDAPIType requester,
                          bool* blocked);
  void OnDidLose3DContext(const GURL& top_origin_url,
                          ThreeDAPIType context_type,
                          int arb_robustness_status_code);

#if defined(ENABLE_PLUGINS)
  void OnGetPlugins(bool refresh, IPC::Message* reply_msg);
  void GetPluginsCallback(IPC::Message* reply_msg,
                          const std::vector<WebPluginInfo>& plugins);
  void OnGetPluginInfo(int render_frame_id,
                       const GURL& url,
                       const GURL& policy_url,
                       const std::string& mime_type,
                       bool* found,
                       WebPluginInfo* info,
                       std::string* actual_mime_type);
  void OnOpenChannelToPlugin(int render_frame_id,
                             const GURL& url,
                             const GURL& policy_url,
                             const std::string& mime_type,
                             IPC::Message* reply_msg);
  void OnCompletedOpenChannelToNpapiPlugin(
      OpenChannelToNpapiPluginCallback* client);
  void OnOpenChannelToPepperPlugin(const base::FilePath& path,
                                   IPC::Message* reply_msg);
  void OnDidCreateOutOfProcessPepperInstance(
      int plugin_child_id,
      int32 pp_instance,
      PepperRendererInstanceData instance_data,
      bool is_external);
  void OnDidDeleteOutOfProcessPepperInstance(int plugin_child_id,
                                             int32 pp_instance,
                                             bool is_external);
  void OnOpenChannelToPpapiBroker(int routing_id,
                                  const base::FilePath& path);
  void OnPluginInstanceThrottleStateChange(int plugin_child_id,
                                           int32 pp_instance,
                                           bool is_throttled);
#endif  // ENABLE_PLUGINS

  // Returns the correct net::URLRequestContext depending on what type of url is
  // given.
  // Only call on the IO thread.
  net::URLRequestContext* GetRequestContextForURL(const GURL& url);

#if defined(ENABLE_PLUGINS)
  PluginServiceImpl* plugin_service_;
  base::FilePath profile_data_directory_;

  // Initialized to 0, accessed on FILE thread only.
  base::TimeTicks last_plugin_refresh_time_;

  std::set<OpenChannelToNpapiPluginCallback*> plugin_host_clients_;
#endif  // ENABLE_PLUGINS

  // Contextual information to be used for requests created here.
  scoped_refptr<net::URLRequestContextGetter> request_context_;

  // The ResourceContext which is to be used on the IO thread.
  ResourceContext* resource_context_;

  // Needed for issuing routing ids and surface ids.
  scoped_refptr<RenderWidgetHelper> render_widget_helper_;

  // Whether this process is used for incognito contents.
  bool incognito_;

  const int render_process_id_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_FRAME_HOST_RENDER_FRAME_MESSAGE_FILTER_H_
