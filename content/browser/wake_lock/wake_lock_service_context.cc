// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/wake_lock/wake_lock_service_context.h"

#include "base/bind.h"
#include "content/browser/power_save_blocker_impl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/power_save_blocker.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/service_registry.h"

namespace content {

WakeLockServiceContext::WakeLockServiceContext(WebContents* web_contents)
    : WebContentsObserver(web_contents), weak_factory_(this) {}

WakeLockServiceContext::~WakeLockServiceContext() {}

void WakeLockServiceContext::CreateService(
    int render_process_id,
    int render_frame_id,
    mojo::InterfaceRequest<WakeLockService> request) {
  new WakeLockServiceImpl(weak_factory_.GetWeakPtr(), render_process_id,
                          render_frame_id, request.Pass());
}

void WakeLockServiceContext::RenderFrameDeleted(
    RenderFrameHost* render_frame_host) {
  CancelWakeLock(render_frame_host->GetProcess()->GetID(),
                 render_frame_host->GetRoutingID());
}

void WakeLockServiceContext::RequestWakeLock(int render_process_id,
                                             int render_frame_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!RenderFrameHost::FromID(render_process_id, render_frame_id))
    return;

  frames_requesting_lock_.insert(
      std::pair<int, int>(render_process_id, render_frame_id));
  UpdateWakeLock();
}

void WakeLockServiceContext::CancelWakeLock(int render_process_id,
                                            int render_frame_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  frames_requesting_lock_.erase(
      std::pair<int, int>(render_process_id, render_frame_id));
  UpdateWakeLock();
}

bool WakeLockServiceContext::HasWakeLockForTests() const {
  return wake_lock_;
}

void WakeLockServiceContext::CreateWakeLock() {
  DCHECK(!wake_lock_);
  wake_lock_ = PowerSaveBlocker::Create(
      PowerSaveBlocker::kPowerSaveBlockPreventDisplaySleep,
      PowerSaveBlocker::kReasonOther, "Wake Lock API");

  //TODO(mfomitchev): Support PowerSaveBlocker on Aura - crbug.com/546718.
#if defined(OS_ANDROID) && !defined(USE_AURA)
  // On Android, additionaly associate the blocker with this WebContents.
  DCHECK(web_contents());

  static_cast<PowerSaveBlockerImpl*>(wake_lock_.get())
      ->InitDisplaySleepBlocker(web_contents());
#endif
}

void WakeLockServiceContext::RemoveWakeLock() {
  DCHECK(wake_lock_);
  wake_lock_.reset();
}

void WakeLockServiceContext::UpdateWakeLock() {
  if (!frames_requesting_lock_.empty()) {
    if (!wake_lock_)
      CreateWakeLock();
  } else {
    if (wake_lock_)
      RemoveWakeLock();
  }
}

}  // namespace content
