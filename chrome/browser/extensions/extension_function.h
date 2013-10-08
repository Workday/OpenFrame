// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_FUNCTION_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_FUNCTION_H_

#include <list>
#include <string>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/sequenced_task_runner_helpers.h"
#include "chrome/browser/extensions/extension_function_histogram_value.h"
#include "chrome/browser/extensions/extension_info_map.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_view_host_observer.h"
#include "content/public/common/console_message_level.h"
#include "ipc/ipc_message.h"

class Browser;
class ChromeRenderMessageFilter;
class ExtensionFunction;
class ExtensionFunctionDispatcher;
class UIThreadExtensionFunction;
class IOThreadExtensionFunction;
class Profile;
class QuotaLimitHeuristic;

namespace base {
class ListValue;
class Value;
}

namespace content {
class RenderViewHost;
class WebContents;
}

namespace extensions {
class WindowController;
}

#ifdef NDEBUG
#define EXTENSION_FUNCTION_VALIDATE(test) do { \
    if (!(test)) { \
      bad_message_ = true; \
      return false; \
    } \
  } while (0)
#else   // NDEBUG
#define EXTENSION_FUNCTION_VALIDATE(test) CHECK(test)
#endif  // NDEBUG

#define EXTENSION_FUNCTION_ERROR(error) do { \
    error_ = error; \
    bad_message_ = true; \
    return false; \
  } while (0)

// Declares a callable extension function with the given |name|. You must also
// supply a unique |histogramvalue| used for histograms of extension function
// invocation (add new ones at the end of the enum in
// extension_function_histogram_value.h).
#define DECLARE_EXTENSION_FUNCTION(name, histogramvalue) \
  public: static const char* function_name() { return name; } \
  public: static extensions::functions::HistogramValue histogram_value() \
    { return extensions::functions::histogramvalue; }

// Traits that describe how ExtensionFunction should be deleted. This just calls
// the virtual "Destruct" method on ExtensionFunction, allowing derived classes
// to override the behavior.
struct ExtensionFunctionDeleteTraits {
 public:
  static void Destruct(const ExtensionFunction* x);
};

