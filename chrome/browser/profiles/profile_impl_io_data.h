// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PROFILES_PROFILE_IMPL_IO_DATA_H_
#define CHROME_BROWSER_PROFILES_PROFILE_IMPL_IO_DATA_H_

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/containers/hash_tables.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry.h"
#include "chrome/browser/profiles/profile_io_data.h"

namespace chrome_browser_net {
class HttpServerPropertiesManager;
class Predictor;
}  // namespace chrome_browser_net

namespace net {
class FtpTransactionFactory;
class HttpServerProperties;
class HttpTransactionFactory;
}  // namespace net

namespace quota {
class SpecialStoragePolicy;
}  // namespace quota

class ProfileImplIOData : public ProfileIOData {
 public:
  class Handle {
   public:
    explicit Handle(Profile* profile);
    ~Handle();

    // Init() must be called before ~Handle(). It records most of the
    // parameters needed to construct a ChromeURLRequestContextGetter.
    void Init(const base::FilePath& cookie_path,
              const base::FilePath& server_bound_cert_path,
              const base::FilePath& cache_path,
              int cache_max_size,
              const base::FilePath& media_cache_path,
              int media_cache_max_size,
              const base::FilePath& extensions_cookie_path,
              const base::FilePath& profile_path,
              const base::FilePath& infinite_cache_path,
              chrome_browser_net::Predictor* predictor,
              bool restore_old_session_cookies,
              quota::SpecialStoragePolicy* special_storage_policy);

    // These Create*ContextGetter() functions are only exposed because the
    // circular relationship between Profile, ProfileIOData::Handle, and the
    // ChromeURLRequestContextGetter factories requires Profile be able to call
    // these functions.
    scoped_refptr<ChromeURLRequestContextGetter>
        CreateMainRequestContextGetter(
            content::ProtocolHandlerMap* protocol_handlers,
            PrefService* local_state,
            IOThread* io_thread) const;
    scoped_refptr<ChromeURLRequestContextGetter>
        CreateIsolatedAppRequestContextGetter(
            const base::FilePath& partition_path,
            bool in_memory,
            content::ProtocolHandlerMap* protocol_handlers) const;

    content::ResourceContext* GetResourceContext() const;
    // GetResourceContextNoInit() does not call LazyInitialize() so it can be
    // safely be used during initialization.
    content::ResourceContext* GetResourceContextNoInit() const;
    scoped_refptr<ChromeURLRequestContextGetter>
        GetMediaRequestContextGetter() const;
    scoped_refptr<ChromeURLRequestContextGetter>
        GetExtensionsRequestContextGetter() const;
    scoped_refptr<ChromeURLRequestContextGetter>
        GetIsolatedMediaRequestContextGetter(
            const base::FilePath& partition_path,
            bool in_memory) const;

    // Deletes all network related data since |time|. It deletes transport
    // security state since |time| and also deletes HttpServerProperties data.
    // Works asynchronously, however if the |completion| callback is non-null,
    // it will be posted on the UI thread once the removal process completes.
    void ClearNetworkingHistorySince(base::Time time,
                                     const base::Closure& completion);

   private:
    typedef std::map<StoragePartitionDescriptor,
                     scoped_refptr<ChromeURLRequestContextGetter>,
                     StoragePartitionDescriptorLess>
      ChromeURLRequestContextGetterMap;

    // Lazily initialize ProfileParams. We do this on the calls to
    // Get*RequestContextGetter(), so we only initialize ProfileParams right
    // before posting a task to the IO thread to start using them. This prevents
    // objects that are supposed to be deleted on the IO thread, but are created
    // on the UI thread from being unnecessarily initialized.
    void LazyInitialize() const;

    // Ordering is important here. Do not reorder unless you know what you're
    // doing. We need to release |io_data_| *before* the getters, because we
    // want to make sure that the last reference for |io_data_| is on the IO
    // thread. The getters will be deleted on the IO thread, so they will
    // release their refs to their contexts, which will release the last refs to
    // the ProfileIOData on the IO thread.
    mutable scoped_refptr<ChromeURLRequestContextGetter>
        main_request_context_getter_;
    mutable scoped_refptr<ChromeURLRequestContextGetter>
        media_request_context_getter_;
    mutable scoped_refptr<ChromeURLRequestContextGetter>
        extensions_request_context_getter_;
    mutable ChromeURLRequestContextGetterMap app_request_context_getter_map_;
    mutable ChromeURLRequestContextGetterMap
        isolated_media_request_context_getter_map_;
    ProfileImplIOData* const io_data_;

