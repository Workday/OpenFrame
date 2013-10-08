// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CUSTOM_HANDLERS_PROTOCOL_HANDLER_REGISTRY_H_
#define CHROME_BROWSER_CUSTOM_HANDLERS_PROTOCOL_HANDLER_REGISTRY_H_

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/sequenced_task_runner_helpers.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/common/custom_handlers/protocol_handler.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_job.h"
#include "net/url_request/url_request_job_factory.h"

namespace user_prefs {
class PrefRegistrySyncable;
}

// This is where handlers for protocols registered with
// navigator.registerProtocolHandler() are registered. Each Profile owns an
// instance of this class, which is initialized on browser start through
// Profile::InitRegisteredProtocolHandlers(), and they should be the only
// instances of this class.
class ProtocolHandlerRegistry : public BrowserContextKeyedService {

 public:
  // Provides notification of when the OS level user agent settings
  // are changed.
  class DefaultClientObserver
      : public ShellIntegration::DefaultWebClientObserver {
   public:
    explicit DefaultClientObserver(ProtocolHandlerRegistry* registry);
    virtual ~DefaultClientObserver();

    // Get response from the worker regarding whether Chrome is the default
    // handler for the protocol.
    virtual void SetDefaultWebClientUIState(
        ShellIntegration::DefaultWebClientUIState state) OVERRIDE;

    virtual bool IsInteractiveSetDefaultPermitted() OVERRIDE;

    // Give the observer a handle to the worker, so we can find out the protocol
    // when we're called and also tell the worker if we get deleted.
    void SetWorker(ShellIntegration::DefaultProtocolClientWorker* worker);

   protected:
    ShellIntegration::DefaultProtocolClientWorker* worker_;

   private:
    virtual bool IsOwnedByWorker() OVERRIDE;

    // This is a raw pointer, not reference counted, intentionally. In general
    // subclasses of DefaultWebClientObserver are not able to be refcounted
    // e.g. the browser options page
    ProtocolHandlerRegistry* registry_;

    DISALLOW_COPY_AND_ASSIGN(DefaultClientObserver);
  };

  // |Delegate| provides an interface for interacting asynchronously
  // with the underlying OS for the purposes of registering Chrome
  // as the default handler for specific protocols.
  class Delegate {
   public:
    virtual ~Delegate();
    virtual void RegisterExternalHandler(const std::string& protocol);
    virtual void DeregisterExternalHandler(const std::string& protocol);
    virtual bool IsExternalHandlerRegistered(const std::string& protocol);
    virtual ShellIntegration::DefaultProtocolClientWorker* CreateShellWorker(
        ShellIntegration::DefaultWebClientObserver* observer,
        const std::string& protocol);
    virtual DefaultClientObserver* CreateShellObserver(
        ProtocolHandlerRegistry* registry);
    virtual void RegisterWithOSAsDefaultClient(
        const std::string& protocol,
        ProtocolHandlerRegistry* registry);
  };

  // Forward declaration of the internal implementation class.
  class IOThreadDelegate;

  // JobInterceptorFactory intercepts URLRequestJob creation for URLRequests the
  // ProtocolHandlerRegistry is registered to handle.  When no handler is
  // registered, the URLRequest is passed along to the chained
  // URLRequestJobFactory (set with |JobInterceptorFactory::Chain|).
  // JobInterceptorFactory's are created via
  // |ProtocolHandlerRegistry::CreateJobInterceptorFactory|.
  class JobInterceptorFactory : public net::URLRequestJobFactory {
   public:
    // |io_thread_delegate| is used to perform actual job creation work.
    explicit JobInterceptorFactory(IOThreadDelegate* io_thread_delegate);
    virtual ~JobInterceptorFactory();

    // |job_factory| is set as the URLRequestJobFactory where requests are
    // forwarded if JobInterceptorFactory decides to pass on them.
    void Chain(scoped_ptr<net::URLRequestJobFactory> job_factory);

    // URLRequestJobFactory implementation.
    virtual net::URLRequestJob* MaybeCreateJobWithProtocolHandler(
        const std::string& scheme,
        net::URLRequest* request,
        net::NetworkDelegate* network_delegate) const OVERRIDE;
    virtual bool IsHandledProtocol(const std::string& scheme) const OVERRIDE;
    virtual bool IsHandledURL(const GURL& url) const OVERRIDE;
    virtual bool IsSafeRedirectTarget(const GURL& location) const OVERRIDE;

