// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_PEPPER_PEPPER_FLASH_FONT_FILE_HOST_H_
#define CHROME_RENDERER_PEPPER_PEPPER_FLASH_FONT_FILE_HOST_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "ppapi/c/private/pp_private_font_charset.h"
#include "ppapi/host/resource_host.h"

namespace content {
class RendererPpapiHost;
}

namespace ppapi {
namespace proxy {
struct SerializedFontDescription;
}
}

namespace chrome {

class PepperFlashFontFileHost : public ppapi::host::ResourceHost {
 public:
  PepperFlashFontFileHost(
      content::RendererPpapiHost* host,
      PP_Instance instance,
      PP_Resource resource,
      const ppapi::proxy::SerializedFontDescription& description,
      PP_PrivateFontCharset charset);
  virtual ~PepperFlashFontFileHost();

  virtual int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) OVERRIDE;

 private:
  int32_t OnGetFontTable(ppapi::host::HostMessageContext* context,
                         uint32_t table);

  // Non-owning pointer.
  content::RendererPpapiHost* renderer_ppapi_host_;
  // Only valid on Linux.
  int fd_;

  DISALLOW_COPY_AND_ASSIGN(PepperFlashFontFileHost);
};

}  // namespace chrome

#endif  // CHROME_RENDERER_PEPPER_PEPPER_FLASH_FONT_FILE_HOST_H_
