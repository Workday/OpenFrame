// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_CHROME_RENDER_VIEW_OBSERVER_H_
#define CHROME_RENDERER_CHROME_RENDER_VIEW_OBSERVER_H_

#include <set>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/timer/timer.h"
#include "content/public/common/top_controls_state.h"
#include "content/public/renderer/render_view_observer.h"
#include "url/gurl.h"

class ContentSettingsObserver;
class ExternalHostBindings;
class SkBitmap;

namespace blink {
class WebView;
struct WebWindowFeatures;
}

namespace web_cache {
class WebCacheRenderProcessObserver;
}

// This class holds the Chrome specific parts of RenderView, and has the same
// lifetime.
class ChromeRenderViewObserver : public content::RenderViewObserver {
 public:
  // translate_helper can be NULL.
  ChromeRenderViewObserver(
      content::RenderView* render_view,
      web_cache::WebCacheRenderProcessObserver*
          web_cache_render_process_observer);
  ~ChromeRenderViewObserver() override;

 private:
  // RenderViewObserver implementation.
  bool OnMessageReceived(const IPC::Message& message) override;
  void DidStartLoading() override;
  void Navigate(const GURL& url) override;
  virtual void DidClearWindowObject(blink::WebLocalFrame* frame) override;
  void OnHandleMessageFromExternalHost(const std::string& message,
                                       const std::string& origin,
                                       const std::string& target);
  //void OnJavaScriptStressTestControl(int cmd, int param);

#if !defined(OS_ANDROID) && !defined(OS_IOS)
  void OnWebUIJavaScript(const base::string16& javascript);
#endif
#if defined(ENABLE_EXTENSIONS)
  void OnSetVisuallyDeemphasized(bool deemphasized);
#endif
#if defined(OS_ANDROID)
  void OnUpdateTopControlsState(content::TopControlsState constraints,
                                content::TopControlsState current,
                                bool animate);
#endif
  void OnGetWebApplicationInfo();
  void OnSetWindowFeatures(const blink::WebWindowFeatures& window_features);

  ExternalHostBindings* GetExternalHostBindings();

  // Determines if a host is in the strict security host set.
  bool IsStrictSecurityHost(const std::string& host);

  // Save the JavaScript to preload if a ViewMsg_WebUIJavaScript is received.
  std::vector<base::string16> webui_javascript_;

  // Owned by ChromeContentRendererClient and outlive us.
  web_cache::WebCacheRenderProcessObserver* web_cache_render_process_observer_;

  // true if webview is overlayed with grey color.
  bool webview_visually_deemphasized_;

  // External host exposed through automation controller.
  scoped_ptr<ExternalHostBindings> external_host_bindings_;
  DISALLOW_COPY_AND_ASSIGN(ChromeRenderViewObserver);
};

#endif  // CHROME_RENDERER_CHROME_RENDER_VIEW_OBSERVER_H_
