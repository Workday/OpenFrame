// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOMATION_AUTOMATION_RESOURCE_MESSAGE_FILTER_H_
#define CHROME_BROWSER_AUTOMATION_AUTOMATION_RESOURCE_MESSAGE_FILTER_H_

#include <map>
#include <string>
#include <vector>

#include "base/atomicops.h"
#include "base/lazy_instance.h"
#include "ipc/ipc_channel_proxy.h"
#include "net/base/completion_callback.h"

class GURL;
class URLRequestAutomationJob;

namespace content {
class BrowserMessageFilter;
}  // namespace content

namespace net {
class URLRequestContext;
}  // namespace net

// This class filters out incoming automation IPC messages for network
// requests and processes them on the IPC thread.  As a result, network
// requests are not delayed by costly UI processing that may be occurring
// on the main thread of the browser.  It also means that any hangs in
// starting a network request will not interfere with browser UI.
class AutomationResourceMessageFilter
    : public IPC::ChannelProxy::MessageFilter,
      public IPC::Sender {
 public:
  // Information needed to send IPCs through automation.
  struct AutomationDetails {
    AutomationDetails();
    AutomationDetails(int tab, AutomationResourceMessageFilter* flt,
                      bool pending_view);
    ~AutomationDetails();

    int tab_handle;
    int ref_count;
    scoped_refptr<AutomationResourceMessageFilter> filter;
    // Indicates whether network requests issued by this render view need to
    // be executed later.
    bool is_pending_render_view;
  };

  // Create the filter.
  AutomationResourceMessageFilter();
  virtual ~AutomationResourceMessageFilter();

  // Returns a new automation request id. This is unique across all instances
  // of AutomationResourceMessageFilter.
  int NewAutomationRequestId() {
    return base::subtle::Barrier_AtomicIncrement(&unique_request_id_, 1);
  }

  // IPC::ChannelProxy::MessageFilter methods:
  virtual void OnFilterAdded(IPC::Channel* channel);
  virtual void OnFilterRemoved();

  // IPC::Listener implementation.
  virtual void OnChannelConnected(int32 peer_pid) OVERRIDE;
  virtual void OnChannelClosing() OVERRIDE;
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

  // ResourceDispatcherHost::Receiver methods:
  virtual bool Send(IPC::Message* message);

  // Add request to the list of outstanding requests.
  virtual bool RegisterRequest(URLRequestAutomationJob* job);

  // Remove request from the list of outstanding requests.
  virtual void UnRegisterRequest(URLRequestAutomationJob* job);

  // Can be called from the UI thread.
  // The pending_view parameter should be true if network requests initiated by
  // this render view need to be paused waiting for an acknowledgement from
  // the external host.
  static bool RegisterRenderView(int renderer_pid,
                                 int renderer_id,
                                 int tab_handle,
                                 AutomationResourceMessageFilter* filter,
                                 bool pending_view);
  static void UnRegisterRenderView(int renderer_pid, int renderer_id);

  // Can be called from the UI thread.
  // Resumes pending render views, i.e. network requests issued by this view
  // can now be serviced.
  static bool ResumePendingRenderView(int renderer_pid,
                                      int renderer_id,
                                      int tab_handle,
                                      AutomationResourceMessageFilter* filter);

  // Called only on the IO thread.
  static bool LookupRegisteredRenderView(
      int renderer_pid, int renderer_id, AutomationDetails* details);

  // Sends the download request to the automation host.
  bool SendDownloadRequestToHost(int routing_id, int tab_handle,
                                 int request_id);

  // If this returns true, then the get and set cookie IPCs should be sent to
  // the following two functions.
  static bool ShouldFilterCookieMessages(int render_process_id,
                                         int render_view_id);

  // Retrieves cookies for the url passed in from the external host. The
  // callback passed in is notified on success or failure asynchronously.
  static void GetCookiesForUrl(content::BrowserMessageFilter* filter,
                               net::URLRequestContext* context,
                               int render_process_id,
                               IPC::Message* reply_msg,
                               const GURL& url);

  // Sets cookies on the URL in the external host.
  static void SetCookiesForUrl(int render_process_id,
                               int render_view_id,
                               const GURL& url,
                               const std::string& cookie_line);

  // This function gets invoked when we receive a response from the external
  // host for the cookie request sent in GetCookiesForUrl above. It sets the
  // cookie temporarily on the cookie store and executes the completion
  // callback which reads the cookie from the store. The cookie value is reset
  // after the callback finishes executing.
  void OnGetCookiesHostResponse(int tab_handle, bool success, const GURL& url,
                                const std::string& cookies, int cookie_id);

 protected:
  // Retrieves the automation request id for the passed in chrome request
  // id and returns it in the automation_request_id parameter.
  // Returns true on success.
  bool GetAutomationRequestId(int request_id, int* automation_request_id);

  static void RegisterRenderViewInIOThread(int renderer_pid, int renderer_id,
      int tab_handle, AutomationResourceMessageFilter* filter,
      bool pending_view);
  static void UnRegisterRenderViewInIOThread(int renderer_pid, int renderer_id);

  static void ResumePendingRenderViewInIOThread(
      int renderer_pid, int renderer_id, int tab_handle,
      AutomationResourceMessageFilter* filter);

 private:
  // Resumes pending jobs from the old AutomationResourceMessageFilter instance
  // passed in.
  static void ResumeJobsForPendingView(
      int tab_handle,
      AutomationResourceMessageFilter* old_filter,
      AutomationResourceMessageFilter* new_filter);

  static int GetNextCompletionCallbackId() {
    return ++next_completion_callback_id_;
  }

  // A unique renderer id is a combination of renderer process id and
  // it's routing id.
  struct RendererId {
    int pid_;
    int id_;

    RendererId() : pid_(0), id_(0) {}
    RendererId(int pid, int id) : pid_(pid), id_(id) {}

    bool operator < (const RendererId& rhs) const {
      return ((pid_ == rhs.pid_) ? (id_ < rhs.id_) : (pid_ < rhs.pid_));
    }
  };

  typedef std::map<RendererId, AutomationDetails> RenderViewMap;
  typedef std::map<int, URLRequestAutomationJob*> RequestMap;

  // The channel associated with the automation connection. This pointer is not
  // owned by this class.
  IPC::Channel* channel_;

  // A unique request id per process.
  static int unique_request_id_;

  // Map of outstanding requests.
  RequestMap request_map_;

  // Map of pending requests, i.e. requests which were waiting for the external
  // host to connect back.
  RequestMap pending_request_map_;

  // Map of render views interested in diverting url requests over automation.
  static base::LazyInstance<RenderViewMap> filtered_render_views_;

  // Contains information used for completing the request to read cookies from
  // the host coming in from the renderer.
  struct CookieCompletionInfo;

  // Map of completion callback id to CookieCompletionInfo, which contains the
  // actual callback which is invoked on successful retrieval of cookies from
  // host. The mapping is setup when GetCookiesForUrl is invoked to retrieve
  // cookies from the host and is removed when we receive a response from the
  // host. Please see the OnGetCookiesHostResponse function.
  typedef std::map<int, CookieCompletionInfo> CompletionCallbackMap;
  static base::LazyInstance<CompletionCallbackMap> completion_callback_map_;

  // Contains the id of the next completion callback. This is passed to the the
  // external host as a cookie referring to the completion callback.
  static int next_completion_callback_id_;

  DISALLOW_COPY_AND_ASSIGN(AutomationResourceMessageFilter);
};

#endif  // CHROME_BROWSER_AUTOMATION_AUTOMATION_RESOURCE_MESSAGE_FILTER_H_
