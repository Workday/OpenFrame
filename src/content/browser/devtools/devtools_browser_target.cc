// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/devtools/devtools_browser_target.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/values.h"
#include "content/public/browser/browser_thread.h"
#include "net/server/http_server.h"

namespace content {

DevToolsBrowserTarget::DevToolsBrowserTarget(
    base::MessageLoopProxy* message_loop_proxy,
    net::HttpServer* http_server,
    int connection_id)
    : message_loop_proxy_(message_loop_proxy),
      http_server_(http_server),
      connection_id_(connection_id),
      handlers_deleter_(&handlers_),
      weak_factory_(this) {
}

void DevToolsBrowserTarget::RegisterDomainHandler(
    const std::string& domain,
    DevToolsProtocol::Handler* handler,
    bool handle_on_ui_thread) {
  DCHECK(handlers_.find(domain) == handlers_.end());
  handlers_[domain] = handler;
  if (handle_on_ui_thread) {
    handle_on_ui_thread_.insert(domain);
    handler->SetNotifier(base::Bind(&DevToolsBrowserTarget::RespondFromUIThread,
                                    weak_factory_.GetWeakPtr()));
  } else {
    handler->SetNotifier(base::Bind(&DevToolsBrowserTarget::Respond,
                                    base::Unretained(this)));
  }
}

void DevToolsBrowserTarget::HandleMessage(const std::string& data) {
  std::string error_response;
  scoped_refptr<DevToolsProtocol::Command> command =
      DevToolsProtocol::ParseCommand(data, &error_response);
  if (!command) {
    Respond(error_response);
    return;
  }

  DomainHandlerMap::iterator it = handlers_.find(command->domain());
  if (it == handlers_.end()) {
    Respond(command->NoSuchMethodErrorResponse()->Serialize());
    return;
  }

  DevToolsProtocol::Handler* handler = it->second;
  bool handle_directly = handle_on_ui_thread_.find(command->domain()) ==
      handle_on_ui_thread_.end();
  if (handle_directly) {
    scoped_refptr<DevToolsProtocol::Response> response =
        handler->HandleCommand(command);
    if (response && response->is_async_promise())
      return;
    if (response)
      Respond(response->Serialize());
    else
      Respond(command->NoSuchMethodErrorResponse()->Serialize());
    return;
  }

  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&DevToolsBrowserTarget::HandleCommandOnUIThread,
                 this,
                 handler,
                 command));
}

void DevToolsBrowserTarget::Detach() {
  message_loop_proxy_ = NULL;
  http_server_ = NULL;

  std::vector<DevToolsProtocol::Handler*> ui_handlers;
  for (std::set<std::string>::iterator domain_it = handle_on_ui_thread_.begin();
       domain_it != handle_on_ui_thread_.end();
       ++domain_it) {
    DomainHandlerMap::iterator handler_it = handlers_.find(*domain_it);
    CHECK(handler_it != handlers_.end());
    ui_handlers.push_back(handler_it->second);
    handlers_.erase(handler_it);
  }

  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&DevToolsBrowserTarget::DeleteHandlersOnUIThread,
                 this,
                 ui_handlers));
}

DevToolsBrowserTarget::~DevToolsBrowserTarget() {
}

void DevToolsBrowserTarget::HandleCommandOnUIThread(
    DevToolsProtocol::Handler* handler,
    scoped_refptr<DevToolsProtocol::Command> command) {
  scoped_refptr<DevToolsProtocol::Response> response =
      handler->HandleCommand(command);
  if (response && response->is_async_promise())
    return;

  if (response)
    RespondFromUIThread(response->Serialize());
  else
    RespondFromUIThread(command->NoSuchMethodErrorResponse()->Serialize());
}

void DevToolsBrowserTarget::DeleteHandlersOnUIThread(
    std::vector<DevToolsProtocol::Handler*> handlers) {
  STLDeleteElements(&handlers);
}

void DevToolsBrowserTarget::Respond(const std::string& message) {
  if (!http_server_)
    return;
  http_server_->SendOverWebSocket(connection_id_, message);
}

void DevToolsBrowserTarget::RespondFromUIThread(const std::string& message) {
  if (!message_loop_proxy_)
    return;
  message_loop_proxy_->PostTask(
      FROM_HERE,
      base::Bind(&DevToolsBrowserTarget::Respond, this, message));
}

}  // namespace content
