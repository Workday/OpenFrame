// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// This class simulates what wininet does when a dns lookup fails.

#ifndef CHROME_BROWSER_AUTOMATION_URL_REQUEST_AUTOMATION_JOB_H_
#define CHROME_BROWSER_AUTOMATION_URL_REQUEST_AUTOMATION_JOB_H_

#include "base/memory/weak_ptr.h"
#include "chrome/common/ref_counted_util.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_job.h"

class AutomationResourceMessageFilter;
struct AutomationURLResponse;

namespace net {
class HttpResponseHeaders;
class HttpResponseInfo;
class HttpUserAgentSettings;
class HostPortPair;
}

namespace IPC {
class Message;
}

// net::URLRequestJob implementation that loads the resources using
// automation.
class URLRequestAutomationJob : public net::URLRequestJob {
 public:
  URLRequestAutomationJob(
      net::URLRequest* request,
      net::NetworkDelegate* network_delegate,
      const net::HttpUserAgentSettings* http_user_agent_settings,
      int tab,
      int request_id,
      AutomationResourceMessageFilter* filter,
      bool is_pending);

  // Register our factory for HTTP/HTTPs requests.
  static void EnsureProtocolFactoryRegistered();

  static net::URLRequest::ProtocolFactory Factory;

  // net::URLRequestJob methods.
  virtual void Start();
  virtual void Kill();
  virtual bool GetMimeType(std::string* mime_type) const;
  virtual bool GetCharset(std::string* charset);
  virtual void GetResponseInfo(net::HttpResponseInfo* info);
  virtual void GetLoadTimingInfo(net::LoadTimingInfo* load_timing_info) const;
  virtual int GetResponseCode() const;
  virtual bool IsRedirectResponse(GURL* location, int* http_status_code);
  virtual net::UploadProgress GetUploadProgress() const;
  virtual net::HostPortPair GetSocketAddress() const;

  // Peek and process automation messages for URL requests.
  static bool MayFilterMessage(const IPC::Message& message, int* request_id);
  void OnMessage(const IPC::Message& message);

  int id() const {
    return id_;
  }

  int request_id() const {
    return request_id_;
  }

  bool is_pending() const {
    return is_pending_;
  }

  AutomationResourceMessageFilter* message_filter() const {
    return message_filter_.get();
  }

  // Resumes a job, which was waiting for the external host to connect to the
  // automation channel. This is to ensure that this request gets routed to the
  // external host.
  void StartPendingJob(int new_tab_handle,
                       AutomationResourceMessageFilter* new_filter);

 protected:
  // Protected net::URLRequestJob override.
  virtual bool ReadRawData(net::IOBuffer* buf, int buf_size, int* bytes_read);

  void StartAsync();
  void Cleanup();
  void DisconnectFromMessageFilter();

  // IPC message handlers.
  void OnRequestStarted(int id, const AutomationURLResponse& response);
  void OnDataAvailable(int id, const std::string& bytes);
  void OnRequestEnd(int id, const net::URLRequestStatus& status);

 private:
  virtual ~URLRequestAutomationJob();

  // Task which is scheduled in the URLRequestAutomationJob::ReadRawData
  // function, which completes the job.
  void NotifyJobCompletionTask();

  const net::HttpUserAgentSettings* http_user_agent_settings_;
  int id_;
  int tab_;
  scoped_refptr<AutomationResourceMessageFilter> message_filter_;

  scoped_refptr<net::IOBuffer> pending_buf_;
  size_t pending_buf_size_;

  std::string mime_type_;
  scoped_refptr<net::HttpResponseHeaders> headers_;
  std::string redirect_url_;
  int redirect_status_;
  int request_id_;

  static int instance_count_;

  static bool is_protocol_factory_registered_;
  // The previous HTTP/HTTPs protocol factories. We pass unhandled
  // requests off to these factories
  static net::URLRequest::ProtocolFactory* old_http_factory_;
  static net::URLRequest::ProtocolFactory* old_https_factory_;

  // Set to true if the job is waiting for the external host to connect to the
  // automation channel, which will be used for routing the network requests to
  // the host.
  bool is_pending_;

  // Contains the request status code, which is eventually passed  to the http
  // stack when we receive a Read request for a completed job.
  net::URLRequestStatus request_status_;

  // Contains the ip address and port of the destination host.
  net::HostPortPair socket_address_;

  // Size of the upload data appended to the request.
  uint64 upload_size_;

  // When the request was sent out over automation.
  base::TimeTicks request_start_;

  // When the response headers arrived from automation.
  base::TimeTicks receive_headers_end_;

  base::WeakPtrFactory<URLRequestAutomationJob> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestAutomationJob);
};

#endif  // CHROME_BROWSER_AUTOMATION_URL_REQUEST_AUTOMATION_JOB_H_
