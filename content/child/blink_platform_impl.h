// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_CHILD_BLINK_PLATFORM_IMPL_H_
#define CONTENT_CHILD_BLINK_PLATFORM_IMPL_H_

#include "base/compiler_specific.h"
#include "base/containers/scoped_ptr_hash_map.h"
#include "base/threading/thread_local_storage.h"
#include "base/timer/timer.h"
#include "base/trace_event/trace_event.h"
#include "components/webcrypto/webcrypto_impl.h"
#include "content/child/webfallbackthemeengine_impl.h"
#include "content/common/content_export.h"
#include "third_party/WebKit/public/platform/Platform.h"
#include "third_party/WebKit/public/platform/WebGestureDevice.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "ui/base/layout.h"

#if defined(USE_DEFAULT_RENDER_THEME)
#include "content/child/webthemeengine_impl_default.h"
#elif defined(OS_WIN)
#include "content/child/webthemeengine_impl_win.h"
#elif defined(OS_MACOSX)
#include "content/child/webthemeengine_impl_mac.h"
#elif defined(OS_ANDROID)
#include "content/child/webthemeengine_impl_android.h"
#endif

namespace base {
class MessageLoop;
}

namespace content {
class BackgroundSyncProvider;
class FlingCurveConfiguration;
class NotificationDispatcher;
class PermissionDispatcher;
class PushDispatcher;
class ThreadSafeSender;
class TraceLogObserverAdapter;
class WebCryptoImpl;
class WebGeofencingProviderImpl;
class WebMemoryDumpProviderAdapter;

class CONTENT_EXPORT BlinkPlatformImpl
    : NON_EXPORTED_BASE(public blink::Platform) {
 public:
  BlinkPlatformImpl();
  explicit BlinkPlatformImpl(
      scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner);
  ~BlinkPlatformImpl() override;

  // Platform methods (partial implementation):
  blink::WebThemeEngine* themeEngine() override;
  blink::WebFallbackThemeEngine* fallbackThemeEngine() override;
  blink::Platform::FileHandle databaseOpenFile(
      const blink::WebString& vfs_file_name,
      int desired_flags) override;
  int databaseDeleteFile(const blink::WebString& vfs_file_name,
                         bool sync_dir) override;
  long databaseGetFileAttributes(
      const blink::WebString& vfs_file_name) override;
  long long databaseGetFileSize(const blink::WebString& vfs_file_name) override;
  long long databaseGetSpaceAvailableForOrigin(
      const blink::WebString& origin_identifier) override;
  bool databaseSetFileSize(const blink::WebString& vfs_file_name,
                           long long size) override;
  blink::WebString signedPublicKeyAndChallengeString(
      unsigned key_size_index,
      const blink::WebString& challenge,
      const blink::WebURL& url) override;
  size_t memoryUsageMB() override;
  size_t actualMemoryUsageMB() override;
  size_t physicalMemoryMB() override;
  size_t virtualMemoryLimitMB() override;
  bool isLowEndDeviceMode() override;
  size_t numberOfProcessors() override;

  blink::WebDiscardableMemory* allocateAndLockDiscardableMemory(
      size_t bytes) override;
  size_t maxDecodedImageBytes() override;
  uint32_t getUniqueIdForProcess() override;
  blink::WebURLLoader* createURLLoader() override;
  blink::WebSocketHandle* createWebSocketHandle() override;
  blink::WebString userAgent() override;
  blink::WebData parseDataURL(const blink::WebURL& url,
                              blink::WebString& mimetype,
                              blink::WebString& charset) override;
  blink::WebURLError cancelledError(const blink::WebURL& url) const override;
  bool isReservedIPAddress(const blink::WebString& host) const override;
  bool portAllowed(const blink::WebURL& url) const override;
  blink::WebThread* createThread(const char* name) override;
  blink::WebThread* currentThread() override;
  void yieldCurrentThread() override;
  blink::WebWaitableEvent* createWaitableEvent(
      blink::WebWaitableEvent::ResetPolicy policy,
      blink::WebWaitableEvent::InitialState state) override;
  blink::WebWaitableEvent* waitMultipleEvents(
      const blink::WebVector<blink::WebWaitableEvent*>& events) override;
  void decrementStatsCounter(const char* name) override;
  void incrementStatsCounter(const char* name) override;
  void histogramCustomCounts(const char* name,
                             int sample,
                             int min,
                             int max,
                             int bucket_count) override;
  void histogramEnumeration(const char* name,
                            int sample,
                            int boundary_value) override;
  void histogramSparse(const char* name, int sample) override;
  const unsigned char* getTraceCategoryEnabledFlag(
      const char* category_name) override;
  TraceEventAPIAtomicWord* getTraceSamplingState(
      const unsigned thread_bucket) override;
  virtual TraceEventHandle addTraceEvent(
      char phase,
      const unsigned char* category_group_enabled,
      const char* name,
      unsigned long long id,
      unsigned long long bind_id,
      double timestamp,
      int num_args,
      const char** arg_names,
      const unsigned char* arg_types,
      const unsigned long long* arg_values,
      blink::WebConvertableToTraceFormat* convertable_values,
      unsigned int flags);
  virtual TraceEventHandle addTraceEvent(
      char phase,
      const unsigned char* category_group_enabled,
      const char* name,
      unsigned long long id,
      double timestamp,
      int num_args,
      const char** arg_names,
      const unsigned char* arg_types,
      const unsigned long long* arg_values,
      blink::WebConvertableToTraceFormat* convertable_values,
      unsigned char flags);
  void updateTraceEventDuration(const unsigned char* category_group_enabled,
                                const char* name,
                                TraceEventHandle) override;
  void registerMemoryDumpProvider(blink::WebMemoryDumpProvider* wmdp,
                                  const char* name) override;
  void unregisterMemoryDumpProvider(
      blink::WebMemoryDumpProvider* wmdp) override;
  blink::WebProcessMemoryDump* createProcessMemoryDump() override;
  blink::Platform::WebMemoryAllocatorDumpGuid createWebMemoryAllocatorDumpGuid(
      const blink::WebString& guidStr) override;
  void addTraceLogEnabledStateObserver(
      blink::Platform::TraceLogEnabledStateObserver* observer) override;
  void removeTraceLogEnabledStateObserver(
      blink::Platform::TraceLogEnabledStateObserver* observer) override;

  blink::WebData loadResource(const char* name) override;
  blink::WebString queryLocalizedString(
      blink::WebLocalizedString::Name name) override;
  virtual blink::WebString queryLocalizedString(
      blink::WebLocalizedString::Name name,
      int numeric_value);
  blink::WebString queryLocalizedString(blink::WebLocalizedString::Name name,
                                        const blink::WebString& value) override;
  blink::WebString queryLocalizedString(
      blink::WebLocalizedString::Name name,
      const blink::WebString& value1,
      const blink::WebString& value2) override;
  void suddenTerminationChanged(bool enabled) override {}
  double currentTimeSeconds() override;
  double monotonicallyIncreasingTimeSeconds() override;
  double systemTraceTime() override;
  void cryptographicallyRandomValues(unsigned char* buffer,
                                     size_t length) override;
  blink::WebGestureCurve* createFlingAnimationCurve(
      blink::WebGestureDevice device_source,
      const blink::WebFloatPoint& velocity,
      const blink::WebSize& cumulative_scroll) override;
  void didStartWorkerRunLoop() override;
  void didStopWorkerRunLoop() override;
  blink::WebCrypto* crypto() override;
  blink::WebGeofencingProvider* geofencingProvider() override;
  blink::WebNotificationManager* notificationManager() override;
  blink::WebPushProvider* pushProvider() override;
  blink::WebServicePortProvider* createServicePortProvider(
      blink::WebServicePortProviderClient*) override;
  blink::WebPermissionClient* permissionClient() override;
  blink::WebSyncProvider* backgroundSyncProvider() override;

  blink::WebString domCodeStringFromEnum(int dom_code) override;
  int domEnumFromCodeString(const blink::WebString& codeString) override;
  blink::WebString domKeyStringFromEnum(int dom_key) override;
  int domKeyEnumFromString(const blink::WebString& key_string) override;

 private:
  void InternalInit();
  void UpdateWebThreadTLS(blink::WebThread* thread);

  bool IsMainThread() const;

  scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner_;
  WebThemeEngineImpl native_theme_engine_;
  WebFallbackThemeEngineImpl fallback_theme_engine_;
  base::ThreadLocalStorage::Slot current_thread_slot_;
  webcrypto::WebCryptoImpl web_crypto_;
  scoped_ptr<WebGeofencingProviderImpl> geofencing_provider_;
  base::ScopedPtrHashMap<blink::WebMemoryDumpProvider*,
                         scoped_ptr<WebMemoryDumpProviderAdapter>>
      memory_dump_providers_;
  base::ScopedPtrHashMap<blink::Platform::TraceLogEnabledStateObserver*,
                         scoped_ptr<TraceLogObserverAdapter>>
      trace_log_observers_;

  scoped_refptr<ThreadSafeSender> thread_safe_sender_;
  scoped_refptr<NotificationDispatcher> notification_dispatcher_;
  scoped_refptr<PushDispatcher> push_dispatcher_;
  scoped_ptr<PermissionDispatcher> permission_client_;
  scoped_ptr<BackgroundSyncProvider> main_thread_sync_provider_;
};

}  // namespace content

#endif  // CONTENT_CHILD_BLINK_PLATFORM_IMPL_H_
