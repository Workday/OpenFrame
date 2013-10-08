// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_PEPPER_PEPPER_EXTENSIONS_COMMON_HOST_H_
#define CHROME_RENDERER_PEPPER_PEPPER_EXTENSIONS_COMMON_HOST_H_

#include <map>
#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/linked_ptr.h"
#include "chrome/renderer/extensions/request_sender.h"
#include "ppapi/host/resource_host.h"

namespace base {
class ListValue;
}

namespace content {
class RendererPpapiHost;
}

namespace ppapi {
namespace host {
struct ReplyMessageContext;
}
}

namespace extensions {
class Dispatcher;
}

namespace chrome {

class PepperExtensionsCommonHost : public ppapi::host::ResourceHost,
                                   public extensions::RequestSender::Source {
 public:
  virtual ~PepperExtensionsCommonHost();

  static PepperExtensionsCommonHost* Create(content::RendererPpapiHost* host,
                                            PP_Instance instance,
                                            PP_Resource resource);

  // ppapi::host::ResourceMessageHandler overrides.
  virtual int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) OVERRIDE;

  // extensions::RequestSender::Source implementation.
  virtual extensions::ChromeV8Context* GetContext() OVERRIDE;
  virtual void OnResponseReceived(const std::string& name,
                                  int request_id,
                                  bool success,
                                  const base::ListValue& response,
                                  const std::string& error) OVERRIDE;
 private:
  typedef std::map<int, linked_ptr<ppapi::host::ReplyMessageContext> >
      PendingRequestMap;

  PepperExtensionsCommonHost(content::RendererPpapiHost* host,
                             PP_Instance instance,
                             PP_Resource resource,
                             extensions::Dispatcher* dispatcher);

  int32_t OnPost(ppapi::host::HostMessageContext* context,
                 const std::string& request_name,
                 base::ListValue& args);

  int32_t OnCall(ppapi::host::HostMessageContext* context,
                 const std::string& request_name,
                 base::ListValue& args);

  // Non-owning pointer.
  content::RendererPpapiHost* renderer_ppapi_host_;
  // Non-owning pointer.
  extensions::Dispatcher* dispatcher_;

  PendingRequestMap pending_request_map_;

  DISALLOW_COPY_AND_ASSIGN(PepperExtensionsCommonHost);
};

}  // namespace chrome

#endif  // CHROME_RENDERER_PEPPER_PEPPER_EXTENSIONS_COMMON_HOST_H_
