// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_DEVICE_ID_HOST_H_
#define CHROME_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_DEVICE_ID_HOST_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/renderer_host/pepper/device_id_fetcher.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/resource_host.h"

namespace content {
class BrowserPpapiHost;
}

namespace IPC {
class Message;
}

namespace chrome {

class PepperFlashDeviceIDHost : public ppapi::host::ResourceHost {
 public:
  PepperFlashDeviceIDHost(content::BrowserPpapiHost* host,
                          PP_Instance instance,
                          PP_Resource resource);
  virtual ~PepperFlashDeviceIDHost();

  // ResourceHost override.
  virtual int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) OVERRIDE;

 private:
  // IPC message handler.
  int32_t OnHostMsgGetDeviceID(const ppapi::host::HostMessageContext* context);

  // Called by the fetcher when the device ID was retrieved, or the empty string
  // on error.
  void GotDeviceID(ppapi::host::ReplyMessageContext reply_context,
                   const std::string& contents);

  scoped_refptr<DeviceIDFetcher> fetcher_;

  base::WeakPtrFactory<PepperFlashDeviceIDHost> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(PepperFlashDeviceIDHost);
};

}  // namespace chrome

#endif  // CHROME_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FLASH_DEVICE_ID_HOST_H_
