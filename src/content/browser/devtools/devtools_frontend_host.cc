// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/devtools/devtools_frontend_host.h"

#include "content/browser/devtools/devtools_manager_impl.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/common/devtools_messages.h"
#include "content/public/browser/devtools_client_host.h"
#include "content/public/browser/devtools_frontend_host_delegate.h"

namespace content {

// static
DevToolsClientHost* DevToolsClientHost::CreateDevToolsFrontendHost(
    WebContents* client_web_contents,
    DevToolsFrontendHostDelegate* delegate) {
  return new DevToolsFrontendHost(
      static_cast<WebContentsImpl*>(client_web_contents), delegate);
}

// static
void DevToolsClientHost::SetupDevToolsFrontendClient(
    RenderViewHost* frontend_rvh) {
  frontend_rvh->Send(new DevToolsMsg_SetupDevToolsClient(
      frontend_rvh->GetRoutingID()));
}

DevToolsFrontendHost::DevToolsFrontendHost(
    WebContentsImpl* web_contents,
    DevToolsFrontendHostDelegate* delegate)
    : WebContentsObserver(web_contents),
      delegate_(delegate) {
}

DevToolsFrontendHost::~DevToolsFrontendHost() {
  DevToolsManager::GetInstance()->ClientHostClosing(this);
}

void DevToolsFrontendHost::DispatchOnInspectorFrontend(
    const std::string& message) {
  if (!web_contents())
    return;
  RenderViewHostImpl* target_host =
      static_cast<RenderViewHostImpl*>(web_contents()->GetRenderViewHost());
  target_host->Send(new DevToolsClientMsg_DispatchOnInspectorFrontend(
      target_host->GetRoutingID(),
      message));
}

void DevToolsFrontendHost::InspectedContentsClosing() {
  delegate_->InspectedContentsClosing();
}

void DevToolsFrontendHost::ReplacedWithAnotherClient() {
}

bool DevToolsFrontendHost::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(DevToolsFrontendHost, message)
    IPC_MESSAGE_HANDLER(DevToolsAgentMsg_DispatchOnInspectorBackend,
                        OnDispatchOnInspectorBackend)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_ActivateWindow, OnActivateWindow)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_ChangeAttachedWindowHeight,
                        OnChangeAttachedWindowHeight)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_CloseWindow, OnCloseWindow)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_MoveWindow, OnMoveWindow)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_RequestSetDockSide,
                        OnRequestSetDockSide)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_OpenInNewTab, OnOpenInNewTab)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_Save, OnSave)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_Append, OnAppend)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_RequestFileSystems,
                        OnRequestFileSystems)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_AddFileSystem, OnAddFileSystem)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_RemoveFileSystem, OnRemoveFileSystem)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_IndexPath, OnIndexPath)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_StopIndexing, OnStopIndexing)
    IPC_MESSAGE_HANDLER(DevToolsHostMsg_SearchInPath, OnSearchInPath)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void DevToolsFrontendHost::RenderProcessGone(
    base::TerminationStatus status) {
  switch(status) {
    case base::TERMINATION_STATUS_ABNORMAL_TERMINATION:
    case base::TERMINATION_STATUS_PROCESS_WAS_KILLED:
    case base::TERMINATION_STATUS_PROCESS_CRASHED:
      DevToolsManager::GetInstance()->ClientHostClosing(this);
      break;
    default:
      break;
  }
}

void DevToolsFrontendHost::OnDispatchOnInspectorBackend(
    const std::string& message) {
  DevToolsManagerImpl::GetInstance()->DispatchOnInspectorBackend(this, message);
//  delegate_->DispatchOnInspectorBackend(message);
}

void DevToolsFrontendHost::OnActivateWindow() {
  delegate_->ActivateWindow();
}

void DevToolsFrontendHost::OnChangeAttachedWindowHeight(unsigned height) {
  delegate_->ChangeAttachedWindowHeight(height);
}

void DevToolsFrontendHost::OnCloseWindow() {
  delegate_->CloseWindow();
}

void DevToolsFrontendHost::OnMoveWindow(int x, int y) {
  delegate_->MoveWindow(x, y);
}

void DevToolsFrontendHost::OnOpenInNewTab(const std::string& url) {
  delegate_->OpenInNewTab(url);
}

void DevToolsFrontendHost::OnSave(
    const std::string& url,
    const std::string& content,
    bool save_as) {
  delegate_->SaveToFile(url, content, save_as);
}

void DevToolsFrontendHost::OnAppend(
    const std::string& url,
    const std::string& content) {
  delegate_->AppendToFile(url, content);
}

void DevToolsFrontendHost::OnRequestFileSystems() {
  delegate_->RequestFileSystems();
}

void DevToolsFrontendHost::OnAddFileSystem() {
  delegate_->AddFileSystem();
}

void DevToolsFrontendHost::OnRemoveFileSystem(
    const std::string& file_system_path) {
  delegate_->RemoveFileSystem(file_system_path);
}

void DevToolsFrontendHost::OnIndexPath(int request_id,
                                       const std::string& file_system_path) {
  delegate_->IndexPath(request_id, file_system_path);
}

void DevToolsFrontendHost::OnStopIndexing(int request_id) {
  delegate_->StopIndexing(request_id);
}

void DevToolsFrontendHost::OnSearchInPath(int request_id,
                                          const std::string& file_system_path,
                                          const std::string& query) {
  delegate_->SearchInPath(request_id, file_system_path, query);
}

void DevToolsFrontendHost::OnRequestSetDockSide(const std::string& side) {
  delegate_->SetDockSide(side);
}

}  // namespace content
