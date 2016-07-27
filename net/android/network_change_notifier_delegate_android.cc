// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/android/network_change_notifier_delegate_android.h"

#include "base/android/context_utils.h"
#include "base/android/jni_array.h"
#include "base/logging.h"
#include "jni/NetworkChangeNotifier_jni.h"
#include "net/android/network_change_notifier_android.h"

namespace net {

namespace {

// Converts a Java side connection type (integer) to
// the native side NetworkChangeNotifier::ConnectionType.
NetworkChangeNotifier::ConnectionType ConvertConnectionType(
    jint connection_type) {
  switch (connection_type) {
    case NetworkChangeNotifier::CONNECTION_UNKNOWN:
    case NetworkChangeNotifier::CONNECTION_ETHERNET:
    case NetworkChangeNotifier::CONNECTION_WIFI:
    case NetworkChangeNotifier::CONNECTION_2G:
    case NetworkChangeNotifier::CONNECTION_3G:
    case NetworkChangeNotifier::CONNECTION_4G:
    case NetworkChangeNotifier::CONNECTION_NONE:
    case NetworkChangeNotifier::CONNECTION_BLUETOOTH:
      break;
    default:
      NOTREACHED() << "Unknown connection type received: " << connection_type;
      return NetworkChangeNotifier::CONNECTION_UNKNOWN;
  }
  return static_cast<NetworkChangeNotifier::ConnectionType>(connection_type);
}

// Converts a Java side connection type (integer) to
// the native side NetworkChangeNotifier::ConnectionType.
NetworkChangeNotifier::ConnectionSubtype ConvertConnectionSubtype(
    jint subtype) {
  DCHECK(subtype >= 0 && subtype <= NetworkChangeNotifier::SUBTYPE_LAST);

  return static_cast<NetworkChangeNotifier::ConnectionSubtype>(subtype);
}

}  // namespace

// static
void NetworkChangeNotifierDelegateAndroid::JavaIntArrayToNetworkMap(
    JNIEnv* env,
    jintArray int_array,
    NetworkMap* network_map) {
  std::vector<int> int_list;
  base::android::JavaIntArrayToIntVector(env, int_array, &int_list);
  network_map->clear();
  for (auto i = int_list.begin(); i != int_list.end(); ++i) {
    NetworkChangeNotifier::NetworkHandle network_handle = *i;
    CHECK(++i != int_list.end());
    (*network_map)[network_handle] = static_cast<ConnectionType>(*i);
  }
}

jdouble GetMaxBandwidthForConnectionSubtype(JNIEnv* env,
                                            const JavaParamRef<jclass>& caller,
                                            jint subtype) {
  return NetworkChangeNotifierAndroid::GetMaxBandwidthForConnectionSubtype(
      ConvertConnectionSubtype(subtype));
}

NetworkChangeNotifierDelegateAndroid::NetworkChangeNotifierDelegateAndroid()
    : observers_(new base::ObserverListThreadSafe<Observer>()) {
  JNIEnv* env = base::android::AttachCurrentThread();
  java_network_change_notifier_.Reset(
      Java_NetworkChangeNotifier_init(
          env, base::android::GetApplicationContext()));
  Java_NetworkChangeNotifier_addNativeObserver(
      env, java_network_change_notifier_.obj(),
      reinterpret_cast<intptr_t>(this));
  SetCurrentConnectionType(
      ConvertConnectionType(
          Java_NetworkChangeNotifier_getCurrentConnectionType(
              env, java_network_change_notifier_.obj())));
  SetCurrentMaxBandwidth(
      Java_NetworkChangeNotifier_getCurrentMaxBandwidthInMbps(
          env, java_network_change_notifier_.obj()));
  SetCurrentDefaultNetwork(Java_NetworkChangeNotifier_getCurrentDefaultNetId(
      env, java_network_change_notifier_.obj()));
  NetworkMap network_map;
  ScopedJavaLocalRef<jintArray> networks_and_types =
      Java_NetworkChangeNotifier_getCurrentNetworksAndTypes(
          env, java_network_change_notifier_.obj());
  JavaIntArrayToNetworkMap(env, networks_and_types.obj(), &network_map);
  SetCurrentNetworksAndTypes(network_map);
}

NetworkChangeNotifierDelegateAndroid::~NetworkChangeNotifierDelegateAndroid() {
  DCHECK(thread_checker_.CalledOnValidThread());
  observers_->AssertEmpty();
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_NetworkChangeNotifier_removeNativeObserver(
      env, java_network_change_notifier_.obj(),
      reinterpret_cast<intptr_t>(this));
}

NetworkChangeNotifier::ConnectionType
NetworkChangeNotifierDelegateAndroid::GetCurrentConnectionType() const {
  base::AutoLock auto_lock(connection_lock_);
  return connection_type_;
}

void NetworkChangeNotifierDelegateAndroid::
    GetCurrentMaxBandwidthAndConnectionType(
        double* max_bandwidth_mbps,
        ConnectionType* connection_type) const {
  base::AutoLock auto_lock(connection_lock_);
  *connection_type = connection_type_;
  *max_bandwidth_mbps = connection_max_bandwidth_;
}

NetworkChangeNotifier::ConnectionType
NetworkChangeNotifierDelegateAndroid::GetNetworkConnectionType(
    NetworkChangeNotifier::NetworkHandle network) const {
  base::AutoLock auto_lock(connection_lock_);
  auto network_entry = network_map_.find(network);
  if (network_entry == network_map_.end())
    return ConnectionType::CONNECTION_UNKNOWN;
  return network_entry->second;
}

NetworkChangeNotifier::NetworkHandle
NetworkChangeNotifierDelegateAndroid::GetCurrentDefaultNetwork() const {
  base::AutoLock auto_lock(connection_lock_);
  return default_network_;
}

void NetworkChangeNotifierDelegateAndroid::GetCurrentlyConnectedNetworks(
    NetworkList* network_list) const {
  network_list->clear();
  base::AutoLock auto_lock(connection_lock_);
  for (auto i : network_map_)
    network_list->push_back(i.first);
}

void NetworkChangeNotifierDelegateAndroid::NotifyConnectionTypeChanged(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint new_connection_type,
    jint default_netid) {
  DCHECK(thread_checker_.CalledOnValidThread());
  const ConnectionType actual_connection_type = ConvertConnectionType(
      new_connection_type);
  SetCurrentConnectionType(actual_connection_type);
  NetworkHandle default_network = default_netid;
  if (default_network != GetCurrentDefaultNetwork()) {
    SetCurrentDefaultNetwork(default_network);
    bool default_exists;
    {
      base::AutoLock auto_lock(connection_lock_);
      // |default_network| may be an invalid value (i.e. -1) in cases where
      // the device is disconnected or when run on Android versions prior to L,
      // in which case |default_exists| will correctly be false and no
      // OnNetworkMadeDefault notification will be sent.
      default_exists = network_map_.find(default_network) != network_map_.end();
    }
    // Android Lollipop had race conditions where CONNECTIVITY_ACTION intents
    // were sent out before the network was actually made the default.
    // Delay sending the OnNetworkMadeDefault notification until we are
    // actually notified that the network connected in NotifyOfNetworkConnect.
    if (default_exists) {
      observers_->Notify(FROM_HERE, &Observer::OnNetworkMadeDefault,
                         default_network);
    }
  }
  observers_->Notify(FROM_HERE, &Observer::OnConnectionTypeChanged);
}

jint NetworkChangeNotifierDelegateAndroid::GetConnectionType(JNIEnv*,
                                                             jobject) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  return GetCurrentConnectionType();
}

