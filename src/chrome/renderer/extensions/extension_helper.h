// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_EXTENSIONS_EXTENSION_HELPER_H_
#define CHROME_RENDERER_EXTENSIONS_EXTENSION_HELPER_H_

#include <map>
#include <vector>

#include "base/memory/linked_ptr.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/common/console_message_level.h"
#include "content/public/renderer/render_view_observer.h"
#include "content/public/renderer/render_view_observer_tracker.h"
#include "extensions/common/view_type.h"
#include "third_party/WebKit/public/platform/WebURLResponse.h"

class GURL;
class SkBitmap;
struct ExtensionMsg_ExecuteCode_Params;
struct ExtensionMsg_ExternalConnectionInfo;
struct WebApplicationInfo;

namespace base {
class DictionaryValue;
class ListValue;
}

namespace extensions {
class Dispatcher;

// RenderView-level plumbing for extension features.
class ExtensionHelper
    : public content::RenderViewObserver,
      public content::RenderViewObserverTracker<ExtensionHelper> {
 public:
  // Returns a list of extension RenderViews that match the given filter
  // criteria. If |browser_window_id| is not extension_misc::kUnknownWindowId,
  // the list is restricted to views in that browser window.
  static std::vector<content::RenderView*> GetExtensionViews(
      const std::string& extension_id,
      int browser_window_id,
      ViewType view_type);

  // Returns the given extension's background page, or NULL if none.
  static content::RenderView* GetBackgroundPage(
      const std::string& extension_id);

  ExtensionHelper(content::RenderView* render_view, Dispatcher* dispatcher);
  virtual ~ExtensionHelper();

  int tab_id() const { return tab_id_; }
  int browser_window_id() const { return browser_window_id_; }
  ViewType view_type() const { return view_type_; }
  Dispatcher* dispatcher() const { return dispatcher_; }

 private:
  // RenderViewObserver implementation.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;
  virtual void DidFinishDocumentLoad(WebKit::WebFrame* frame) OVERRIDE;
  virtual void DidFinishLoad(WebKit::WebFrame* frame) OVERRIDE;
  virtual void DidCreateDocumentElement(WebKit::WebFrame* frame) OVERRIDE;
  virtual void DidStartProvisionalLoad(WebKit::WebFrame* frame) OVERRIDE;
  virtual void FrameDetached(WebKit::WebFrame* frame) OVERRIDE;
  virtual void DidCreateDataSource(WebKit::WebFrame* frame,
                                   WebKit::WebDataSource* ds) OVERRIDE;
  virtual void DraggableRegionsChanged(WebKit::WebFrame* frame) OVERRIDE;

  void OnExtensionResponse(int request_id, bool success,
                           const base::ListValue& response,
                           const std::string& error);
  void OnExtensionMessageInvoke(const std::string& extension_id,
                                const std::string& module_name,
                                const std::string& function_name,
                                const base::ListValue& args,
                                bool user_gesture);
  void OnExtensionDispatchOnConnect(
      int target_port_id,
      const std::string& channel_name,
      const base::DictionaryValue& source_tab,
      const ExtensionMsg_ExternalConnectionInfo& info);
  void OnExtensionDeliverMessage(int target_port_id,
                                 const std::string& message);
  void OnExtensionDispatchOnDisconnect(int port_id,
                                       const std::string& error_message);
  void OnExecuteCode(const ExtensionMsg_ExecuteCode_Params& params);
  void OnGetApplicationInfo(int page_id);
  void OnNotifyRendererViewType(ViewType view_type);
  void OnSetTabId(int tab_id);
  void OnUpdateBrowserWindowId(int window_id);
  void OnAddMessageToConsole(content::ConsoleMessageLevel level,
                             const std::string& message);
  void OnAppWindowClosed();

  Dispatcher* dispatcher_;

  // The app info that we are processing. This is used when installing an app
  // via application definition. The in-progress web app is stored here while
  // its manifest and icons are downloaded.
  scoped_ptr<WebApplicationInfo> pending_app_info_;

  // The number of app icon requests outstanding. When this reaches zero, we're
  // done processing an app definition file.
  int pending_app_icon_requests_;

  // Type of view attached with RenderView.
  ViewType view_type_;

  // Id of the tab which the RenderView is attached to.
  int tab_id_;

  // Id number of browser window which RenderView is attached to.
  int browser_window_id_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionHelper);
};

}  // namespace extensions

#endif  // CHROME_RENDERER_EXTENSIONS_EXTENSION_HELPER_H_
