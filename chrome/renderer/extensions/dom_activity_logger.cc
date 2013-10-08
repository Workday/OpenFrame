// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/dom_activity_logger.h"

#include "base/logging.h"
#include "chrome/common/extensions/dom_action_types.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/renderer/chrome_render_process_observer.h"
#include "chrome/renderer/extensions/activity_log_converter_strategy.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/v8_value_converter.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/web/WebDOMActivityLogger.h"
#include "v8/include/v8.h"

using content::V8ValueConverter;

namespace extensions {

DOMActivityLogger::DOMActivityLogger(const std::string& extension_id,
                                     const GURL& url,
                                     const string16& title)
  : extension_id_(extension_id), url_(url), title_(title) {
}  // namespace extensions

void DOMActivityLogger::log(
    const WebString& api_name,
    int argc,
    const v8::Handle<v8::Value> argv[],
    const WebString& call_type) {
  scoped_ptr<V8ValueConverter> converter(V8ValueConverter::create());
  ActivityLogConverterStrategy strategy;
  converter->SetFunctionAllowed(true);
  converter->SetStrategy(&strategy);
  scoped_ptr<ListValue> argv_list_value(new ListValue());
  for (int i =0; i < argc; i++) {
    argv_list_value->Set(
        i, converter->FromV8Value(argv[i], v8::Context::GetCurrent()));
  }
  ExtensionHostMsg_DOMAction_Params params;
  params.url = url_;
  params.url_title = title_;
  params.api_call = api_name.utf8();
  params.arguments.Swap(argv_list_value.get());
  const std::string type = call_type.utf8();
  if (type == "Getter")
    params.call_type = DomActionType::GETTER;
  else if (type == "Setter")
    params.call_type = DomActionType::SETTER;
  else
    params.call_type = DomActionType::METHOD;

  content::RenderThread::Get()->Send(
      new ExtensionHostMsg_AddDOMActionToActivityLog(extension_id_, params));
}

void DOMActivityLogger::AttachToWorld(int world_id,
                                      const std::string& extension_id,
                                      const GURL& url,
                                      const string16& title) {
  // Check if extension activity logging is enabled.
  if (!ChromeRenderProcessObserver::extension_activity_log_enabled())
    return;
  // If there is no logger registered for world_id, construct a new logger
  // and register it with world_id.
  if (!WebKit::hasDOMActivityLogger(world_id)) {
    DOMActivityLogger* logger = new DOMActivityLogger(extension_id, url, title);
    WebKit::setDOMActivityLogger(world_id, logger);
  }
}

}  // namespace extensions