    Profile* const profile_;

    mutable bool initialized_;

    DISALLOW_COPY_AND_ASSIGN(Handle);
  };

 private:
  friend class base::RefCountedThreadSafe<ProfileImplIOData>;

  struct LazyParams {
    LazyParams();
    ~LazyParams();

    // All of these parameters are intended to be read on the IO thread.
    base::FilePath cookie_path;
    base::FilePath server_bound_cert_path;
    base::FilePath cache_path;
    int cache_max_size;
    base::FilePath media_cache_path;
    int media_cache_max_size;
    base::FilePath extensions_cookie_path;
    base::FilePath infinite_cache_path;
    bool restore_old_session_cookies;
    scoped_refptr<quota::SpecialStoragePolicy> special_storage_policy;
  };

  typedef base::hash_map<std::string, net::HttpTransactionFactory* >
      HttpTransactionFactoryMap;

  ProfileImplIOData();
  virtual ~ProfileImplIOData();

  virtual void InitializeInternal(
      ProfileParams* profile_params,
      content::ProtocolHandlerMap* protocol_handlers) const OVERRIDE;
  virtual void InitializeExtensionsRequestContext(
      ProfileParams* profile_params) const OVERRIDE;
  virtual ChromeURLRequestContext* InitializeAppRequestContext(
      ChromeURLRequestContext* main_context,
      const StoragePartitionDescriptor& partition_descriptor,
      scoped_ptr<ProtocolHandlerRegistry::JobInterceptorFactory>
          protocol_handler_interceptor,
      content::ProtocolHandlerMap* protocol_handlers) const OVERRIDE;
  virtual ChromeURLRequestContext* InitializeMediaRequestContext(
      ChromeURLRequestContext* original_context,
      const StoragePartitionDescriptor& partition_descriptor) const OVERRIDE;
  virtual ChromeURLRequestContext*
      AcquireMediaRequestContext() const OVERRIDE;
  virtual ChromeURLRequestContext*
      AcquireIsolatedAppRequestContext(
          ChromeURLRequestContext* main_context,
          const StoragePartitionDescriptor& partition_descriptor,
          scoped_ptr<ProtocolHandlerRegistry::JobInterceptorFactory>
              protocol_handler_interceptor,
          content::ProtocolHandlerMap* protocol_handlers) const OVERRIDE;
  virtual ChromeURLRequestContext*
      AcquireIsolatedMediaRequestContext(
          ChromeURLRequestContext* app_context,
          const StoragePartitionDescriptor& partition_descriptor)
              const OVERRIDE;
  virtual chrome_browser_net::LoadTimeStats* GetLoadTimeStats(
      IOThread::Globals* io_thread_globals) const OVERRIDE;

  // Deletes all network related data since |time|. It deletes transport
  // security state since |time| and also deletes HttpServerProperties data.
  // Works asynchronously, however if the |completion| callback is non-null,
  // it will be posted on the UI thread once the removal process completes.
  void ClearNetworkingHistorySinceOnIOThread(base::Time time,
                                             const base::Closure& completion);

  // Lazy initialization params.
  mutable scoped_ptr<LazyParams> lazy_params_;

  mutable scoped_ptr<net::HttpTransactionFactory> main_http_factory_;
  mutable scoped_ptr<net::FtpTransactionFactory> ftp_factory_;

  // Same as |ProfileIOData::http_server_properties_|, owned there to maintain
  // destruction ordering.
  mutable chrome_browser_net::HttpServerPropertiesManager*
    http_server_properties_manager_;

  mutable scoped_ptr<chrome_browser_net::Predictor> predictor_;

  mutable scoped_ptr<ChromeURLRequestContext> media_request_context_;

  mutable scoped_ptr<net::URLRequestJobFactory> main_job_factory_;
  mutable scoped_ptr<net::URLRequestJobFactory> extensions_job_factory_;

  // Parameters needed for isolated apps.
  base::FilePath profile_path_;
  int app_cache_max_size_;
  int app_media_cache_max_size_;

  DISALLOW_COPY_AND_ASSIGN(ProfileImplIOData);
};

#endif  // CHROME_BROWSER_PROFILES_PROFILE_IMPL_IO_DATA_H_
