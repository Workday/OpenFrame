// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_PLUGIN_PEPPER_NETWORK_MANAGER_H_
#define REMOTING_CLIENT_PLUGIN_PEPPER_NETWORK_MANAGER_H_

#include "base/compiler_specific.h"
#include "base/memory/weak_ptr.h"
#include "ppapi/cpp/instance_handle.h"
#include "ppapi/cpp/private/network_monitor_private.h"
#include "third_party/libjingle/source/talk/base/network.h"

namespace pp {
class NetworkListPrivate;
}  // namespace pp

namespace remoting {

// PepperNetworkManager uses the PPB_NetworkMonitor_Private API to
// implement the NetworkManager interface that libjingle uses to
// monitor the host system's network interfaces.
class PepperNetworkManager : public talk_base::NetworkManagerBase {
 public:
  PepperNetworkManager(const pp::InstanceHandle& instance);
  virtual ~PepperNetworkManager();

  // NetworkManager interface.
  virtual void StartUpdating() OVERRIDE;
  virtual void StopUpdating() OVERRIDE;

 private:
  static void OnNetworkListCallbackHandler(void* user_data,
                                           PP_Resource list_resource);

  void OnNetworkList(const pp::NetworkListPrivate& list);

  void SendNetworksChangedSignal();

  pp::NetworkMonitorPrivate monitor_;
  int start_count_;
  bool network_list_received_;

  base::WeakPtrFactory<PepperNetworkManager> weak_factory_;
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_PLUGIN_PEPPER_NETWORK_MANAGER_H_
