// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SyncCallbacks_h
#define SyncCallbacks_h

#include "platform/heap/Handle.h"
#include "public/platform/WebCallbacks.h"
#include "public/platform/modules/background_sync/WebSyncProvider.h"
#include "public/platform/modules/background_sync/WebSyncRegistration.h"
#include "wtf/Noncopyable.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefPtr.h"

namespace blink {

class ServiceWorkerRegistration;
class ScriptPromiseResolver;
struct WebSyncError;
struct WebSyncRegistration;
class WebString;

// SyncRegistrationCallbacks is an implementation of WebSyncRegistrationCallbacks
// that will resolve the underlying promise depending on the result passed to
// the callback. It takes a ServiceWorkerRegistration in its constructor and
// will pass it to the SyncRegistration.
class SyncRegistrationCallbacks final : public WebSyncRegistrationCallbacks {
    WTF_MAKE_NONCOPYABLE(SyncRegistrationCallbacks);
    // FIXME(tasak): when making public/platform classes to use PartitionAlloc,
    // the following macro should be moved to WebCallbacks defined in public/platformWebCallbacks.h.
    USING_FAST_MALLOC(SyncRegistrationCallbacks);
public:
    SyncRegistrationCallbacks(ScriptPromiseResolver*, ServiceWorkerRegistration*);
    ~SyncRegistrationCallbacks() override;

    void onSuccess(WebPassOwnPtr<WebSyncRegistration>) override;
    void onError(const WebSyncError&) override;

private:
    Persistent<ScriptPromiseResolver> m_resolver;
    Persistent<ServiceWorkerRegistration> m_serviceWorkerRegistration;
};

// SyncNotifyWhenDoneCallbacks is an implementation of
// WebSyncNotifyWhenDoneCallbacks that will resolve the underlying promise
// depending on the result passed to the callback. It takes a
// ServiceWorkerRegistration in its constructor and will pass it to the
// SyncProvider.
class SyncNotifyWhenFinishedCallbacks final : public WebSyncNotifyWhenFinishedCallbacks {
    WTF_MAKE_NONCOPYABLE(SyncNotifyWhenFinishedCallbacks);
    // FIXME(tasak): when making public/platform classes to use PartitionAlloc,
    // the following macro should be moved to WebCallbacks defined in public/platformWebCallbacks.h.
    USING_FAST_MALLOC(SyncNotifyWhenFinishedCallbacks);

public:
    SyncNotifyWhenFinishedCallbacks(ScriptPromiseResolver*, ServiceWorkerRegistration*);
    ~SyncNotifyWhenFinishedCallbacks() override;

    void onSuccess() override;
    void onError(const WebSyncError&) override;

private:
    Persistent<ScriptPromiseResolver> m_resolver;
    Persistent<ServiceWorkerRegistration> m_serviceWorkerRegistration;
};

// SyncUnregistrationCallbacks is an implementation of
// WebSyncUnregistrationCallbacks that will resolve the underlying promise
// depending on the result passed to the callback. It takes a
// ServiceWorkerRegistration in its constructor and will pass it to the
// SyncProvider.
class SyncUnregistrationCallbacks final : public WebSyncUnregistrationCallbacks {
    WTF_MAKE_NONCOPYABLE(SyncUnregistrationCallbacks);
    // FIXME(tasak): when making public/platform classes to use PartitionAlloc,
    // the following macro should be moved to WebCallbacks defined in public/platformWebCallbacks.h.
    USING_FAST_MALLOC(SyncUnregistrationCallbacks);
public:
    SyncUnregistrationCallbacks(ScriptPromiseResolver*, ServiceWorkerRegistration*);
    ~SyncUnregistrationCallbacks() override;

    void onSuccess(bool) override;
    void onError(const WebSyncError&) override;

private:
    Persistent<ScriptPromiseResolver> m_resolver;
    Persistent<ServiceWorkerRegistration> m_serviceWorkerRegistration;
};

// SyncGetRegistrationsCallbacks is an implementation of WebSyncGetRegistrationsCallbacks
// that will resolve the underlying promise depending on the result passed to
// the callback. It takes a ServiceWorkerRegistration in its constructor and
// will pass it to the SyncRegistration.
class SyncGetRegistrationsCallbacks final : public WebSyncGetRegistrationsCallbacks {
    WTF_MAKE_NONCOPYABLE(SyncGetRegistrationsCallbacks);
    // FIXME(tasak): when making public/platform classes to use PartitionAlloc,
    // the following macro should be moved to WebCallbacks defined in public/platformWebCallbacks.h.
    USING_FAST_MALLOC(SyncGetRegistrationsCallbacks);
public:
    SyncGetRegistrationsCallbacks(ScriptPromiseResolver*, ServiceWorkerRegistration*);
    ~SyncGetRegistrationsCallbacks() override;

    void onSuccess(const WebVector<WebSyncRegistration*>&) override;
    void onError(const WebSyncError&) override;

private:
    Persistent<ScriptPromiseResolver> m_resolver;
    Persistent<ServiceWorkerRegistration> m_serviceWorkerRegistration;
};

// SyncGetPermissionStatusCallbacks is an implementation of
// WebSyncGetPermissionStatusCallbacks that will resolve the underlying promise
// depending on the permission status passed to the callback.
class SyncGetPermissionStatusCallbacks final : public WebSyncGetPermissionStatusCallbacks {
    WTF_MAKE_NONCOPYABLE(SyncGetPermissionStatusCallbacks);
    USING_FAST_MALLOC(SyncGetPermissionStatusCallbacks);
public:
    SyncGetPermissionStatusCallbacks(ScriptPromiseResolver*, ServiceWorkerRegistration*);
    ~SyncGetPermissionStatusCallbacks() override;

    void onSuccess(WebSyncPermissionStatus) override;
    void onError(const WebSyncError&) override;

private:
    static String permissionString(WebSyncPermissionStatus);

    Persistent<ScriptPromiseResolver> m_resolver;
    Persistent<ServiceWorkerRegistration> m_serviceWorkerRegistration;
};

} // namespace blink

#endif // SyncCallbacks_h
