// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/pepper/pepper_socket_utils.h"

#include <string>
#include <vector>

#include "base/logging.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/common/content_client.h"
#include "ppapi/c/private/ppb_net_address_private.h"
#include "ppapi/shared_impl/private/net_address_private_impl.h"

namespace content {
namespace pepper_socket_utils {

SocketPermissionRequest CreateSocketPermissionRequest(
    SocketPermissionRequest::OperationType type,
    const PP_NetAddress_Private& net_addr) {
  std::string host = ppapi::NetAddressPrivateImpl::DescribeNetAddress(net_addr,
                                                                      false);
  int port = 0;
  std::vector<unsigned char> address;
  ppapi::NetAddressPrivateImpl::NetAddressToIPEndPoint(net_addr,
                                                       &address,
                                                       &port);
  return SocketPermissionRequest(type, host, port);
}

bool CanUseSocketAPIs(bool external_plugin,
                      bool private_api,
                      const SocketPermissionRequest& params,
                      int render_process_id,
                      int render_view_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  RenderViewHost* render_view_host = RenderViewHost::FromID(render_process_id,
                                                            render_view_id);
  return render_view_host && CanUseSocketAPIs(external_plugin,
                                              private_api,
                                              params,
                                              render_view_host);
}

bool CanUseSocketAPIs(bool external_plugin,
                      bool private_api,
                      const SocketPermissionRequest& params,
                      RenderViewHost* render_view_host) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (!external_plugin) {
    // Always allow socket APIs for out-process plugins (other than external
    // plugins instantiated by the embeeder through
    // BrowserPpapiHost::CreateExternalPluginProcess).
    return true;
  }

  if (!render_view_host)
    return false;
  SiteInstance* site_instance = render_view_host->GetSiteInstance();
  if (!site_instance)
    return false;
  if (!GetContentClient()->browser()->AllowPepperSocketAPI(
          site_instance->GetBrowserContext(),
          site_instance->GetSiteURL(),
          private_api,
          params)) {
    LOG(ERROR) << "Host " << site_instance->GetSiteURL().host()
               << " cannot use socket API or destination is not allowed";
    return false;
  }

  return true;
}

}  // namespace pepper_socket_utils
}  // namespace content