   private:
    // When JobInterceptorFactory decides to pass on particular requests,
    // they're forwarded to the chained URLRequestJobFactory, |job_factory_|.
    scoped_ptr<URLRequestJobFactory> job_factory_;
    // |io_thread_delegate_| performs the actual job creation decisions by
    // mirroring the ProtocolHandlerRegistry on the IO thread.
    scoped_refptr<IOThreadDelegate> io_thread_delegate_;

    DISALLOW_COPY_AND_ASSIGN(JobInterceptorFactory);
  };

  typedef std::map<std::string, ProtocolHandler> ProtocolHandlerMap;
  typedef std::vector<ProtocolHandler> ProtocolHandlerList;
  typedef std::map<std::string, ProtocolHandlerList> ProtocolHandlerMultiMap;
  typedef std::vector<DefaultClientObserver*> DefaultClientObserverList;

  // Creates a new instance. Assumes ownership of |delegate|.
  ProtocolHandlerRegistry(Profile* profile, Delegate* delegate);
  virtual ~ProtocolHandlerRegistry();

  // Returns a net::URLRequestJobFactory suitable for use on the IO thread, but
  // is initialized on the UI thread.
  scoped_ptr<JobInterceptorFactory> CreateJobInterceptorFactory();

  // Called when a site tries to register as a protocol handler. If the request
  // can be handled silently by the registry - either to ignore the request
  // or to update an existing handler - the request will succeed. If this
  // function returns false the user needs to be prompted for confirmation.
  bool SilentlyHandleRegisterHandlerRequest(const ProtocolHandler& handler);

  // Called when the user accepts the registration of a given protocol handler.
  void OnAcceptRegisterProtocolHandler(const ProtocolHandler& handler);

  // Called when the user denies the registration of a given protocol handler.
  void OnDenyRegisterProtocolHandler(const ProtocolHandler& handler);

  // Called when the user indicates that they don't want to be asked about the
  // given protocol handler again.
  void OnIgnoreRegisterProtocolHandler(const ProtocolHandler& handler);

  // Removes all handlers that have the same origin and protocol as the given
  // one and installs the given handler. Returns true if any protocol handlers
  // were replaced.
  bool AttemptReplace(const ProtocolHandler& handler);

  // Returns a list of protocol handlers that can be replaced by the given
  // handler.
  ProtocolHandlerList GetReplacedHandlers(const ProtocolHandler& handler) const;

  // Clears the default for the provided protocol.
  void ClearDefault(const std::string& scheme);

  // Returns true if this handler is the default handler for its protocol.
  bool IsDefault(const ProtocolHandler& handler) const;

  // Initializes default protocol settings and loads them from prefs.
  // This method must be called to complete initialization of the
  // registry after creation, and prior to use.
  void InitProtocolSettings();

  // Returns the offset in the list of handlers for a protocol of the default
  // handler for that protocol.
  int GetHandlerIndex(const std::string& scheme) const;

  // Get the list of protocol handlers for the given scheme.
  ProtocolHandlerList GetHandlersFor(const std::string& scheme) const;

  // Get the list of ignored protocol handlers.
  ProtocolHandlerList GetIgnoredHandlers();

  // Yields a list of the protocols that have handlers registered in this
  // registry.
  void GetRegisteredProtocols(std::vector<std::string>* output) const;

  // Returns true if we allow websites to register handlers for the given
  // scheme.
  bool CanSchemeBeOverridden(const std::string& scheme) const;

  // Returns true if an identical protocol handler has already been registered.
  bool IsRegistered(const ProtocolHandler& handler) const;

  // Returns true if an identical protocol handler is being ignored.
  bool IsIgnored(const ProtocolHandler& handler) const;

  // Returns true if an equivalent protocol handler has already been registered.
  bool HasRegisteredEquivalent(const ProtocolHandler& handler) const;

  // Returns true if an equivalent protocol handler is being ignored.
  bool HasIgnoredEquivalent(const ProtocolHandler& handler) const;

  // Causes the given protocol handler to not be ignored anymore.
  void RemoveIgnoredHandler(const ProtocolHandler& handler);

