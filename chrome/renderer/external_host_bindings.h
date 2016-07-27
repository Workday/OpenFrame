// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_EXTERNAL_HOST_BINDINGS_H_
#define CHROME_RENDERER_EXTERNAL_HOST_BINDINGS_H_

#include "ipc/ipc_sender.h"
#include "gin/wrappable.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "webkit/renderer/cpp_bound_class.h"

struct NPObject;

namespace gin {
class Arguments;
}

// ExternalHostBindings is the class backing the "externalHost" object
// accessible from Javascript
//
// We expose one function, for sending a message to the external host:
//  postMessage(String message[, String target]);
//joek: Removed CppBoundClass references as that will require updating an bit of the stack (gypi and more)
class ExternalHostBindings : public gin::Wrappable<ExternalHostBindings>/*, public webkit_glue::CppBoundClass*/ {
 public:
  ExternalHostBindings(IPC::Sender* sender, int routing_id);
  static gin::WrapperInfo kWrapperInfo;
  virtual ~ExternalHostBindings();

  // Invokes the registered onmessage handler.
  // Returns true on successful invocation.
  bool ForwardMessageFromExternalHost(const std::string& message,
                                      const std::string& origin,
                                      const std::string& target);

  // Overridden to hold onto a pointer back to the web frame.
  void BindToJavascript(blink::WebFrame* frame, const std::string& classname);
  
protected:
  // gin::Wrappable method:
  virtual gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
	  v8::Isolate* isolate) override;

 private:
  //scoped_ptr<NPP_t> npp_;
  // Creates an uninitialized instance of a MessageEvent object.
  // This is equivalent to calling window.document.createEvent("MessageEvent")
  // in javascript.
  bool CreateMessageEvent(NPObject** message_event);

  // The postMessage() function provided to Javascript.
  void PostMessageW(/*const webkit_glue::CppArgumentList& args, webkit_glue::CppVariant* result*/);

  NPObject* on_message_handler_;

  blink::WebFrame* frame_;
  IPC::Sender* sender_;

  DISALLOW_COPY_AND_ASSIGN(ExternalHostBindings);
};

#endif  // CHROME_RENDERER_EXTERNAL_HOST_BINDINGS_H_
