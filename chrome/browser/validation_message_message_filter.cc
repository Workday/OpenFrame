// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/validation_message_message_filter.h"

#include "base/bind.h"
#include "chrome/browser/ui/validation_message_bubble.h"
#include "chrome/common/validation_message_messages.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host.h"

using content::BrowserThread;
using content::RenderProcessHost;
using content::RenderWidgetHost;

namespace {

void DeleteBubbleOnUIThread(chrome::ValidationMessageBubble* bubble) {
  delete bubble;
}

}

ValidationMessageMessageFilter::ValidationMessageMessageFilter(int renderer_id)
  : renderer_id_(renderer_id) {
}

ValidationMessageMessageFilter::~ValidationMessageMessageFilter() {
  if (!validation_message_bubble_)
    return;
  // ValidationMessageBubble desructor might call UI-related API.
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      base::Bind(&DeleteBubbleOnUIThread,
          validation_message_bubble_.release()));
}

bool ValidationMessageMessageFilter::OnMessageReceived(
    const IPC::Message& message, bool* message_was_ok) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(
      ValidationMessageMessageFilter, message, *message_was_ok)
    IPC_MESSAGE_HANDLER(ValidationMessageMsg_ShowValidationMessage,
                        OnShowValidationMessage)
    IPC_MESSAGE_HANDLER(ValidationMessageMsg_HideValidationMessage,
                        OnHideValidationMessage)
    IPC_MESSAGE_HANDLER(ValidationMessageMsg_MoveValidationMessage,
                        OnMoveValidationMessage)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void ValidationMessageMessageFilter::OverrideThreadForMessage(
    const IPC::Message& message, BrowserThread::ID* thread) {
  if (message.type() == ValidationMessageMsg_ShowValidationMessage::ID
      || message.type() == ValidationMessageMsg_HideValidationMessage::ID
      || message.type() == ValidationMessageMsg_MoveValidationMessage::ID)
    *thread = BrowserThread::UI;
}

void ValidationMessageMessageFilter::OnShowValidationMessage(
    int route_id, const gfx::Rect& anchor_in_root_view,
    const string16& main_text, const string16& sub_text) {
  RenderWidgetHost* widget_host =
      RenderWidgetHost::FromID(renderer_id_, route_id);
  validation_message_bubble_ = chrome::ValidationMessageBubble::CreateAndShow(
      widget_host, anchor_in_root_view, main_text, sub_text);
}

void ValidationMessageMessageFilter::OnHideValidationMessage() {
  validation_message_bubble_.reset();
}

void ValidationMessageMessageFilter::OnMoveValidationMessage(
    int route_id, const gfx::Rect& anchor_in_root_view) {
  if (!validation_message_bubble_)
      return;
  RenderWidgetHost* widget_host =
      RenderWidgetHost::FromID(renderer_id_, route_id);
  validation_message_bubble_->SetPositionRelativeToAnchor(
      widget_host, anchor_in_root_view);
}