  // Returns true if the protocol has a default protocol handler.
  bool IsHandledProtocol(const std::string& scheme) const;

  // Removes the given protocol handler from the registry.
  void RemoveHandler(const ProtocolHandler& handler);

  // Remove the default handler for the given protocol.
  void RemoveDefaultHandler(const std::string& scheme);

  // Returns the default handler for this protocol, or an empty handler if none
  // exists.
  const ProtocolHandler& GetHandlerFor(const std::string& scheme) const;

  // Puts this registry in the enabled state - registered protocol handlers
  // will handle requests.
  void Enable();

  // Puts this registry in the disabled state - registered protocol handlers
  // will not handle requests.
  void Disable();

  // This is called by the UI thread when the system is shutting down. This
  // does finalization which must be done on the UI thread.
  virtual void Shutdown() OVERRIDE;

  // Registers the preferences that we store registered protocol handlers in.
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  bool enabled() const { return enabled_; }

  // Add a predefined protocol handler. This has to be called before the first
  // load command was issued, otherwise the command will be ignored.
  void AddPredefinedHandler(const ProtocolHandler& handler);

 private:
  friend class base::DeleteHelper<ProtocolHandlerRegistry>;
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::IO>;

  // for access to InstallDefaultsForChromeOS
  friend class ProtocolHandlerRegistryFactory;

  friend class ProtocolHandlerRegistryTest;
  friend class RegisterProtocolHandlerBrowserTest;

  // Puts the given handler at the top of the list of handlers for its
  // protocol.
  void PromoteHandler(const ProtocolHandler& handler);

  // Saves a user's registered protocol handlers.
  void Save();

  // Returns a pointer to the list of handlers registered for the given scheme,
  // or NULL if there are none.
  const ProtocolHandlerList* GetHandlerList(const std::string& scheme) const;

  // Install default protocol handlers for chromeos which must be done
  // prior to calling InitProtocolSettings.
  void InstallDefaultsForChromeOS();

  // Makes this ProtocolHandler the default handler for its protocol.
  void SetDefault(const ProtocolHandler& handler);

  // Insert the given ProtocolHandler into the registry.
  void InsertHandler(const ProtocolHandler& handler);

  // Returns a JSON list of protocol handlers. The caller is responsible for
  // deleting this Value.
  Value* EncodeRegisteredHandlers();

  // Returns a JSON list of ignored protocol handlers. The caller is
  // responsible for deleting this Value.
  Value* EncodeIgnoredHandlers();

  // Sends a notification of the given type to the NotificationService.
  void NotifyChanged();

  // Registers a new protocol handler.
  void RegisterProtocolHandler(const ProtocolHandler& handler);

  // Get the DictionaryValues stored under the given pref name that are valid
  // ProtocolHandler values.
  std::vector<const DictionaryValue*> GetHandlersFromPref(
      const char* pref_name) const;

  // Ignores future requests to register the given protocol handler.
  void IgnoreProtocolHandler(const ProtocolHandler& handler);

  // Map from protocols (strings) to protocol handlers.
  ProtocolHandlerMultiMap protocol_handlers_;

  // Protocol handlers that the user has told us to ignore.
  ProtocolHandlerList ignored_protocol_handlers_;

  // Protocol handlers that are the defaults for a given protocol.
  ProtocolHandlerMap default_handlers_;

  // The Profile that owns this ProtocolHandlerRegistry.
  Profile* profile_;

  // The Delegate that registers / deregisters external handlers on our behalf.
  scoped_ptr<Delegate> delegate_;

  // If false then registered protocol handlers will not be used to handle
  // requests.
  bool enabled_;

  // Whether or not we are loading.
  bool is_loading_;

  // When the table gets loaded this flag will be set and any further calls to
  // AddPredefinedHandler will be rejected.
  bool is_loaded_;

  // Copy of registry data for use on the IO thread. Changes to the registry
  // are posted to the IO thread where updates are applied to this object.
  scoped_refptr<IOThreadDelegate> io_thread_delegate_;

  DefaultClientObserverList default_client_observers_;

  DISALLOW_COPY_AND_ASSIGN(ProtocolHandlerRegistry);
};
#endif  // CHROME_BROWSER_CUSTOM_HANDLERS_PROTOCOL_HANDLER_REGISTRY_H_
