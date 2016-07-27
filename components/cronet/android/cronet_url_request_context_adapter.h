// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_CRONET_ANDROID_CRONET_URL_REQUEST_CONTEXT_ADAPTER_H_
#define COMPONENTS_CRONET_ANDROID_CRONET_URL_REQUEST_CONTEXT_ADAPTER_H_

#include <jni.h>

#include <queue>
#include <string>

#include "base/android/scoped_java_ref.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/prefs/json_pref_store.h"
#include "base/threading/thread.h"
#include "net/base/network_quality_estimator.h"

class PrefService;

namespace base {
class SingleThreadTaskRunner;
class TimeTicks;
}  // namespace base

namespace net {
class HttpServerPropertiesManager;
class NetLog;
class ProxyConfigService;
class SdchOwner;
class URLRequestContext;
class WriteToFileNetLogObserver;
}  // namespace net

namespace cronet {

#if defined(DATA_REDUCTION_PROXY_SUPPORT)
class CronetDataReductionProxy;
#endif

struct URLRequestContextConfig;

bool CronetUrlRequestContextAdapterRegisterJni(JNIEnv* env);

// Adapter between Java CronetUrlRequestContext and net::URLRequestContext.
class CronetURLRequestContextAdapter
    : public net::NetworkQualityEstimator::RTTObserver,
      public net::NetworkQualityEstimator::ThroughputObserver {
 public:
  explicit CronetURLRequestContextAdapter(
      scoped_ptr<URLRequestContextConfig> context_config);

  ~CronetURLRequestContextAdapter() override;

  // Called on main Java thread to initialize URLRequestContext.
  void InitRequestContextOnMainThread(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& jcaller);

  // Releases all resources for the request context and deletes the object.
  // Blocks until network thread is destroyed after running all pending tasks.
  void Destroy(JNIEnv* env,
               const base::android::JavaParamRef<jobject>& jcaller);

  // Posts a task that might depend on the context being initialized
  // to the network thread.
  void PostTaskToNetworkThread(const tracked_objects::Location& posted_from,
                               const base::Closure& callback);

  bool IsOnNetworkThread() const;

  net::URLRequestContext* GetURLRequestContext();

  void StartNetLogToFile(JNIEnv* env,
                         const base::android::JavaParamRef<jobject>& jcaller,
                         const base::android::JavaParamRef<jstring>& jfile_name,
                         jboolean jlog_all);

  void StopNetLog(JNIEnv* env,
                  const base::android::JavaParamRef<jobject>& jcaller);

  // Default net::LOAD flags used to create requests.
  int default_load_flags() const { return default_load_flags_; }

  // Called on main Java thread to initialize URLRequestContext.
  void InitRequestContextOnMainThread();

  // Enables the network quality estimator and optionally configures it to
  // observe localhost requests, and to consider smaller responses when
  // observing throughput. It is recommended that both options be set to false.
  void EnableNetworkQualityEstimator(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& jcaller,
      jboolean use_local_host_requests,
      jboolean use_smaller_responses);

  // Request that RTT and/or throughput observations should or should not be
  // provided by the network quality estimator.
  void ProvideRTTObservations(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& jcaller,
      bool should);
  void ProvideThroughputObservations(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& jcaller,
      bool should);

 private:
  // Initializes |context_| on the Network thread.
  void InitializeOnNetworkThread(scoped_ptr<URLRequestContextConfig> config,
                                 const base::android::ScopedJavaGlobalRef<
                                     jobject>& jcronet_url_request_context);

  // Runs a task that might depend on the context being initialized.
  // This method should only be run on the network thread.
  void RunTaskAfterContextInitOnNetworkThread(
      const base::Closure& task_to_run_after_context_init);

  scoped_refptr<base::SingleThreadTaskRunner> GetNetworkTaskRunner() const;

  void StartNetLogToFileOnNetworkThread(const std::string& file_name,
                                        bool log_all);

  void StopNetLogOnNetworkThread();

  // Gets the file thread. Create one if there is none.
  base::Thread* GetFileThread();

  // Instantiate and configure the network quality estimator. For default
  // behavior, parameters should be set to false; otherwise the estimator
  // can be configured to observe requests to localhost, as well as to use
  // observe smaller responses when estimating throughput.
  void EnableNetworkQualityEstimatorOnNetworkThread(
      bool use_local_host_requests,
      bool use_smaller_responses);

  void ProvideRTTObservationsOnNetworkThread(bool should);
  void ProvideThroughputObservationsOnNetworkThread(bool should);

  // net::NetworkQualityEstimator::RTTObserver implementation.
  void OnRTTObservation(
      int32_t rtt_ms,
      const base::TimeTicks& timestamp,
      net::NetworkQualityEstimator::ObservationSource source) override;

  // net::NetworkQualityEstimator::ThroughputObserver implementation.
  void OnThroughputObservation(
      int32_t throughput_kbps,
      const base::TimeTicks& timestamp,
      net::NetworkQualityEstimator::ObservationSource source) override;

  // Network thread is owned by |this|, but is destroyed from java thread.
  base::Thread* network_thread_;

  // File thread should be destroyed last.
  scoped_ptr<base::Thread> file_thread_;

  // |write_to_file_observer_| and |context_| should only be accessed on
  // network thread.
  scoped_ptr<net::WriteToFileNetLogObserver> write_to_file_observer_;

  // |pref_service_| should outlive the HttpServerPropertiesManager owned by
  // |context_|.
  scoped_ptr<PrefService> pref_service_;
  scoped_ptr<net::NetLog> net_log_;
  scoped_ptr<net::URLRequestContext> context_;
  scoped_ptr<net::ProxyConfigService> proxy_config_service_;
  scoped_refptr<JsonPrefStore> json_pref_store_;
  net::HttpServerPropertiesManager* http_server_properties_manager_;

  // |sdch_owner_| should be destroyed before |json_pref_store_|, because
  // tearing down |sdch_owner_| forces |json_pref_store_| to flush pending
  // writes to the disk.
  scoped_ptr<net::SdchOwner> sdch_owner_;

  // Context config is only valid until context is initialized.
  scoped_ptr<URLRequestContextConfig> context_config_;

  // A queue of tasks that need to be run after context has been initialized.
  std::queue<base::Closure> tasks_waiting_for_context_;
  bool is_context_initialized_;
  int default_load_flags_;

  // A network quality estimator.
  scoped_ptr<net::NetworkQualityEstimator> network_quality_estimator_;

  // Java object that owns this CronetURLRequestContextAdapter.
  base::android::ScopedJavaGlobalRef<jobject> jcronet_url_request_context_;

#if defined(DATA_REDUCTION_PROXY_SUPPORT)
  scoped_ptr<CronetDataReductionProxy> data_reduction_proxy_;
#endif

  DISALLOW_COPY_AND_ASSIGN(CronetURLRequestContextAdapter);
};

}  // namespace cronet

#endif  // COMPONENTS_CRONET_ANDROID_CRONET_URL_REQUEST_CONTEXT_ADAPTER_H_
