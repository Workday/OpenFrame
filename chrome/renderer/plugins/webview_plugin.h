// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_PLUGINS_WEBVIEW_PLUGIN_H_
#define CHROME_RENDERER_PLUGINS_WEBVIEW_PLUGIN_H_

#include <list>

#include "base/memory/scoped_ptr.h"
#include "base/sequenced_task_runner_helpers.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebURLResponse.h"
#include "third_party/WebKit/public/web/WebCursorInfo.h"
#include "third_party/WebKit/public/web/WebFrameClient.h"
#include "third_party/WebKit/public/web/WebPlugin.h"
#include "third_party/WebKit/public/web/WebViewClient.h"

struct WebPreferences;

namespace WebKit {
class WebMouseEvent;
}

// This class implements the WebPlugin interface by forwarding drawing and
// handling input events to a WebView.
// It can be used as a placeholder for an actual plugin, using HTML for the UI.
// To show HTML data inside the WebViewPlugin,
// call web_view->mainFrame()->loadHTMLString() with the HTML data and a fake
// chrome:// URL as origin.

class WebViewPlugin : public WebKit::WebPlugin,
                      public WebKit::WebViewClient,
                      public WebKit::WebFrameClient {
 public:
  class Delegate {
   public:
    // Bind |frame| to a Javascript object, enabling the delegate to receive
    // callback methods from Javascript inside the WebFrame.
    // This method is called from WebFrameClient::didClearWindowObject.
    virtual void BindWebFrame(WebKit::WebFrame* frame) = 0;

    // Called before the WebViewPlugin is destroyed. The delegate should delete
    // itself here.
    virtual void WillDestroyPlugin() = 0;

    // Called upon a context menu event.
    virtual void ShowContextMenu(const WebKit::WebMouseEvent&) = 0;
  };

  explicit WebViewPlugin(Delegate* delegate);

  // Convenience method to set up a new WebViewPlugin using |preferences|
  // and displaying |html_data|. |url| should be a (fake) chrome:// URL; it is
  // only used for navigation and never actually resolved.
  static WebViewPlugin* Create(
      Delegate* delegate,
      const WebPreferences& preferences,
      const std::string& html_data,
      const GURL& url);

  WebKit::WebView* web_view() { return web_view_; }

  // When loading a plug-in document (i.e. a full page plug-in not embedded in
  // another page), we save all data that has been received, and replay it with
  // this method on the actual plug-in.
  void ReplayReceivedData(WebKit::WebPlugin* plugin);

  void RestoreTitleText();

  // WebPlugin methods:
  virtual WebKit::WebPluginContainer* container() const;
  virtual bool initialize(WebKit::WebPluginContainer*);
  virtual void destroy();

  virtual NPObject* scriptableObject();
  virtual struct _NPP* pluginNPP();

  virtual bool getFormValue(WebKit::WebString& value);

  virtual void paint(WebKit::WebCanvas* canvas, const WebKit::WebRect& rect);

  // Coordinates are relative to the containing window.
  virtual void updateGeometry(
      const WebKit::WebRect& frame_rect, const WebKit::WebRect& clip_rect,
      const WebKit::WebVector<WebKit::WebRect>& cut_out_rects, bool is_visible);

  virtual void updateFocus(bool) {}
  virtual void updateVisibility(bool) {}

  virtual bool acceptsInputEvents();
  virtual bool handleInputEvent(const WebKit::WebInputEvent& event,
                                WebKit::WebCursorInfo& cursor_info);

  virtual void didReceiveResponse(const WebKit::WebURLResponse& response);
  virtual void didReceiveData(const char* data, int data_length);
  virtual void didFinishLoading();
  virtual void didFailLoading(const WebKit::WebURLError& error);

  // Called in response to WebPluginContainer::loadFrameRequest
  virtual void didFinishLoadingFrameRequest(
      const WebKit::WebURL& url, void* notifyData) {}
  virtual void didFailLoadingFrameRequest(const WebKit::WebURL& url,
                                          void* notify_data,
                                          const WebKit::WebURLError& error) {}

  // WebViewClient methods:
  virtual bool acceptsLoadDrops();

  virtual void setToolTipText(const WebKit::WebString&,
                              WebKit::WebTextDirection);

  virtual void startDragging(WebKit::WebFrame* frame,
                             const WebKit::WebDragData& drag_data,
                             WebKit::WebDragOperationsMask mask,
                             const WebKit::WebImage& image,
                             const WebKit::WebPoint& point);

  // WebWidgetClient methods:
  virtual void didInvalidateRect(const WebKit::WebRect&);
  virtual void didChangeCursor(const WebKit::WebCursorInfo& cursor);

  // WebFrameClient methods:
  virtual void didClearWindowObject(WebKit::WebFrame* frame);

  // This method is defined in WebPlugin as well as in WebFrameClient, but with
  // different parameters. We only care about implementing the WebPlugin
  // version, so we implement this method and call the default in WebFrameClient
  // (which does nothing) to correctly overload it.
  virtual void didReceiveResponse(WebKit::WebFrame* frame,
                                  unsigned identifier,
                                  const WebKit::WebURLResponse& response);

 private:
  friend class base::DeleteHelper<WebViewPlugin>;
  virtual ~WebViewPlugin();

  Delegate* delegate_;
  // Destroys itself.
  WebKit::WebCursorInfo current_cursor_;
  // Owns us.
  WebKit::WebPluginContainer* container_;
  // Owned by us, deleted via |close()|.
  WebKit::WebView* web_view_;
  gfx::Rect rect_;

  WebKit::WebURLResponse response_;
  std::list<std::string> data_;
  bool finished_loading_;
  scoped_ptr<WebKit::WebURLError> error_;
  WebKit::WebString old_title_;
};

#endif  // CHROME_RENDERER_PLUGINS_WEBVIEW_PLUGIN_H_