// Abstract base class for extension functions the ExtensionFunctionDispatcher
// knows how to dispatch to.
class ExtensionFunction
    : public base::RefCountedThreadSafe<ExtensionFunction,
                                        ExtensionFunctionDeleteTraits> {
 public:
  enum ResponseType {
    // The function has succeeded.
    SUCCEEDED,
    // The function has failed.
    FAILED,
    // The input message is malformed.
    BAD_MESSAGE
  };

  typedef base::Callback<void(ResponseType type,
                              const base::ListValue& results,
                              const std::string& error)> ResponseCallback;

  ExtensionFunction();

  virtual UIThreadExtensionFunction* AsUIThreadExtensionFunction();
  virtual IOThreadExtensionFunction* AsIOThreadExtensionFunction();

  // Returns true if the function has permission to run.
  //
  // The default implementation is to check the Extension's permissions against
  // what this function requires to run, but some APIs may require finer
  // grained control, such as tabs.executeScript being allowed for active tabs.
  //
  // This will be run after the function has been set up but before Run().
  virtual bool HasPermission();

  // Execute the API. Clients should initialize the ExtensionFunction using
  // SetArgs(), set_request_id(), and the other setters before calling this
  // method. Derived classes should be ready to return GetResultList() and
  // GetError() before returning from this function.
  // Note that once Run() returns, dispatcher() can be NULL, so be sure to
  // NULL-check.
  virtual void Run();

  // Gets whether quota should be applied to this individual function
  // invocation. This is different to GetQuotaLimitHeuristics which is only
  // invoked once and then cached.
  //
  // Returns false by default.
  virtual bool ShouldSkipQuotaLimiting() const;

  // Optionally adds one or multiple QuotaLimitHeuristic instances suitable for
  // this function to |heuristics|. The ownership of the new QuotaLimitHeuristic
  // instances is passed to the owner of |heuristics|.
  // No quota limiting by default.
  //
  // Only called once per lifetime of the ExtensionsQuotaService.
  virtual void GetQuotaLimitHeuristics(
      QuotaLimitHeuristics* heuristics) const {}

  // Called when the quota limit has been exceeded. The default implementation
  // returns an error.
  virtual void OnQuotaExceeded(const std::string& violation_error);

  // Specifies the raw arguments to the function, as a JSON value.
  virtual void SetArgs(const base::ListValue* args);

  // Sets a single Value as the results of the function.
  void SetResult(base::Value* result);

  // Retrieves the results of the function as a ListValue.
  const base::ListValue* GetResultList();

  // Retrieves any error string from the function.
  virtual const std::string GetError();

  // Sets the function's error string.
  virtual void SetError(const std::string& error);

  // Specifies the name of the function.
  void set_name(const std::string& name) { name_ = name; }
  const std::string& name() const { return name_; }

  void set_profile_id(void* profile_id) { profile_id_ = profile_id; }
  void* profile_id() const { return profile_id_; }

  void set_extension(const extensions::Extension* extension) {
    extension_ = extension;
  }
  const extensions::Extension* GetExtension() const { return extension_.get(); }
  const std::string& extension_id() const { return extension_->id(); }

  void set_request_id(int request_id) { request_id_ = request_id; }
  int request_id() { return request_id_; }

  void set_source_url(const GURL& source_url) { source_url_ = source_url; }
  const GURL& source_url() { return source_url_; }

  void set_has_callback(bool has_callback) { has_callback_ = has_callback; }
  bool has_callback() { return has_callback_; }

  void set_include_incognito(bool include) { include_incognito_ = include; }
  bool include_incognito() const { return include_incognito_; }

  void set_user_gesture(bool user_gesture) { user_gesture_ = user_gesture; }
  bool user_gesture() const { return user_gesture_; }

  void set_histogram_value(
      extensions::functions::HistogramValue histogram_value) {
    histogram_value_ = histogram_value; }
  extensions::functions::HistogramValue histogram_value() const {
    return histogram_value_; }

  void set_response_callback(const ResponseCallback& callback) {
    response_callback_ = callback;
  }

 protected:
  friend struct ExtensionFunctionDeleteTraits;

  virtual ~ExtensionFunction();

  // Helper method for ExtensionFunctionDeleteTraits. Deletes this object.
  virtual void Destruct() const = 0;

  // Derived classes should implement this method to do their work and return
  // success/failure.
  virtual bool RunImpl() = 0;

  // Sends the result back to the extension.
  virtual void SendResponse(bool success) = 0;

  // Common implementation for SendResponse.
  void SendResponseImpl(bool success);

  // Return true if the argument to this function at |index| was provided and
  // is non-null.
  bool HasOptionalArgument(size_t index);

  // Id of this request, used to map the response back to the caller.
  int request_id_;

  // The Profile of this function's extension.
  void* profile_id_;

  // The extension that called this function.
  scoped_refptr<const extensions::Extension> extension_;

  // The name of this function.
  std::string name_;

  // The URL of the frame which is making this request
  GURL source_url_;

  // True if the js caller provides a callback function to receive the response
  // of this call.
  bool has_callback_;

  // True if this callback should include information from incognito contexts
  // even if our profile_ is non-incognito. Note that in the case of a "split"
  // mode extension, this will always be false, and we will limit access to
  // data from within the same profile_ (either incognito or not).
  bool include_incognito_;

  // True if the call was made in response of user gesture.
  bool user_gesture_;

  // The arguments to the API. Only non-null if argument were specified.
  scoped_ptr<base::ListValue> args_;

  // The results of the API. This should be populated by the derived class
  // before SendResponse() is called.
  scoped_ptr<base::ListValue> results_;

  // Any detailed error from the API. This should be populated by the derived
  // class before Run() returns.
  std::string error_;

  // Any class that gets a malformed message should set this to true before
  // returning.  Usually we want to kill the message sending process.
  bool bad_message_;

  // The sample value to record with the histogram API when the function
  // is invoked.
  extensions::functions::HistogramValue histogram_value_;

  // The callback to run once the function has done execution.
  ResponseCallback response_callback_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionFunction);
};

// Extension functions that run on the UI thread. Most functions fall into
// this category.
class UIThreadExtensionFunction : public ExtensionFunction {
 public:
  // TODO(yzshen): We should be able to remove this interface now that we
  // support overriding the response callback.
  // A delegate for use in testing, to intercept the call to SendResponse.
  class DelegateForTests {
   public:
    virtual void OnSendResponse(UIThreadExtensionFunction* function,
                                bool success,
                                bool bad_message) = 0;
  };

  UIThreadExtensionFunction();

  virtual UIThreadExtensionFunction* AsUIThreadExtensionFunction() OVERRIDE;

  void set_test_delegate(DelegateForTests* delegate) {
    delegate_ = delegate;
  }

  // Called when a message was received.
  // Should return true if it processed the message.
  virtual bool OnMessageReceivedFromRenderView(const IPC::Message& message);

  // Set the profile which contains the extension that has originated this
  // function call.
  void set_profile(Profile* profile) { profile_ = profile; }
  Profile* profile() const { return profile_; }

  void SetRenderViewHost(content::RenderViewHost* render_view_host);
  content::RenderViewHost* render_view_host() const {
    return render_view_host_;
  }

  void set_dispatcher(
      const base::WeakPtr<ExtensionFunctionDispatcher>& dispatcher) {
    dispatcher_ = dispatcher;
  }
  ExtensionFunctionDispatcher* dispatcher() const {
    return dispatcher_.get();
  }

  // Gets the "current" browser, if any.
  //
  // Many extension APIs operate relative to the current browser, which is the
  // browser the calling code is running inside of. For example, popups, tabs,
  // and infobars all have a containing browser, but background pages and
  // notification bubbles do not.
  //
  // If there is no containing window, the current browser defaults to the
  // foremost one.
  //
  // Incognito browsers are not considered unless the calling extension has
  // incognito access enabled.
  //
  // This method can return NULL if there is no matching browser, which can
  // happen if only incognito windows are open, or early in startup or shutdown
  // shutdown when there are no active windows.
  //
  // TODO(stevenjb): Replace this with GetExtensionWindowController().
  Browser* GetCurrentBrowser();

