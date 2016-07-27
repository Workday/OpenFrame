// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/net/aw_request_interceptor.h"

#include "android_webview/browser/aw_contents_io_thread_client.h"
#include "android_webview/browser/input_stream.h"
#include "android_webview/browser/net/android_stream_reader_url_request_job.h"
#include "android_webview/browser/net/aw_web_resource_response.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/supports_user_data.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/resource_request_info.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_request_job.h"

namespace android_webview {

namespace {

const void* const kRequestAlreadyHasJobDataKey = &kRequestAlreadyHasJobDataKey;

class StreamReaderJobDelegateImpl
    : public AndroidStreamReaderURLRequestJob::Delegate {
 public:
  StreamReaderJobDelegateImpl(
      scoped_ptr<AwWebResourceResponse> aw_web_resource_response)
      : aw_web_resource_response_(aw_web_resource_response.Pass()) {
    DCHECK(aw_web_resource_response_);
  }

  scoped_ptr<InputStream> OpenInputStream(JNIEnv* env,
                                          const GURL& url) override {
    return aw_web_resource_response_->GetInputStream(env).Pass();
  }

  void OnInputStreamOpenFailed(net::URLRequest* request,
                               bool* restart) override {
    *restart = false;
  }

  bool GetMimeType(JNIEnv* env,
                   net::URLRequest* request,
                   android_webview::InputStream* stream,
                   std::string* mime_type) override {
    return aw_web_resource_response_->GetMimeType(env, mime_type);
  }

  bool GetCharset(JNIEnv* env,
                  net::URLRequest* request,
                  android_webview::InputStream* stream,
                  std::string* charset) override {
    return aw_web_resource_response_->GetCharset(env, charset);
  }

  void AppendResponseHeaders(JNIEnv* env,
                             net::HttpResponseHeaders* headers) override {
    int status_code;
    std::string reason_phrase;
    if (aw_web_resource_response_->GetStatusInfo(
            env, &status_code, &reason_phrase)) {
      std::string status_line("HTTP/1.1 ");
      status_line.append(base::IntToString(status_code));
      status_line.append(" ");
      status_line.append(reason_phrase);
      headers->ReplaceStatusLine(status_line);
    }
    aw_web_resource_response_->GetResponseHeaders(env, headers);
  }

 private:
  scoped_ptr<AwWebResourceResponse> aw_web_resource_response_;
};

class ShouldInterceptRequestAdaptor
    : public AndroidStreamReaderURLRequestJob::DelegateObtainer {
 public:
  explicit ShouldInterceptRequestAdaptor(
      scoped_ptr<AwContentsIoThreadClient> io_thread_client)
      : io_thread_client_(io_thread_client.Pass()), weak_factory_(this) {}
   ~ShouldInterceptRequestAdaptor() override {}

  void ObtainDelegate(net::URLRequest* request,
                      const Callback& callback) override {
    callback_ = callback;
    io_thread_client_->ShouldInterceptRequestAsync(
        // The request is only used while preparing the call, not retained.
        request,
        base::Bind(&ShouldInterceptRequestAdaptor::WebResourceResponseObtained,
                   // The lifetime of the DelegateObtainer is managed by
                   // AndroidStreamReaderURLRequestJob, it might get deleted.
                   weak_factory_.GetWeakPtr()));
  }

 private:
  void WebResourceResponseObtained(
      scoped_ptr<AwWebResourceResponse> response) {
    if (response) {
      callback_.Run(
          make_scoped_ptr(new StreamReaderJobDelegateImpl(response.Pass())));
    } else {
      callback_.Run(nullptr);
    }
  }

  scoped_ptr<AwContentsIoThreadClient> io_thread_client_;
  Callback callback_;
  base::WeakPtrFactory<ShouldInterceptRequestAdaptor> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ShouldInterceptRequestAdaptor);
};

}  // namespace

AwRequestInterceptor::AwRequestInterceptor() {}

AwRequestInterceptor::~AwRequestInterceptor() {}

net::URLRequestJob* AwRequestInterceptor::MaybeInterceptRequest(
    net::URLRequest* request,
    net::NetworkDelegate* network_delegate) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  // MaybeInterceptRequest can be called multiple times for the same request.
  if (request->GetUserData(kRequestAlreadyHasJobDataKey))
    return nullptr;

  int render_process_id, render_frame_id;
  if (!content::ResourceRequestInfo::GetRenderFrameForRequest(
          request, &render_process_id, &render_frame_id)) {
    return nullptr;
  }

  scoped_ptr<AwContentsIoThreadClient> io_thread_client =
      AwContentsIoThreadClient::FromID(render_process_id, render_frame_id);

  if (!io_thread_client)
    return nullptr;

  GURL referrer(request->referrer());
  if (referrer.is_valid() &&
      (!request->is_pending() || request->is_redirecting())) {
    request->SetExtraRequestHeaderByName(net::HttpRequestHeaders::kReferer,
                                         referrer.spec(), true);
  }
  request->SetUserData(kRequestAlreadyHasJobDataKey,
                       new base::SupportsUserData::Data());
  return new AndroidStreamReaderURLRequestJob(
      request, network_delegate,
      make_scoped_ptr(
          new ShouldInterceptRequestAdaptor(io_thread_client.Pass())),
      true);
}

}  // namespace android_webview
