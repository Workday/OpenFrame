// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/browser_plugin/browser_plugin_message_filter.h"

#include "base/supports_user_data.h"
#include "content/browser/browser_plugin/browser_plugin_guest.h"
#include "content/browser/browser_plugin/browser_plugin_guest_manager.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/common/browser_plugin/browser_plugin_constants.h"
#include "content/common/browser_plugin/browser_plugin_messages.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_view_host.h"

namespace content {

BrowserPluginMessageFilter::BrowserPluginMessageFilter(int render_process_id,
                                                       bool is_guest)
    : render_process_id_(render_process_id),
      is_guest_(is_guest) {
}

BrowserPluginMessageFilter::~BrowserPluginMessageFilter() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
}

bool BrowserPluginMessageFilter::OnMessageReceived(
    const IPC::Message& message,
    bool* message_was_ok) {
  // Any message requested by a BrowserPluginGuest should be routed through
  // a BrowserPluginGuestManager.
  if (BrowserPluginGuest::ShouldForwardToBrowserPluginGuest(message)) {
    DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    BrowserPluginGuestManager* guest_manager = GetBrowserPluginGuestManager();
    if (guest_manager)
      guest_manager->OnMessageReceived(message, render_process_id_);
    // We always swallow messages destined for BrowserPluginGuestManager because
    // we're on the UI thread and fallback code is expected to be run on the IO
    // thread.
    return true;
  }
  return false;
}

void BrowserPluginMessageFilter::OnDestruct() const {
  BrowserThread::DeleteOnIOThread::Destruct(this);
}

void BrowserPluginMessageFilter::OverrideThreadForMessage(
    const IPC::Message& message, BrowserThread::ID* thread) {
  if (BrowserPluginGuest::ShouldForwardToBrowserPluginGuest(message))
    *thread = BrowserThread::UI;
}

BrowserPluginGuestManager*
    BrowserPluginMessageFilter::GetBrowserPluginGuestManager() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  RenderProcessHostImpl* host = static_cast<RenderProcessHostImpl*>(
      RenderProcessHost::FromID(render_process_id_));
  if (!host)
    return NULL;

  BrowserContext* browser_context = host->GetBrowserContext();
  return static_cast<BrowserPluginGuestManager*>(
      browser_context->GetUserData(
          browser_plugin::kBrowserPluginGuestManagerKeyName));
}

} // namespace content
