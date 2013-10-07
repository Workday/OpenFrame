// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/render_view_observer_natives.h"

#include "chrome/common/extensions/api/extension_api.h"
#include "chrome/renderer/extensions/dispatcher.h"
#include "content/public/renderer/render_view.h"
#include "content/public/renderer/render_view_observer.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebScopedMicrotaskSuppression.h"

namespace extensions {

namespace {

// Deletes itself when done.
class LoadWatcher : public content::RenderViewObserver {
 public:
  LoadWatcher(ChromeV8Context* context,
              content::RenderView* view,
              v8::Handle<v8::Function> cb)
      : content::RenderViewObserver(view),
        context_(context),
        callback_(cb) {
  }

  virtual void DidCreateDocumentElement(WebKit::WebFrame* frame) OVERRIDE {
    CallbackAndDie(true);
  }

  virtual void DidFailProvisionalLoad(
      WebKit::WebFrame* frame,
      const WebKit::WebURLError& error) OVERRIDE {
    CallbackAndDie(false);
  }

 private:
  void CallbackAndDie(bool succeeded) {
    v8::HandleScope handle_scope;
    v8::Handle<v8::Value> args[] = { v8::Boolean::New(succeeded) };
    context_->CallFunction(callback_.get(), 1, args);
    delete this;
  }

  ChromeV8Context* context_;
  ScopedPersistent<v8::Function> callback_;
  DISALLOW_COPY_AND_ASSIGN(LoadWatcher);
};
}  // namespace


RenderViewObserverNatives::RenderViewObserverNatives(Dispatcher* dispatcher,
                                                     ChromeV8Context* context)
    : ChromeV8Extension(dispatcher, context) {
  RouteFunction("OnDocumentElementCreated",
                base::Bind(&RenderViewObserverNatives::OnDocumentElementCreated,
                           base::Unretained(this)));
}

void RenderViewObserverNatives::OnDocumentElementCreated(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  CHECK(args.Length() == 2);
  CHECK(args[0]->IsInt32());
  CHECK(args[1]->IsFunction());

  int view_id = args[0]->Int32Value();

  content::RenderView* view = content::RenderView::FromRoutingID(view_id);
  if (!view) {
    LOG(WARNING) << "No render view found to register LoadWatcher.";
    return;
  }

  new LoadWatcher(context(), view, args[1].As<v8::Function>());

  args.GetReturnValue().Set(true);
}

}  // namespace extensions
