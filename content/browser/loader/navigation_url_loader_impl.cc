// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/loader/navigation_url_loader_impl.h"

#include "base/bind.h"
#include "base/location.h"
#include "content/browser/frame_host/navigation_request_info.h"
#include "content/browser/loader/navigation_url_loader_delegate.h"
#include "content/browser/loader/navigation_url_loader_impl_core.h"
#include "content/browser/service_worker/service_worker_navigation_handle.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/stream_handle.h"

namespace content {

NavigationURLLoaderImpl::NavigationURLLoaderImpl(
    BrowserContext* browser_context,
    scoped_ptr<NavigationRequestInfo> request_info,
    ServiceWorkerNavigationHandle* service_worker_handle,
    NavigationURLLoaderDelegate* delegate)
    : delegate_(delegate), weak_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  core_ = new NavigationURLLoaderImplCore(weak_factory_.GetWeakPtr());
  ServiceWorkerNavigationHandleCore* service_worker_handle_core =
      service_worker_handle ? service_worker_handle->core() : nullptr;
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&NavigationURLLoaderImplCore::Start, base::Unretained(core_),
                 browser_context->GetResourceContext(),
                 service_worker_handle_core, base::Passed(&request_info)));
}

NavigationURLLoaderImpl::~NavigationURLLoaderImpl() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  BrowserThread::DeleteSoon(BrowserThread::IO, FROM_HERE, core_);
  core_ = nullptr;
}

void NavigationURLLoaderImpl::FollowRedirect() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&NavigationURLLoaderImplCore::FollowRedirect,
                 base::Unretained(core_)));
}

void NavigationURLLoaderImpl::NotifyRequestRedirected(
    const net::RedirectInfo& redirect_info,
    const scoped_refptr<ResourceResponse>& response) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  delegate_->OnRequestRedirected(redirect_info, response);
}

void NavigationURLLoaderImpl::NotifyResponseStarted(
    const scoped_refptr<ResourceResponse>& response,
    scoped_ptr<StreamHandle> body) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  delegate_->OnResponseStarted(response, body.Pass());
}

void NavigationURLLoaderImpl::NotifyRequestFailed(bool in_cache,
                                                  int net_error) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  delegate_->OnRequestFailed(in_cache, net_error);
}

void NavigationURLLoaderImpl::NotifyRequestStarted(base::TimeTicks timestamp) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  delegate_->OnRequestStarted(timestamp);
}

}  // namespace content