  // Gets the "current" web contents if any. If there is no associated web
  // contents then defaults to the foremost one.
  content::WebContents* GetAssociatedWebContents();

  // Same as above but uses WindowControllerList instead of BrowserList.
  extensions::WindowController* GetExtensionWindowController();

  // Returns true if this function (and the profile and extension that it was
  // invoked from) can operate on the window wrapped by |window_controller|.
  bool CanOperateOnWindow(
      const extensions::WindowController* window_controller) const;

 protected:
  // Emits a message to the extension's devtools console.
  void WriteToConsole(content::ConsoleMessageLevel level,
                      const std::string& message);

  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::UI>;
  friend class base::DeleteHelper<UIThreadExtensionFunction>;

  virtual ~UIThreadExtensionFunction();

  virtual void SendResponse(bool success) OVERRIDE;

  // The dispatcher that will service this extension function call.
  base::WeakPtr<ExtensionFunctionDispatcher> dispatcher_;

  // The RenderViewHost we will send responses too.
  content::RenderViewHost* render_view_host_;

  // The Profile of this function's extension.
  Profile* profile_;

 private:
  // Helper class to track the lifetime of ExtensionFunction's RenderViewHost
  // pointer and NULL it out when it dies. It also allows us to filter IPC
  // messages coming from the RenderViewHost.
  class RenderViewHostTracker : public content::RenderViewHostObserver {
   public:
    explicit RenderViewHostTracker(UIThreadExtensionFunction* function);

   private:
    // content::RenderViewHostObserver:
    virtual void RenderViewHostDestroyed(
        content::RenderViewHost* render_view_host) OVERRIDE;
    virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

    UIThreadExtensionFunction* function_;

    DISALLOW_COPY_AND_ASSIGN(RenderViewHostTracker);
  };

  virtual void Destruct() const OVERRIDE;

  scoped_ptr<RenderViewHostTracker> tracker_;

  DelegateForTests* delegate_;
};

// Extension functions that run on the IO thread. This type of function avoids
// a roundtrip to and from the UI thread (because communication with the
// extension process happens on the IO thread). It's intended to be used when
// performance is critical (e.g. the webRequest API which can block network
// requests). Generally, UIThreadExtensionFunction is more appropriate and will
// be easier to use and interface with the rest of the browser.
class IOThreadExtensionFunction : public ExtensionFunction {
 public:
  IOThreadExtensionFunction();

  virtual IOThreadExtensionFunction* AsIOThreadExtensionFunction() OVERRIDE;

  void set_ipc_sender(base::WeakPtr<ChromeRenderMessageFilter> ipc_sender,
                      int routing_id) {
    ipc_sender_ = ipc_sender;
    routing_id_ = routing_id;
  }

  base::WeakPtr<ChromeRenderMessageFilter> ipc_sender_weak() const {
    return ipc_sender_;
  }

  int routing_id() const { return routing_id_; }

  void set_extension_info_map(const ExtensionInfoMap* extension_info_map) {
    extension_info_map_ = extension_info_map;
  }
  const ExtensionInfoMap* extension_info_map() const {
    return extension_info_map_.get();
  }

 protected:
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::IO>;
  friend class base::DeleteHelper<IOThreadExtensionFunction>;

  virtual ~IOThreadExtensionFunction();

  virtual void Destruct() const OVERRIDE;

  virtual void SendResponse(bool success) OVERRIDE;

 private:
  base::WeakPtr<ChromeRenderMessageFilter> ipc_sender_;
  int routing_id_;

  scoped_refptr<const ExtensionInfoMap> extension_info_map_;
};

// Base class for an extension function that runs asynchronously *relative to
// the browser's UI thread*.
class AsyncExtensionFunction : public UIThreadExtensionFunction {
 public:
  AsyncExtensionFunction();

 protected:
  virtual ~AsyncExtensionFunction();
};

// A SyncExtensionFunction is an ExtensionFunction that runs synchronously
// *relative to the browser's UI thread*. Note that this has nothing to do with
// running synchronously relative to the extension process. From the extension
// process's point of view, the function is still asynchronous.
//
// This kind of function is convenient for implementing simple APIs that just
// need to interact with things on the browser UI thread.
class SyncExtensionFunction : public UIThreadExtensionFunction {
 public:
  SyncExtensionFunction();

  virtual void Run() OVERRIDE;

 protected:
  virtual ~SyncExtensionFunction();
};

class SyncIOThreadExtensionFunction : public IOThreadExtensionFunction {
 public:
  SyncIOThreadExtensionFunction();

  virtual void Run() OVERRIDE;

 protected:
  virtual ~SyncIOThreadExtensionFunction();
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_FUNCTION_H_
