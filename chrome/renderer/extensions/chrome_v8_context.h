// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_EXTENSIONS_CHROME_V8_CONTEXT_H_
#define CHROME_RENDERER_EXTENSIONS_CHROME_V8_CONTEXT_H_

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/common/extensions/features/feature.h"
#include "chrome/renderer/extensions/module_system.h"
#include "chrome/renderer/extensions/request_sender.h"
#include "chrome/renderer/extensions/safe_builtins.h"
#include "chrome/renderer/extensions/scoped_persistent.h"
#include "v8/include/v8.h"

namespace WebKit {
class WebFrame;
}

namespace content {
class RenderView;
}

namespace extensions {
class Extension;

// Chrome's wrapper for a v8 context.
class ChromeV8Context : public RequestSender::Source {
 public:
  ChromeV8Context(v8::Handle<v8::Context> context,
                  WebKit::WebFrame* frame,
                  const Extension* extension,
                  Feature::Context context_type);
  virtual ~ChromeV8Context();

  // Clears the WebFrame for this contexts and invalidates the associated
  // ModuleSystem.
  void Invalidate();

  // Returns true if this context is still valid, false if it isn't.
  // A context becomes invalid via Invalidate().
  bool is_valid() const {
    return !v8_context_.get().IsEmpty();
  }

  v8::Handle<v8::Context> v8_context() const {
    return v8_context_.get();
  }

  const Extension* extension() const {
    return extension_.get();
  }

  WebKit::WebFrame* web_frame() const {
    return web_frame_;
  }

  Feature::Context context_type() const {
    return context_type_;
  }

  void set_module_system(scoped_ptr<ModuleSystem> module_system) {
    module_system_ = module_system.Pass();
  }

  ModuleSystem* module_system() { return module_system_.get(); }

  SafeBuiltins* safe_builtins() {
    return &safe_builtins_;
  }
  const SafeBuiltins* safe_builtins() const {
    return &safe_builtins_;
  }

  // Returns the ID of the extension associated with this context, or empty
  // string if there is no such extension.
  std::string GetExtensionID() const;

  // Returns the RenderView associated with this context. Can return NULL if the
  // context is in the process of being destroyed.
  content::RenderView* GetRenderView() const;

  // Get the URL of this context's web frame.
  GURL GetURL() const;

  // Runs |function| with appropriate scopes. Doesn't catch exceptions, callers
  // must do that if they want.
  //
  // USE THIS METHOD RATHER THAN v8::Function::Call WHEREVER POSSIBLE.
  v8::Local<v8::Value> CallFunction(v8::Handle<v8::Function> function,
                                    int argc,
                                    v8::Handle<v8::Value> argv[]) const;

  // Fires the onunload event on the unload_event module.
  void DispatchOnUnloadEvent();

  // Returns the availability of the API |api_name|.
  Feature::Availability GetAvailability(const std::string& api_name);

  // Returns whether the API |api_name| or any part of the API could be
  // available in this context without taking into account the context's
  // extension.
  bool IsAnyFeatureAvailableToContext(const std::string& api_name);

  // Returns a string description of the type of context this is.
  std::string GetContextTypeDescription();

  // RequestSender::Source implementation.
  virtual ChromeV8Context* GetContext() OVERRIDE;
  virtual void OnResponseReceived(const std::string& name,
                                  int request_id,
                                  bool success,
                                  const base::ListValue& response,
                                  const std::string& error) OVERRIDE;

 private:
  // The v8 context the bindings are accessible to.
  ScopedPersistent<v8::Context> v8_context_;

  // The WebFrame associated with this context. This can be NULL because this
  // object can outlive is destroyed asynchronously.
  WebKit::WebFrame* web_frame_;

  // The extension associated with this context, or NULL if there is none. This
  // might be a hosted app in the case that this context is hosting a web URL.
  scoped_refptr<const Extension> extension_;

  // The type of context.
  Feature::Context context_type_;

  // Owns and structures the JS that is injected to set up extension bindings.
  scoped_ptr<ModuleSystem> module_system_;

  // Contains safe copies of builtin objects like Function.prototype.
  SafeBuiltins safe_builtins_;

  DISALLOW_COPY_AND_ASSIGN(ChromeV8Context);
};

}  // namespace extensions

#endif  // CHROME_RENDERER_EXTENSIONS_CHROME_V8_CONTEXT_H_
