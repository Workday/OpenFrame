// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/chrome_render_view_observer.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/debug/crash_logging.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/trace_event/trace_event.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/crash_keys.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "chrome/renderer/external_host_bindings.h"
#include "chrome/renderer/prerender/prerender_helper.h"
#include "chrome/renderer/web_apps.h"
#include "components/web_cache/renderer/web_cache_render_process_observer.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/renderer/content_renderer_client.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_view.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebView.h"

#if defined(ENABLE_EXTENSIONS)
#include "chrome/common/extensions/chrome_extension_messages.h"
#endif

using blink::WebFrame;
using blink::WebLocalFrame;
using blink::WebWindowFeatures;

ChromeRenderViewObserver::ChromeRenderViewObserver(
    content::RenderView* render_view,
    web_cache::WebCacheRenderProcessObserver* web_cache_render_process_observer)
    : content::RenderViewObserver(render_view),
      web_cache_render_process_observer_(web_cache_render_process_observer),
      webview_visually_deemphasized_(false) {}

ChromeRenderViewObserver::~ChromeRenderViewObserver() {
}

bool ChromeRenderViewObserver::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ChromeRenderViewObserver, message)
#if !defined(OS_ANDROID) && !defined(OS_IOS)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_WebUIJavaScript, OnWebUIJavaScript)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_HandleMessageFromExternalHost,
                        OnHandleMessageFromExternalHost)
    //IPC_MESSAGE_HANDLER(ChromeViewMsg_JavaScriptStressTestControl,
    //                    OnJavaScriptStressTestControl) //joek: removed -- not sure it's used in chromeframe
#endif
#if defined(ENABLE_EXTENSIONS)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_SetVisuallyDeemphasized,
                        OnSetVisuallyDeemphasized)
#endif
#if defined(OS_ANDROID)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_UpdateTopControlsState,
                        OnUpdateTopControlsState)
#endif
    IPC_MESSAGE_HANDLER(ChromeViewMsg_GetWebApplicationInfo,
                        OnGetWebApplicationInfo)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_SetWindowFeatures, OnSetWindowFeatures)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  return handled;
}

#if !defined(OS_ANDROID) && !defined(OS_IOS)
void ChromeRenderViewObserver::OnWebUIJavaScript(
    const base::string16& javascript) {
  webui_javascript_.push_back(javascript);
}
#endif

void ChromeRenderViewObserver::OnHandleMessageFromExternalHost(
    const std::string& message,
    const std::string& origin,
    const std::string& target) {
  if (message.empty())
    return;
  GetExternalHostBindings()->ForwardMessageFromExternalHost(message, origin,
                                                            target);
}

/*void ChromeRenderViewObserver::OnJavaScriptStressTestControl(int cmd,
                                                             int param) {
  if (cmd == kJavaScriptStressTestSetStressRunType) {
    v8::Testing::SetStressRunType(static_cast<v8::Testing::StressType>(param));
  } else if (cmd == kJavaScriptStressTestPrepareStressRun) {
    v8::Testing::PrepareStressRun(param);
  }
}*/

#if defined(OS_ANDROID)
void ChromeRenderViewObserver::OnUpdateTopControlsState(
    content::TopControlsState constraints,
    content::TopControlsState current,
    bool animate) {
  render_view()->UpdateTopControlsState(constraints, current, animate);
}
#endif

void ChromeRenderViewObserver::OnGetWebApplicationInfo() {
  WebFrame* main_frame = render_view()->GetWebView()->mainFrame();
  DCHECK(main_frame);

  WebApplicationInfo web_app_info;
  web_apps::ParseWebAppFromWebDocument(main_frame, &web_app_info);

  // The warning below is specific to mobile but it doesn't hurt to show it even
  // if the Chromium build is running on a desktop. It will get more exposition.
  if (web_app_info.mobile_capable ==
        WebApplicationInfo::MOBILE_CAPABLE_APPLE) {
    blink::WebConsoleMessage message(
        blink::WebConsoleMessage::LevelWarning,
        "<meta name=\"apple-mobile-web-app-capable\" content=\"yes\"> is "
        "deprecated. Please include <meta name=\"mobile-web-app-capable\" "
        "content=\"yes\"> - "
        "http://developers.google.com/chrome/mobile/docs/installtohomescreen");
    main_frame->addMessageToConsole(message);
  }

  // Prune out any data URLs in the set of icons.  The browser process expects
  // any icon with a data URL to have originated from a favicon.  We don't want
  // to decode arbitrary data URLs in the browser process.  See
  // http://b/issue?id=1162972
  for (std::vector<WebApplicationInfo::IconInfo>::iterator it =
          web_app_info.icons.begin(); it != web_app_info.icons.end();) {
    if (it->url.SchemeIs(url::kDataScheme))
      it = web_app_info.icons.erase(it);
    else
      ++it;
  }

  // Truncate the strings we send to the browser process.
  web_app_info.title =
      web_app_info.title.substr(0, chrome::kMaxMetaTagAttributeLength);
  web_app_info.description =
      web_app_info.description.substr(0, chrome::kMaxMetaTagAttributeLength);

  Send(new ChromeViewHostMsg_DidGetWebApplicationInfo(
      routing_id(), web_app_info));
}

void ChromeRenderViewObserver::OnSetWindowFeatures(
    const WebWindowFeatures& window_features) {
  render_view()->GetWebView()->setWindowFeatures(window_features);
}

void ChromeRenderViewObserver::Navigate(const GURL& url) {
  // Execute cache clear operations that were postponed until a navigation
  // event (including tab reload).
  if (web_cache_render_process_observer_)
    web_cache_render_process_observer_->ExecutePendingClearCache();
}

#if defined(ENABLE_EXTENSIONS)
void ChromeRenderViewObserver::OnSetVisuallyDeemphasized(bool deemphasized) {
  if (webview_visually_deemphasized_ == deemphasized)
    return;

  webview_visually_deemphasized_ = deemphasized;

  if (deemphasized) {
    // 70% opaque grey.
    SkColor greyish = SkColorSetARGB(178, 0, 0, 0);
    render_view()->GetWebView()->setPageOverlayColor(greyish);
  } else {
    render_view()->GetWebView()->setPageOverlayColor(SK_ColorTRANSPARENT);
  }
}
#endif

void ChromeRenderViewObserver::DidStartLoading() {
  if ((render_view()->GetEnabledBindings() & content::BINDINGS_POLICY_WEB_UI) &&
      !webui_javascript_.empty()) {
    for (size_t i = 0; i < webui_javascript_.size(); ++i) {
      render_view()->GetMainRenderFrame()->ExecuteJavaScript(
          webui_javascript_[i]);
    }
    webui_javascript_.clear();
  }
}
void ChromeRenderViewObserver::DidClearWindowObject(WebLocalFrame* frame) {
  if (render_view()->GetEnabledBindings() &
          content::BINDINGS_POLICY_EXTERNAL_HOST) {
    GetExternalHostBindings()->BindToJavascript(frame, "externalHost");
  }
}

ExternalHostBindings* ChromeRenderViewObserver::GetExternalHostBindings() {
  if (!external_host_bindings_.get()) {
    external_host_bindings_.reset(new ExternalHostBindings(
        render_view(), routing_id()));
  }
  return external_host_bindings_.get();
}