void NetworkChangeNotifierDelegateAndroid::NotifyMaxBandwidthChanged(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jdouble new_max_bandwidth) {
  DCHECK(thread_checker_.CalledOnValidThread());

  SetCurrentMaxBandwidth(new_max_bandwidth);
  observers_->Notify(FROM_HERE, &Observer::OnMaxBandwidthChanged,
                     new_max_bandwidth, GetCurrentConnectionType());
}

void NetworkChangeNotifierDelegateAndroid::NotifyOfNetworkConnect(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint net_id,
    jint connection_type) {
  DCHECK(thread_checker_.CalledOnValidThread());
  NetworkHandle network = net_id;
  bool already_exists;
  {
    base::AutoLock auto_lock(connection_lock_);
    already_exists = network_map_.find(network) != network_map_.end();
    network_map_[network] = static_cast<ConnectionType>(connection_type);
  }
  // Android Lollipop would send many duplicate notifications.
  // This was later fixed in Android Marshmallow.
  // Deduplicate them here by avoiding sending duplicate notifications.
  if (!already_exists) {
    observers_->Notify(FROM_HERE, &Observer::OnNetworkConnected, network);
    if (network == GetCurrentDefaultNetwork()) {
      observers_->Notify(FROM_HERE, &Observer::OnNetworkMadeDefault, network);
    }
  }
}

