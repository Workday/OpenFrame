/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* From ppb_messaging.idl modified Wed Jun  5 10:32:59 2013. */

#ifndef PPAPI_C_PPB_MESSAGING_H_
#define PPAPI_C_PPB_MESSAGING_H_

#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_macros.h"
#include "ppapi/c/pp_stdint.h"
#include "ppapi/c/pp_var.h"

#define PPB_MESSAGING_INTERFACE_1_0 "PPB_Messaging;1.0"
#define PPB_MESSAGING_INTERFACE PPB_MESSAGING_INTERFACE_1_0

/**
 * @file
 * This file defines the <code>PPB_Messaging</code> interface implemented
 * by the browser for sending messages to DOM elements associated with a
 * specific module instance.
 */


/**
 * @addtogroup Interfaces
 * @{
 */
/**
 * The <code>PPB_Messaging</code> interface is implemented by the browser
 * and is related to sending messages to JavaScript message event listeners on
 * the DOM element associated with specific module instance.
 */
struct PPB_Messaging_1_0 {
  /**
   * PostMessage() asynchronously invokes any listeners for message events on
   * the DOM element for the given module instance. A call to PostMessage()
   * will not block while the message is processed.
   *
   * @param[in] instance A <code>PP_Instance</code> identifying one instance
   * of a module.
   * @param[in] message A <code>PP_Var</code> containing the data to be sent to
   * JavaScript.
   * <code>message</code> can be any <code>PP_Var</code> type except
   * <code>PP_VARTYPE_OBJECT</code>. Array/Dictionary types are supported from
   * Chrome M29 onward. All var types are copied when passing them to
   * JavaScript.
   *
   * When passing array or dictionary <code>PP_Var</code>s, the entire reference
   * graph will be converted and transferred. If the reference graph has cycles,
   * the message will not be sent and an error will be logged to the console.
   *
   * Listeners for message events in JavaScript code will receive an object
   * conforming to the HTML 5 <code>MessageEvent</code> interface.
   * Specifically, the value of message will be contained as a property called
   *  data in the received <code>MessageEvent</code>.
   *
   * This messaging system is similar to the system used for listening for
   * messages from Web Workers. Refer to
   * <code>http://www.whatwg.org/specs/web-workers/current-work/</code> for
   * further information.
   *
   * <strong>Example:</strong>
   *
   * @code
   *
   * <body>
   *   <object id="plugin"
   *           type="application/x-ppapi-postMessage-example"/>
   *   <script type="text/javascript">
   *     var plugin = document.getElementById('plugin');
   *     plugin.addEventListener("message",
   *                             function(message) { alert(message.data); },
   *                             false);
   *   </script>
   * </body>
   *
   * @endcode
   *
   * The module instance then invokes PostMessage() as follows:
   *
   * @code
   *
   *  char hello_world[] = "Hello world!";
   *  PP_Var hello_var = ppb_var_interface->VarFromUtf8(instance,
   *                                                    hello_world,
   *                                                    sizeof(hello_world));
   *  ppb_messaging_interface->PostMessage(instance, hello_var); // Copies var.
   *  ppb_var_interface->Release(hello_var);
   *
   * @endcode
   *
   * The browser will pop-up an alert saying "Hello world!"
   */
  void (*PostMessage)(PP_Instance instance, struct PP_Var message);
};

typedef struct PPB_Messaging_1_0 PPB_Messaging;
/**
 * @}
 */

#endif  /* PPAPI_C_PPB_MESSAGING_H_ */

