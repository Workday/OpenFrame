// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/peer_connection_identity_store.h"

#include "base/bind.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/thread_task_runner_handle.h"
#include "content/renderer/media/webrtc_identity_service.h"
#include "content/renderer/render_thread_impl.h"

namespace content {
namespace {

const char kIdentityName[] = "WebRTC";
static unsigned int kRSAChromiumKeyLength = 1024;
static unsigned int kRSAChromiumPubExp = 0x10001;

// Bridges identity requests between the main render thread and libjingle's
// signaling thread.
class RequestHandler : public base::RefCountedThreadSafe<RequestHandler> {
 public:
  explicit RequestHandler(
      const scoped_refptr<base::SingleThreadTaskRunner>& main_thread,
      const scoped_refptr<base::SingleThreadTaskRunner>& signaling_thread,
      webrtc::DtlsIdentityRequestObserver* observer)
      : main_thread_(main_thread),
        signaling_thread_(signaling_thread),
        observer_(observer) {
    DCHECK(main_thread_);
    DCHECK(signaling_thread_);
    DCHECK(observer_);
  }

  void RequestIdentityOnMainThread(const GURL& url,
                                   const GURL& first_party_for_cookies) {
    DCHECK(main_thread_->BelongsToCurrentThread());
    int request_id =
        RenderThreadImpl::current()
            ->get_webrtc_identity_service()
            ->RequestIdentity(
                url, first_party_for_cookies, kIdentityName, kIdentityName,
                base::Bind(&RequestHandler::OnIdentityReady, this),
                base::Bind(&RequestHandler::OnRequestFailed, this));
    DCHECK_NE(request_id, 0);
  }

 private:
  friend class base::RefCountedThreadSafe<RequestHandler>;
  ~RequestHandler() {
    DCHECK(!observer_);
  }

  void OnIdentityReady(
      const std::string& certificate,
      const std::string& private_key) {
    DCHECK(main_thread_->BelongsToCurrentThread());
    signaling_thread_->PostTask(FROM_HERE,
        base::Bind(static_cast<void (webrtc::DtlsIdentityRequestObserver::*)(
                       const std::string&, const std::string&)>(
                           &webrtc::DtlsIdentityRequestObserver::OnSuccess),
                   observer_, certificate, private_key));
    signaling_thread_->PostTask(FROM_HERE,
        base::Bind(&RequestHandler::EnsureReleaseObserverOnSignalingThread,
            this));
  }

  void OnRequestFailed(int error) {
    DCHECK(main_thread_->BelongsToCurrentThread());
    signaling_thread_->PostTask(FROM_HERE,
        base::Bind(&webrtc::DtlsIdentityRequestObserver::OnFailure, observer_,
                   error));
    signaling_thread_->PostTask(FROM_HERE,
        base::Bind(&RequestHandler::EnsureReleaseObserverOnSignalingThread,
            this));
  }

  void EnsureReleaseObserverOnSignalingThread() {
    DCHECK(signaling_thread_->BelongsToCurrentThread());
    observer_ = nullptr;
  }

  const scoped_refptr<base::SingleThreadTaskRunner> main_thread_;
  const scoped_refptr<base::SingleThreadTaskRunner> signaling_thread_;
  scoped_refptr<webrtc::DtlsIdentityRequestObserver> observer_;
};

// Helper function for PeerConnectionIdentityStore::RequestIdentity.
// Used to invoke |observer|->OnSuccess in a PostTask.
void ObserverOnSuccess(
    const rtc::scoped_refptr<webrtc::DtlsIdentityRequestObserver>& observer,
    scoped_ptr<rtc::SSLIdentity> identity) {
  observer->OnSuccess(rtc::scoped_ptr<rtc::SSLIdentity>(identity.release()));
}

}  // namespace

PeerConnectionIdentityStore::PeerConnectionIdentityStore(
    const scoped_refptr<base::SingleThreadTaskRunner>& main_thread,
    const scoped_refptr<base::SingleThreadTaskRunner>& signaling_thread,
    const GURL& url,
    const GURL& first_party_for_cookies)
    : main_thread_(main_thread),
      signaling_thread_(signaling_thread),
      url_(url),
      first_party_for_cookies_(first_party_for_cookies) {
  DCHECK(main_thread_);
  DCHECK(signaling_thread_);
}

PeerConnectionIdentityStore::~PeerConnectionIdentityStore() {
  // Typically destructed on libjingle's signaling thread.
}

void PeerConnectionIdentityStore::RequestIdentity(
    rtc::KeyParams key_params,
    const rtc::scoped_refptr<webrtc::DtlsIdentityRequestObserver>& observer) {
  DCHECK(signaling_thread_->BelongsToCurrentThread());
  DCHECK(observer);

  // TODO(torbjorng): crbug.com/544902. RequestIdentityOnUIThread uses Chromium
  // key generation code with the assumption that it will generate with the
  // following rsa_params(). This assumption should not be implicit! Either pass
  // the parameters along or check against constants exported from relevant
  // header file(s).
  if (key_params.type() == rtc::KT_RSA &&
      key_params.rsa_params().mod_size == kRSAChromiumKeyLength &&
      key_params.rsa_params().pub_exp == kRSAChromiumPubExp) {
    // Use Chromium identity generation code for its hardwired parameters (RSA,
    // 1024, 0x10001). This generation code is preferred over WebRTC generation
    // code due to the performance benefits of caching.
    scoped_refptr<RequestHandler> handler(
        new RequestHandler(main_thread_, signaling_thread_, observer));
    main_thread_->PostTask(
        FROM_HERE,
        base::Bind(&RequestHandler::RequestIdentityOnMainThread, handler, url_,
                   first_party_for_cookies_));
  } else {
    // Fall back on WebRTC identity generation code for everything else, e.g.
    // RSA with any other parameters or ECDSA. These will not be cached.
    scoped_ptr<rtc::SSLIdentity> identity(rtc::SSLIdentity::Generate(
        kIdentityName, key_params));

    // Invoke |observer| callbacks asynchronously. The callbacks of
    // DtlsIdentityStoreInterface implementations have to be async.
    if (identity) {
      // Async call to |observer|->OnSuccess.
      // Helper function necessary because OnSuccess takes an rtc::scoped_ptr
      // argument which has to be Pass()-ed. base::Passed gets around this for
      // scoped_ptr (without rtc namespace), but not for rtc::scoped_ptr.
      signaling_thread_->PostTask(FROM_HERE,
          base::Bind(&ObserverOnSuccess, observer, base::Passed(&identity)));
    } else {
      // Async call to |observer|->OnFailure.
      signaling_thread_->PostTask(FROM_HERE,
          base::Bind(&webrtc::DtlsIdentityRequestObserver::OnFailure,
                     observer, 0));
    }
  }
}

}  // namespace content