void NetworkChangeNotifierDelegateAndroid::NotifyOfNetworkSoonToDisconnect(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint net_id) {
  DCHECK(thread_checker_.CalledOnValidThread());
  NetworkHandle network = net_id;
  {
    base::AutoLock auto_lock(connection_lock_);
    if (network_map_.find(network) == network_map_.end())
      return;
  }
  observers_->Notify(FROM_HERE, &Observer::OnNetworkSoonToDisconnect, network);
}

void NetworkChangeNotifierDelegateAndroid::NotifyOfNetworkDisconnect(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jint net_id) {
  DCHECK(thread_checker_.CalledOnValidThread());
  NetworkHandle network = net_id;
  {
    base::AutoLock auto_lock(connection_lock_);
    if (network == default_network_)
      default_network_ = NetworkChangeNotifier::kInvalidNetworkHandle;
    if (network_map_.erase(network) == 0)
      return;
  }
  observers_->Notify(FROM_HERE, &Observer::OnNetworkDisconnected, network);
}

void NetworkChangeNotifierDelegateAndroid::NotifyUpdateActiveNetworkList(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jintArray>& active_networks) {
  DCHECK(thread_checker_.CalledOnValidThread());
  NetworkList active_network_list;
  base::android::JavaIntArrayToIntVector(env, active_networks,
                                         &active_network_list);
  NetworkList disconnected_networks;
  {
    base::AutoLock auto_lock(connection_lock_);
    for (auto i : network_map_) {
      bool found = false;
      for (auto j : active_network_list) {
        if (j == i.first) {
          found = true;
          break;
        }
      }
      if (!found) {
        disconnected_networks.push_back(i.first);
      }
    }
  }
  for (auto disconnected_network : disconnected_networks)
    NotifyOfNetworkDisconnect(env, obj, disconnected_network);
}

void NetworkChangeNotifierDelegateAndroid::AddObserver(
    Observer* observer) {
  observers_->AddObserver(observer);
}

void NetworkChangeNotifierDelegateAndroid::RemoveObserver(
    Observer* observer) {
  observers_->RemoveObserver(observer);
}

// static
bool NetworkChangeNotifierDelegateAndroid::Register(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

void NetworkChangeNotifierDelegateAndroid::SetCurrentConnectionType(
    ConnectionType new_connection_type) {
  base::AutoLock auto_lock(connection_lock_);
  connection_type_ = new_connection_type;
}

void NetworkChangeNotifierDelegateAndroid::SetCurrentMaxBandwidth(
    double max_bandwidth) {
  base::AutoLock auto_lock(connection_lock_);
  connection_max_bandwidth_ = max_bandwidth;
}

void NetworkChangeNotifierDelegateAndroid::SetCurrentDefaultNetwork(
    NetworkHandle default_network) {
  base::AutoLock auto_lock(connection_lock_);
  default_network_ = default_network;
}

void NetworkChangeNotifierDelegateAndroid::SetCurrentNetworksAndTypes(
    NetworkMap network_map) {
  base::AutoLock auto_lock(connection_lock_);
  network_map_ = network_map;
}

void NetworkChangeNotifierDelegateAndroid::SetOnline() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_NetworkChangeNotifier_forceConnectivityState(env, true);
}

void NetworkChangeNotifierDelegateAndroid::SetOffline() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_NetworkChangeNotifier_forceConnectivityState(env, false);
}

void NetworkChangeNotifierDelegateAndroid::FakeNetworkConnected(
    NetworkChangeNotifier::NetworkHandle network,
    ConnectionType type) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_NetworkChangeNotifier_fakeNetworkConnected(env, network, type);
}

void NetworkChangeNotifierDelegateAndroid::FakeNetworkSoonToBeDisconnected(
    NetworkChangeNotifier::NetworkHandle network) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_NetworkChangeNotifier_fakeNetworkSoonToBeDisconnected(env, network);
}

void NetworkChangeNotifierDelegateAndroid::FakeNetworkDisconnected(
    NetworkChangeNotifier::NetworkHandle network) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_NetworkChangeNotifier_fakeNetworkDisconnected(env, network);
}

void NetworkChangeNotifierDelegateAndroid::FakeUpdateActiveNetworkList(
    NetworkChangeNotifier::NetworkList networks) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_NetworkChangeNotifier_fakeUpdateActiveNetworkList(
      env, base::android::ToJavaIntArray(env, networks).obj());
}

void NetworkChangeNotifierDelegateAndroid::FakeDefaultNetwork(
    NetworkChangeNotifier::NetworkHandle network,
    ConnectionType type) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_NetworkChangeNotifier_fakeDefaultNetwork(env, network, type);
}

}  // namespace net
