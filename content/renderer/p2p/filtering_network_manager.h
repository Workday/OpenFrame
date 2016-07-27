// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_P2P_FILTERING_NETWORK_MANAGER_H_
#define CONTENT_RENDERER_P2P_FILTERING_NETWORK_MANAGER_H_

#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "content/common/content_export.h"
#include "content/renderer/p2p/network_manager_uma.h"
#include "third_party/webrtc/base/network.h"
#include "third_party/webrtc/base/sigslot.h"
#include "url/gurl.h"

namespace media {
class MediaPermission;
}  // namespace media

namespace content {

// FilteringNetworkManager exposes rtc::NetworkManager to
// PeerConnectionDependencyFactory and wraps the IpcNetworkManager. It only
// handles the case wher emultiple_routes is requested. It checks at least one
// of mic/camera permissions is granted before allowing WebRTC to use the local
// IP addresses as ICE candidates. The class handles asynchronous signals like
// SignalNetworksChanged from IpcNetworkManager and permission status from
// MediaPermission before it signals WebRTC that the network information is
// ready. It is designed to fire the network change event at the earliest time
// to reduce any extra call setup delay. This class is not thread safe and
// should only be used by WebRTC's worker thread. It inherits from
// rtc::NetworkManagerBase to have the same implementation of
// GetAnyAddressNetworks(). We can't mark the whole class CONTENT_EXPORT as it
// requires all super classes to be CONTENT_EXPORT as well.
class FilteringNetworkManager : public rtc::NetworkManagerBase,
                                public sigslot::has_slots<> {
 public:
  // This class is created by WebRTC's signaling thread but used by WebRTC's
  // worker thread |task_runner|.
  CONTENT_EXPORT FilteringNetworkManager(
      rtc::NetworkManager* network_manager,
      const GURL& requesting_origin,
      scoped_ptr<media::MediaPermission> media_permission);

  CONTENT_EXPORT ~FilteringNetworkManager() override;

  // Check mic/camera permission. This is called by P2PPortAllocatorFactory.
  CONTENT_EXPORT void Initialize();

  // rtc::NetworkManager:
  void StartUpdating() override;
  void StopUpdating() override;
  void GetNetworks(NetworkList* networks) const override;
  bool GetDefaultLocalAddress(int family,
                              rtc::IPAddress* ipaddress) const override;

 private:
  // Receive callback from MediaPermission when the permission status is
  // available.
  void OnPermissionStatus(bool granted);

  base::WeakPtr<FilteringNetworkManager> GetWeakPtr();

  // Receive callback from the wrapped IpcNetworkManager when the underneath
  // network list is changed.
  void OnNetworksChanged();

  // Reporting the IPPermissionStatus and how long it takes to send
  // SignalNetworksChanged. |report_start_latency| is false when called by the
  // destructor to report no networks changed signal is ever fired and could
  // potentially be a bug.
  void ReportMetrics(bool report_start_latency);

  // A tri-state permission checking status. It starts with UNKNOWN and will
  // change to GRANTED if one of permissions is granted. Otherwise, DENIED will
  // be returned.
  IPPermissionStatus GetIPPermissionStatus() const;

  // Indicate that we have new information worth to signal. This could mean that
  // all pending checks denied and we have never signaled, or enumeration is
  // allowed and we have new updates now.
  bool should_fire_event() const;

  void FireEventIfStarted();

  void SendNetworksChangedSignal();

  // |network_manager_| is just a reference, owned by
  // PeerConnectionDependencyFactory.
  rtc::NetworkManager* network_manager_;

  // The class is created by the signaling thread but used by the worker thread.
  base::ThreadChecker thread_checker_;

  scoped_ptr<media::MediaPermission> media_permission_;

  int pending_permission_checks_ = 0;

  // Whether we have fired the first SignalNetworksChanged.
  bool sent_first_update_ = false;

  // Whether we have received new SignalNetworksChanged from |network_manager_|
  // since last firing.
  bool received_networks_changed_since_last_firing_ = false;

  // SignalNetworksChanged will only be fired if there is any outstanding
  // StartUpdating.
  int start_count_ = 0;

  // Track whether Initialize has been called before StartUpdating.
  bool initialized_ = false;

  // Track how long it takes for client to receive SignalNetworksChanged. This
  // helps to identify if the signal is delayed by permission check and increase
  // the setup time.
  base::TimeTicks start_updating_time_;

  GURL requesting_origin_;

  base::WeakPtrFactory<FilteringNetworkManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(FilteringNetworkManager);
};

}  // namespace content

#endif  // CONTENT_RENDERER_P2P_FILTERING_NETWORK_MANAGER_H_
