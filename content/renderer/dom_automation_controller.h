// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_DOM_AUTOMATION_CONTROLLER_H_
#define CONTENT_RENDERER_DOM_AUTOMATION_CONTROLLER_H_

#include "ipc/ipc_sender.h"
#include "webkit/renderer/cpp_bound_class.h"

/* DomAutomationController class:
   Bound to Javascript window.domAutomationController object.
   At the very basic, this object makes any native value (string, numbers,
   boolean) from javascript available to the automation host in Cpp.
   Any renderer implementation that is built with this binding will allow the
   above facility.
   The intended use of this object is to expose the DOM Objects and their
   attributes to the automation host.

   A typical usage would be like following (JS code):

   var object = document.getElementById('some_id');
   window.domAutomationController.send(object.nodeName); // get the tag name

   For the exact mode of usage,
   refer AutomationProxyTest.*DomAutomationController tests.

   The class provides a single send method that can send variety of native
   javascript values. (NPString, Number(double), Boolean)

   The actual communication occurs in the following manner:

    TEST            MASTER          RENDERER
              (1)             (3)
   |AProxy| ----->|AProvider|----->|RenderView|------|
      /\                |               |            |
      |                 |               |            |
      |(6)              |(2)            |(0)         |(4)
      |                 |               \/           |
      |                 |-------->|DAController|<----|
      |                                 |
      |                                 |(5)
      |-------|WebContentsImpl|<--------|


   Legends:
   - AProxy = AutomationProxy
   - AProvider = AutomationProvider
   - DAController = DomAutomationController

   (0) Initialization step where DAController is bound to the renderer
       and the view_id of the renderer is supplied to the DAController for
       routing message in (5). (routing_id_)
   (1) A 'javascript:' url is sent from the test process to master as an IPC
       message. A unique routing id is generated at this stage (automation_id_)
   (2) The automation_id_ of step (1) is supplied to DAController by calling
       the bound method setAutomationId(). This is required for routing message
       in (6).
   (3) The 'javascript:' url is sent for execution by calling into
       Browser::LoadURL()
   (4) A callback is generated as a result of domAutomationController.send()
       into Cpp. The supplied value is received as a result of this callback.
   (5) The value received in (4) is sent to the master along with the
       stored automation_id_ as an IPC message. routing_id_ is used to route
       the message. (IPC messages, ViewHostMsg_*DomAutomation* )
   (6) The value and the automation_id_ is extracted out of the message received
       in (5). This value is relayed to AProxy using another IPC message.
       automation_id_ is used to route the message.
       (IPC messages, AutomationMsg_Dom*Response)

*/

namespace content {

// TODO(vibhor): Add another method-pair like sendLater() and sendNow()
// sendLater() should keep building a json serializer
// sendNow() should send the above serializer as a string.
class DomAutomationController : public webkit_glue::CppBoundClass {
 public:
  DomAutomationController();

  // Makes the renderer send a javascript value to the app.
  // The value to be sent can be either of type NPString,
  // Number (double casted to int32) or boolean.
  // The function returns true/false based on the result of actual send over
  // IPC. It sets the return value to null on unexpected errors or arguments.
  void Send(const webkit_glue::CppArgumentList& args,
            webkit_glue::CppVariant* result);

  // Makes the renderer send a javascript value to the app.
  // The value must be a NPString and should be properly formed JSON.
  // This function does not modify/escape the returned string in any way.
  void SendJSON(const webkit_glue::CppArgumentList& args,
                webkit_glue::CppVariant* result);

  // Sends a string with a provided Automation Id.
  // Expects two javascript values; the first must be a number type and will be
  // used as the Automation Id, the second must be of type NPString.
  // The function returns true/false to the javascript based on the success
  // of the send over IPC. It sets the javascript return value to null on
  // unexpected errors or arguments.
  void SendWithId(const webkit_glue::CppArgumentList& args,
                  webkit_glue::CppVariant* result);

  void SetAutomationId(const webkit_glue::CppArgumentList& args,
                       webkit_glue::CppVariant* result);

  // TODO(vibhor): Implement later
  // static CppBindingObjectMethod sendLater;
  // static CppBindingObjectMethod sendNow;

  void set_routing_id(int routing_id) { routing_id_ = routing_id; }

  void set_message_sender(IPC::Sender* sender) {
    sender_ = sender;
  }

 private:
  IPC::Sender* sender_;

  // Refer to the comments at the top of the file for more details.
  int routing_id_;  // routing id to be used by first channel.
  int automation_id_;  // routing id to be used by the next channel.
};

}  // namespace content

#endif  // CONTENT_RENDERER_DOM_AUTOMATION_CONTROLLER_H_
