// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/chrome_render_view_observer.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/debug/trace_event.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/prerender_messages.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "chrome/renderer/chrome_render_process_observer.h"
#include "chrome/renderer/content_settings_observer.h"
#include "chrome/renderer/extensions/dispatcher.h"
#include "chrome/renderer/external_host_bindings.h"
#include "chrome/renderer/frame_sniffer.h"
#include "chrome/renderer/prerender/prerender_helper.h"
#include "chrome/renderer/safe_browsing/phishing_classifier_delegate.h"
#include "chrome/renderer/translate/translate_helper.h"
#include "chrome/renderer/webview_color_overlay.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/renderer/content_renderer_client.h"
#include "content/public/renderer/render_view.h"
#include "extensions/common/constants.h"
#include "net/base/data_url.h"
#include "skia/ext/image_operations.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/WebKit/public/platform/WebCString.h"
#include "third_party/WebKit/public/platform/WebRect.h"
#include "third_party/WebKit/public/platform/WebSize.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/public/platform/WebVector.h"
#include "third_party/WebKit/public/web/WebAccessibilityObject.h"
#include "third_party/WebKit/public/web/WebDataSource.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebElement.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebInputEvent.h"
#include "third_party/WebKit/public/web/WebNode.h"
#include "third_party/WebKit/public/web/WebNodeList.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "ui/base/ui_base_switches_util.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/size.h"
#include "ui/gfx/size_f.h"
#include "ui/gfx/skbitmap_operations.h"
#include "v8/include/v8-testing.h"
#include "webkit/glue/webkit_glue.h"

using WebKit::WebAccessibilityObject;
using WebKit::WebCString;
using WebKit::WebDataSource;
using WebKit::WebDocument;
using WebKit::WebElement;
using WebKit::WebFrame;
using WebKit::WebGestureEvent;
using WebKit::WebIconURL;
using WebKit::WebNode;
using WebKit::WebNodeList;
using WebKit::WebRect;
using WebKit::WebSecurityOrigin;
using WebKit::WebSize;
using WebKit::WebString;
using WebKit::WebTouchEvent;
using WebKit::WebURL;
using WebKit::WebURLRequest;
using WebKit::WebView;
using WebKit::WebVector;
using WebKit::WebWindowFeatures;
using extensions::APIPermission;

// Delay in milliseconds that we'll wait before capturing the page contents
// and thumbnail.
static const int kDelayForCaptureMs = 500;

// Typically, we capture the page data once the page is loaded.
// Sometimes, the page never finishes to load, preventing the page capture
// To workaround this problem, we always perform a capture after the following
// delay.
static const int kDelayForForcedCaptureMs = 6000;

// define to write the time necessary for thumbnail/DOM text retrieval,
// respectively, into the system debug log
// #define TIME_TEXT_RETRIEVAL

// maximum number of characters in the document to index, any text beyond this
// point will be clipped
static const size_t kMaxIndexChars = 65535;

// Constants for UMA statistic collection.
static const char kWWWDotGoogleDotCom[] = "www.google.com";
static const char kMailDotGoogleDotCom[] = "mail.google.com";
static const char kPlusDotGoogleDotCom[] = "plus.google.com";
static const char kDocsDotGoogleDotCom[] = "docs.google.com";
static const char kSitesDotGoogleDotCom[] = "sites.google.com";
static const char kPicasawebDotGoogleDotCom[] = "picasaweb.google.com";
static const char kCodeDotGoogleDotCom[] = "code.google.com";
static const char kGroupsDotGoogleDotCom[] = "groups.google.com";
static const char kMapsDotGoogleDotCom[] = "maps.google.com";
static const char kWWWDotYoutubeDotCom[] = "www.youtube.com";
static const char kDotGoogleUserContentDotCom[] = ".googleusercontent.com";
static const char kGoogleReaderPathPrefix[] = "/reader/";
static const char kGoogleSupportPathPrefix[] = "/support/";
static const char kGoogleIntlPathPrefix[] = "/intl/";
static const char kDotJS[] = ".js";
static const char kDotCSS[] = ".css";
static const char kDotSWF[] = ".swf";
static const char kDotHTML[] = ".html";
static const char kTranslateCaptureText[] = "Translate.CaptureText";
enum {
  INSECURE_CONTENT_DISPLAY = 0,
  INSECURE_CONTENT_DISPLAY_HOST_GOOGLE,
  INSECURE_CONTENT_DISPLAY_HOST_WWW_GOOGLE,
  INSECURE_CONTENT_DISPLAY_HTML,
  INSECURE_CONTENT_RUN,
  INSECURE_CONTENT_RUN_HOST_GOOGLE,
  INSECURE_CONTENT_RUN_HOST_WWW_GOOGLE,
  INSECURE_CONTENT_RUN_TARGET_YOUTUBE,
  INSECURE_CONTENT_RUN_JS,
  INSECURE_CONTENT_RUN_CSS,
  INSECURE_CONTENT_RUN_SWF,
  INSECURE_CONTENT_DISPLAY_HOST_YOUTUBE,
  INSECURE_CONTENT_RUN_HOST_YOUTUBE,
  INSECURE_CONTENT_RUN_HOST_GOOGLEUSERCONTENT,
  INSECURE_CONTENT_DISPLAY_HOST_MAIL_GOOGLE,
  INSECURE_CONTENT_RUN_HOST_MAIL_GOOGLE,
  INSECURE_CONTENT_DISPLAY_HOST_PLUS_GOOGLE,
  INSECURE_CONTENT_RUN_HOST_PLUS_GOOGLE,
  INSECURE_CONTENT_DISPLAY_HOST_DOCS_GOOGLE,
  INSECURE_CONTENT_RUN_HOST_DOCS_GOOGLE,
  INSECURE_CONTENT_DISPLAY_HOST_SITES_GOOGLE,
  INSECURE_CONTENT_RUN_HOST_SITES_GOOGLE,
  INSECURE_CONTENT_DISPLAY_HOST_PICASAWEB_GOOGLE,
  INSECURE_CONTENT_RUN_HOST_PICASAWEB_GOOGLE,
  INSECURE_CONTENT_DISPLAY_HOST_GOOGLE_READER,
  INSECURE_CONTENT_RUN_HOST_GOOGLE_READER,
  INSECURE_CONTENT_DISPLAY_HOST_CODE_GOOGLE,
  INSECURE_CONTENT_RUN_HOST_CODE_GOOGLE,
  INSECURE_CONTENT_DISPLAY_HOST_GROUPS_GOOGLE,
  INSECURE_CONTENT_RUN_HOST_GROUPS_GOOGLE,
  INSECURE_CONTENT_DISPLAY_HOST_MAPS_GOOGLE,
  INSECURE_CONTENT_RUN_HOST_MAPS_GOOGLE,
  INSECURE_CONTENT_DISPLAY_HOST_GOOGLE_SUPPORT,
  INSECURE_CONTENT_RUN_HOST_GOOGLE_SUPPORT,
  INSECURE_CONTENT_DISPLAY_HOST_GOOGLE_INTL,
  INSECURE_CONTENT_RUN_HOST_GOOGLE_INTL,
  INSECURE_CONTENT_NUM_EVENTS
};

// Constants for mixed-content blocking.
static const char kGoogleDotCom[] = "google.com";

static bool isHostInDomain(const std::string& host, const std::string& domain) {
  return (EndsWith(host, domain, false) &&
          (host.length() == domain.length() ||
           (host.length() > domain.length() &&
            host[host.length() - domain.length() - 1] == '.')));
}

namespace {
GURL StripRef(const GURL& url) {
  GURL::Replacements replacements;
  replacements.ClearRef();
  return url.ReplaceComponents(replacements);
}

// If the source image is null or occupies less area than
// |thumbnail_min_area_pixels|, we return the image unmodified.  Otherwise, we
// scale down the image so that the width and height do not exceed
// |thumbnail_max_size_pixels|, preserving the original aspect ratio.
static SkBitmap Downscale(WebKit::WebImage image,
                          int thumbnail_min_area_pixels,
                          gfx::Size thumbnail_max_size_pixels) {
  if (image.isNull())
    return SkBitmap();

  gfx::Size image_size = image.size();

  if (image_size.GetArea() < thumbnail_min_area_pixels)
    return image.getSkBitmap();

  if (image_size.width() <= thumbnail_max_size_pixels.width() &&
      image_size.height() <= thumbnail_max_size_pixels.height())
    return image.getSkBitmap();

  gfx::SizeF scaled_size = image_size;

  if (scaled_size.width() > thumbnail_max_size_pixels.width()) {
    scaled_size.Scale(thumbnail_max_size_pixels.width() / scaled_size.width());
  }

  if (scaled_size.height() > thumbnail_max_size_pixels.height()) {
    scaled_size.Scale(
        thumbnail_max_size_pixels.height() / scaled_size.height());
  }

  return skia::ImageOperations::Resize(image.getSkBitmap(),
                                       skia::ImageOperations::RESIZE_GOOD,
                                       static_cast<int>(scaled_size.width()),
                                       static_cast<int>(scaled_size.height()));
}
}  // namespace

ChromeRenderViewObserver::ChromeRenderViewObserver(
    content::RenderView* render_view,
    ContentSettingsObserver* content_settings,
    ChromeRenderProcessObserver* chrome_render_process_observer,
    extensions::Dispatcher* extension_dispatcher)
    : content::RenderViewObserver(render_view),
      chrome_render_process_observer_(chrome_render_process_observer),
      extension_dispatcher_(extension_dispatcher),
      content_settings_(content_settings),
      translate_helper_(new TranslateHelper(render_view)),
      phishing_classifier_(NULL),
      last_indexed_page_id_(-1),
      allow_displaying_insecure_content_(false),
      allow_running_insecure_content_(false),
      capture_timer_(false, false) {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  render_view->GetWebView()->setPermissionClient(this);
  if (!command_line.HasSwitch(switches::kDisableClientSidePhishingDetection))
    OnSetClientSidePhishingDetection(true);
}

ChromeRenderViewObserver::~ChromeRenderViewObserver() {
}

bool ChromeRenderViewObserver::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ChromeRenderViewObserver, message)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_WebUIJavaScript, OnWebUIJavaScript)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_HandleMessageFromExternalHost,
                        OnHandleMessageFromExternalHost)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_JavaScriptStressTestControl,
                        OnJavaScriptStressTestControl)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_SetAllowDisplayingInsecureContent,
                        OnSetAllowDisplayingInsecureContent)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_SetAllowRunningInsecureContent,
                        OnSetAllowRunningInsecureContent)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_SetClientSidePhishingDetection,
                        OnSetClientSidePhishingDetection)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_SetVisuallyDeemphasized,
                        OnSetVisuallyDeemphasized)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_RequestThumbnailForContextNode,
                        OnRequestThumbnailForContextNode)
#if defined(OS_CHROMEOS)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_StartFrameSniffer, OnStartFrameSniffer)
#endif
    IPC_MESSAGE_HANDLER(ChromeViewMsg_GetFPS, OnGetFPS)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_AddStrictSecurityHost,
                        OnAddStrictSecurityHost)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_NPAPINotSupported, OnNPAPINotSupported)
#if defined(OS_ANDROID)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_UpdateTopControlsState,
                        OnUpdateTopControlsState)
#endif
    IPC_MESSAGE_HANDLER(ChromeViewMsg_SetWindowFeatures, OnSetWindowFeatures)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  // Filter only.
  IPC_BEGIN_MESSAGE_MAP(ChromeRenderViewObserver, message)
    IPC_MESSAGE_HANDLER(PrerenderMsg_SetIsPrerendering, OnSetIsPrerendering);
  IPC_END_MESSAGE_MAP()

  return handled;
}

void ChromeRenderViewObserver::OnWebUIJavaScript(
    const string16& frame_xpath,
    const string16& jscript,
    int id,
    bool notify_result) {
  webui_javascript_.reset(new WebUIJavaScript());
  webui_javascript_->frame_xpath = frame_xpath;
  webui_javascript_->jscript = jscript;
  webui_javascript_->id = id;
  webui_javascript_->notify_result = notify_result;
}

void ChromeRenderViewObserver::OnHandleMessageFromExternalHost(
    const std::string& message,
    const std::string& origin,
    const std::string& target) {
  if (message.empty())
    return;
  GetExternalHostBindings()->ForwardMessageFromExternalHost(message, origin,
                                                            target);
}

void ChromeRenderViewObserver::OnJavaScriptStressTestControl(int cmd,
                                                             int param) {
  if (cmd == kJavaScriptStressTestSetStressRunType) {
    v8::Testing::SetStressRunType(static_cast<v8::Testing::StressType>(param));
  } else if (cmd == kJavaScriptStressTestPrepareStressRun) {
    v8::Testing::PrepareStressRun(param);
  }
}

void ChromeRenderViewObserver::OnSetAllowDisplayingInsecureContent(bool allow) {
  allow_displaying_insecure_content_ = allow;
  WebFrame* main_frame = render_view()->GetWebView()->mainFrame();
  if (main_frame)
    main_frame->reload();
}

void ChromeRenderViewObserver::OnSetAllowRunningInsecureContent(bool allow) {
  allow_running_insecure_content_ = allow;
  OnSetAllowDisplayingInsecureContent(allow);
}

void ChromeRenderViewObserver::OnAddStrictSecurityHost(
    const std::string& host) {
  strict_security_hosts_.insert(host);
}

void ChromeRenderViewObserver::OnNPAPINotSupported() {
#if defined(USE_AURA) && defined(OS_WIN)
  content_settings_->BlockNPAPIPlugins();
#else
  NOTREACHED();
#endif
}

#if defined(OS_ANDROID)
void ChromeRenderViewObserver::OnUpdateTopControlsState(
    content::TopControlsState constraints,
    content::TopControlsState current,
    bool animate) {
  render_view()->UpdateTopControlsState(constraints, current, animate);
}
#endif

void ChromeRenderViewObserver::OnSetWindowFeatures(
    const WebWindowFeatures& window_features) {
  render_view()->GetWebView()->setWindowFeatures(window_features);
}

void ChromeRenderViewObserver::Navigate(const GURL& url) {
  // Execute cache clear operations that were postponed until a navigation
  // event (including tab reload).
  if (chrome_render_process_observer_)
    chrome_render_process_observer_->ExecutePendingClearCache();
}

void ChromeRenderViewObserver::OnSetClientSidePhishingDetection(
    bool enable_phishing_detection) {
#if defined(FULL_SAFE_BROWSING) && !defined(OS_CHROMEOS)
  phishing_classifier_ = enable_phishing_detection ?
      safe_browsing::PhishingClassifierDelegate::Create(
          render_view(), NULL) :
      NULL;
#endif
}

void ChromeRenderViewObserver::OnSetVisuallyDeemphasized(bool deemphasized) {
  bool already_deemphasized = !!dimmed_color_overlay_.get();
  if (already_deemphasized == deemphasized)
    return;

  if (deemphasized) {
    // 70% opaque grey.
    SkColor greyish = SkColorSetARGB(178, 0, 0, 0);
    dimmed_color_overlay_.reset(
        new WebViewColorOverlay(render_view(), greyish));
  } else {
    dimmed_color_overlay_.reset();
  }
}

void ChromeRenderViewObserver::OnRequestThumbnailForContextNode(
    int thumbnail_min_area_pixels, gfx::Size thumbnail_max_size_pixels) {
  WebNode context_node = render_view()->GetContextMenuNode();
  SkBitmap thumbnail;
  if (context_node.isElementNode()) {
    WebKit::WebImage image = context_node.to<WebElement>().imageContents();
    thumbnail = Downscale(image,
                          thumbnail_min_area_pixels,
                          thumbnail_max_size_pixels);
  }
  Send(new ChromeViewHostMsg_RequestThumbnailForContextNode_ACK(routing_id(),
                                                                thumbnail));
}

void ChromeRenderViewObserver::OnStartFrameSniffer(const string16& frame_name) {
  new FrameSniffer(render_view(), frame_name);
}

void ChromeRenderViewObserver::OnGetFPS() {
  float fps = (render_view()->GetFilteredTimePerFrame() > 0.0f)?
      1.0f / render_view()->GetFilteredTimePerFrame() : 0.0f;
  Send(new ChromeViewHostMsg_FPS(routing_id(), fps));
}

bool ChromeRenderViewObserver::allowDatabase(
    WebFrame* frame,
    const WebString& name,
    const WebString& display_name,
    unsigned long estimated_size) {
  return content_settings_->AllowDatabase(
      frame, name, display_name, estimated_size);
}

bool ChromeRenderViewObserver::allowFileSystem(WebFrame* frame) {
  return content_settings_->AllowFileSystem(frame);
}

bool ChromeRenderViewObserver::allowImage(WebFrame* frame,
                                          bool enabled_per_settings,
                                          const WebURL& image_url) {
  return content_settings_->AllowImage(frame, enabled_per_settings, image_url);
}

bool ChromeRenderViewObserver::allowIndexedDB(WebFrame* frame,
                                              const WebString& name,
                                              const WebSecurityOrigin& origin) {
  return content_settings_->AllowIndexedDB(frame, name, origin);
}

bool ChromeRenderViewObserver::allowPlugins(WebFrame* frame,
                                           bool enabled_per_settings) {
  return content_settings_->AllowPlugins(frame, enabled_per_settings);
}

bool ChromeRenderViewObserver::allowScript(WebFrame* frame,
                                          bool enabled_per_settings) {
  return content_settings_->AllowScript(frame, enabled_per_settings);
}

bool ChromeRenderViewObserver::allowScriptFromSource(
    WebFrame* frame,
    bool enabled_per_settings,
    const WebURL& script_url) {
  return content_settings_->AllowScriptFromSource(frame,
                                                  enabled_per_settings,
                                                  script_url);
}

bool ChromeRenderViewObserver::allowStorage(WebFrame* frame, bool local) {
  return content_settings_->AllowStorage(frame, local);
}

bool ChromeRenderViewObserver::allowReadFromClipboard(WebFrame* frame,
                                                     bool default_value) {
  bool allowed = false;
  // TODO(dcheng): Should we consider a toURL() method on WebSecurityOrigin?
  Send(new ChromeViewHostMsg_CanTriggerClipboardRead(
      routing_id(), GURL(frame->document().securityOrigin().toString().utf8()),
      &allowed));
  return allowed;
}

bool ChromeRenderViewObserver::allowWriteToClipboard(WebFrame* frame,
                                                    bool default_value) {
  bool allowed = false;
  Send(new ChromeViewHostMsg_CanTriggerClipboardWrite(
      routing_id(), GURL(frame->document().securityOrigin().toString().utf8()),
      &allowed));
  return allowed;
}

bool ChromeRenderViewObserver::allowWebComponents(const WebDocument& document,
                                                  bool defaultValue) {
  if (defaultValue)
    return true;

  WebSecurityOrigin origin = document.securityOrigin();
  if (EqualsASCII(origin.protocol(), chrome::kChromeUIScheme))
    return true;

  if (const extensions::Extension* extension = GetExtension(origin)) {
    if (extension->HasAPIPermission(APIPermission::kExperimental))
      return true;
  }

  return false;
}

bool ChromeRenderViewObserver::allowHTMLNotifications(
    const WebDocument& document) {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kDisableHTMLNotifications))
    return false;

  WebSecurityOrigin origin = document.securityOrigin();
  const extensions::Extension* extension = GetExtension(origin);
  return extension && extension->HasAPIPermission(APIPermission::kNotification);
}

bool ChromeRenderViewObserver::allowMutationEvents(const WebDocument& document,
                                                   bool default_value) {
  WebSecurityOrigin origin = document.securityOrigin();
  const extensions::Extension* extension = GetExtension(origin);
  if (extension && extension->is_platform_app())
    return false;
  return default_value;
}

bool ChromeRenderViewObserver::allowPushState(const WebDocument& document) {
  WebSecurityOrigin origin = document.securityOrigin();
  const extensions::Extension* extension = GetExtension(origin);
  return !extension || !extension->is_platform_app();
}

static void SendInsecureContentSignal(int signal) {
  UMA_HISTOGRAM_ENUMERATION("SSL.InsecureContent", signal,
                            INSECURE_CONTENT_NUM_EVENTS);
}

bool ChromeRenderViewObserver::allowDisplayingInsecureContent(
    WebKit::WebFrame* frame,
    bool allowed_per_settings,
    const WebKit::WebSecurityOrigin& origin,
    const WebKit::WebURL& resource_url) {
  SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY);

  std::string origin_host(origin.host().utf8());
  GURL frame_gurl(frame->document().url());
  if (isHostInDomain(origin_host, kGoogleDotCom)) {
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_GOOGLE);
    if (StartsWithASCII(frame_gurl.path(), kGoogleSupportPathPrefix, false)) {
      SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_GOOGLE_SUPPORT);
    } else if (StartsWithASCII(frame_gurl.path(),
                               kGoogleIntlPathPrefix,
                               false)) {
      SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_GOOGLE_INTL);
    }
  }

  if (origin_host == kWWWDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_WWW_GOOGLE);
    if (StartsWithASCII(frame_gurl.path(), kGoogleReaderPathPrefix, false))
      SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_GOOGLE_READER);
  } else if (origin_host == kMailDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_MAIL_GOOGLE);
  } else if (origin_host == kPlusDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_PLUS_GOOGLE);
  } else if (origin_host == kDocsDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_DOCS_GOOGLE);
  } else if (origin_host == kSitesDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_SITES_GOOGLE);
  } else if (origin_host == kPicasawebDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_PICASAWEB_GOOGLE);
  } else if (origin_host == kCodeDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_CODE_GOOGLE);
  } else if (origin_host == kGroupsDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_GROUPS_GOOGLE);
  } else if (origin_host == kMapsDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_MAPS_GOOGLE);
  } else if (origin_host == kWWWDotYoutubeDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HOST_YOUTUBE);
  }

  GURL resource_gurl(resource_url);
  if (EndsWith(resource_gurl.path(), kDotHTML, false))
    SendInsecureContentSignal(INSECURE_CONTENT_DISPLAY_HTML);

  if (allowed_per_settings || allow_displaying_insecure_content_)
    return true;

  if (!IsStrictSecurityHost(origin_host))
    Send(new ChromeViewHostMsg_DidBlockDisplayingInsecureContent(routing_id()));

  return false;
}

bool ChromeRenderViewObserver::allowRunningInsecureContent(
    WebKit::WebFrame* frame,
    bool allowed_per_settings,
    const WebKit::WebSecurityOrigin& origin,
    const WebKit::WebURL& resource_url) {
  std::string origin_host(origin.host().utf8());
  GURL frame_gurl(frame->document().url());
  DCHECK_EQ(frame_gurl.host(), origin_host);

  bool is_google = isHostInDomain(origin_host, kGoogleDotCom);
  if (is_google) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_GOOGLE);
    if (StartsWithASCII(frame_gurl.path(), kGoogleSupportPathPrefix, false)) {
      SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_GOOGLE_SUPPORT);
    } else if (StartsWithASCII(frame_gurl.path(),
                               kGoogleIntlPathPrefix,
                               false)) {
      SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_GOOGLE_INTL);
    }
  }

  if (origin_host == kWWWDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_WWW_GOOGLE);
    if (StartsWithASCII(frame_gurl.path(), kGoogleReaderPathPrefix, false))
      SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_GOOGLE_READER);
  } else if (origin_host == kMailDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_MAIL_GOOGLE);
  } else if (origin_host == kPlusDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_PLUS_GOOGLE);
  } else if (origin_host == kDocsDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_DOCS_GOOGLE);
  } else if (origin_host == kSitesDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_SITES_GOOGLE);
  } else if (origin_host == kPicasawebDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_PICASAWEB_GOOGLE);
  } else if (origin_host == kCodeDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_CODE_GOOGLE);
  } else if (origin_host == kGroupsDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_GROUPS_GOOGLE);
  } else if (origin_host == kMapsDotGoogleDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_MAPS_GOOGLE);
  } else if (origin_host == kWWWDotYoutubeDotCom) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_YOUTUBE);
  } else if (EndsWith(origin_host, kDotGoogleUserContentDotCom, false)) {
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_HOST_GOOGLEUSERCONTENT);
  }

  GURL resource_gurl(resource_url);
  if (resource_gurl.host() == kWWWDotYoutubeDotCom)
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_TARGET_YOUTUBE);

  if (EndsWith(resource_gurl.path(), kDotJS, false))
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_JS);
  else if (EndsWith(resource_gurl.path(), kDotCSS, false))
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_CSS);
  else if (EndsWith(resource_gurl.path(), kDotSWF, false))
    SendInsecureContentSignal(INSECURE_CONTENT_RUN_SWF);

  if (!allow_running_insecure_content_ && !allowed_per_settings) {
    if (!IsStrictSecurityHost(origin_host))
      content_settings_->DidNotAllowMixedScript();
    return false;
  }

  return true;
}

void ChromeRenderViewObserver::didNotAllowPlugins(WebFrame* frame) {
  content_settings_->DidNotAllowPlugins();
}

void ChromeRenderViewObserver::didNotAllowScript(WebFrame* frame) {
  content_settings_->DidNotAllowScript();
}

void ChromeRenderViewObserver::OnSetIsPrerendering(bool is_prerendering) {
  if (is_prerendering) {
    DCHECK(!prerender::PrerenderHelper::Get(render_view()));
    // The PrerenderHelper will destroy itself either after recording histograms
    // or on destruction of the RenderView.
    new prerender::PrerenderHelper(render_view());
  }
}

void ChromeRenderViewObserver::DidStartLoading() {
  if ((render_view()->GetEnabledBindings() & content::BINDINGS_POLICY_WEB_UI) &&
      webui_javascript_.get()) {
    render_view()->EvaluateScript(webui_javascript_->frame_xpath,
                                  webui_javascript_->jscript,
                                  webui_javascript_->id,
                                  webui_javascript_->notify_result);
    webui_javascript_.reset();
  }
}

void ChromeRenderViewObserver::DidStopLoading() {
  WebFrame* main_frame = render_view()->GetWebView()->mainFrame();
  GURL osd_url = main_frame->document().openSearchDescriptionURL();
  if (!osd_url.is_empty()) {
    Send(new ChromeViewHostMsg_PageHasOSDD(
        routing_id(), render_view()->GetPageId(), osd_url,
        search_provider::AUTODETECTED_PROVIDER));
  }

  // Don't capture pages including refresh meta tag.
  if (HasRefreshMetaTag(main_frame))
    return;

  CapturePageInfoLater(
      render_view()->GetPageId(),
      false,  // preliminary_capture
      base::TimeDelta::FromMilliseconds(
          render_view()->GetContentStateImmediately() ?
              0 : kDelayForCaptureMs));
}

void ChromeRenderViewObserver::DidCommitProvisionalLoad(
    WebFrame* frame, bool is_new_navigation) {
  // Don't capture pages being not new, or including refresh meta tag.
  if (!is_new_navigation || HasRefreshMetaTag(frame))
    return;

  CapturePageInfoLater(
      render_view()->GetPageId(),
      true,  // preliminary_capture
      base::TimeDelta::FromMilliseconds(kDelayForForcedCaptureMs));
}

void ChromeRenderViewObserver::DidClearWindowObject(WebFrame* frame) {
  if (render_view()->GetEnabledBindings() &
          content::BINDINGS_POLICY_EXTERNAL_HOST) {
    GetExternalHostBindings()->BindToJavascript(frame, "externalHost");
  }
}

void ChromeRenderViewObserver::DidHandleGestureEvent(
    const WebGestureEvent& event) {
  if (event.type != WebKit::WebGestureEvent::GestureTap)
    return;

  WebKit::WebTextInputType text_input_type =
      render_view()->GetWebView()->textInputInfo().type;

  render_view()->Send(new ChromeViewHostMsg_FocusedNodeTouched(
      routing_id(),
      text_input_type != WebKit::WebTextInputTypeNone));
}

void ChromeRenderViewObserver::CapturePageInfoLater(int page_id,
                                                    bool preliminary_capture,
                                                    base::TimeDelta delay) {
  capture_timer_.Start(
      FROM_HERE,
      delay,
      base::Bind(&ChromeRenderViewObserver::CapturePageInfo,
                 base::Unretained(this),
                 page_id,
                 preliminary_capture));
}

void ChromeRenderViewObserver::CapturePageInfo(int page_id,
                                               bool preliminary_capture) {
  // If |page_id| is obsolete, we should stop indexing and capturing a page.
  if (render_view()->GetPageId() != page_id)
    return;

  if (!render_view()->GetWebView())
    return;

  WebFrame* main_frame = render_view()->GetWebView()->mainFrame();
  if (!main_frame)
    return;

  // Don't index/capture pages that are in view source mode.
  if (main_frame->isViewSourceModeEnabled())
    return;

  // Don't index/capture pages that failed to load.  This only checks the top
  // level frame so the thumbnail may contain a frame that failed to load.
  WebDataSource* ds = main_frame->dataSource();
  if (ds && ds->hasUnreachableURL())
    return;

  // Don't index/capture pages that are being prerendered.
  if (prerender::PrerenderHelper::IsPrerendering(render_view()))
    return;

  // Retrieve the frame's full text (up to kMaxIndexChars), and pass it to the
  // translate helper for language detection and possible translation.
  string16 contents;
  base::TimeTicks capture_begin_time = base::TimeTicks::Now();
  CaptureText(main_frame, &contents);
  UMA_HISTOGRAM_TIMES(kTranslateCaptureText,
                      base::TimeTicks::Now() - capture_begin_time);
  if (translate_helper_)
    translate_helper_->PageCaptured(page_id, contents);

  // Skip indexing if this is not a new load.  Note that the case where
  // page_id == last_indexed_page_id_ is more complicated, since we need to
  // reindex if the toplevel URL has changed (such as from a redirect), even
  // though this may not cause the page id to be incremented.
  if (page_id < last_indexed_page_id_)
    return;

  bool same_page_id = last_indexed_page_id_ == page_id;
  if (!preliminary_capture)
    last_indexed_page_id_ = page_id;

  // Get the URL for this page.
  GURL url(main_frame->document().url());
  if (url.is_empty()) {
    if (!preliminary_capture)
      last_indexed_url_ = GURL();
    return;
  }

  // If the page id is unchanged, check whether the URL (ignoring fragments)
  // has changed.  If so, we need to reindex.  Otherwise, assume this is a
  // reload, in-page navigation, or some other load type where we don't want to
  // reindex.  Note: subframe navigations after onload increment the page id,
  // so these will trigger a reindex.
  GURL stripped_url(StripRef(url));
  if (same_page_id && stripped_url == last_indexed_url_)
    return;

  if (!preliminary_capture)
    last_indexed_url_ = stripped_url;

  TRACE_EVENT0("renderer", "ChromeRenderViewObserver::CapturePageInfo");

#if defined(FULL_SAFE_BROWSING)
  // Will swap out the string.
  if (phishing_classifier_)
    phishing_classifier_->PageCaptured(&contents, preliminary_capture);
#endif
}

void ChromeRenderViewObserver::CaptureText(WebFrame* frame,
                                           string16* contents) {
  contents->clear();
  if (!frame)
    return;

#ifdef TIME_TEXT_RETRIEVAL
  double begin = time_util::GetHighResolutionTimeNow();
#endif

  // get the contents of the frame
  *contents = frame->contentAsText(kMaxIndexChars);

#ifdef TIME_TEXT_RETRIEVAL
  double end = time_util::GetHighResolutionTimeNow();
  char buf[128];
  sprintf_s(buf, "%d chars retrieved for indexing in %gms\n",
            contents.size(), (end - begin)*1000);
  OutputDebugStringA(buf);
#endif

  // When the contents are clipped to the maximum, we don't want to have a
  // partial word indexed at the end that might have been clipped. Therefore,
  // terminate the string at the last space to ensure no words are clipped.
  if (contents->size() == kMaxIndexChars) {
    size_t last_space_index = contents->find_last_of(kWhitespaceUTF16);
    if (last_space_index == std::wstring::npos)
      return;  // don't index if we got a huge block of text with no spaces
    contents->resize(last_space_index);
  }
}

ExternalHostBindings* ChromeRenderViewObserver::GetExternalHostBindings() {
  if (!external_host_bindings_.get()) {
    external_host_bindings_.reset(new ExternalHostBindings(
        render_view(), routing_id()));
  }
  return external_host_bindings_.get();
}

bool ChromeRenderViewObserver::IsStrictSecurityHost(const std::string& host) {
  return (strict_security_hosts_.find(host) != strict_security_hosts_.end());
}

const extensions::Extension* ChromeRenderViewObserver::GetExtension(
    const WebSecurityOrigin& origin) const {
  if (!EqualsASCII(origin.protocol(), extensions::kExtensionScheme))
    return NULL;

  const std::string extension_id = origin.host().utf8().data();
  if (!extension_dispatcher_->IsExtensionActive(extension_id))
    return NULL;

  return extension_dispatcher_->extensions()->GetByID(extension_id);
}

bool ChromeRenderViewObserver::HasRefreshMetaTag(WebFrame* frame) {
  if (!frame)
    return false;
  WebElement head = frame->document().head();
  if (head.isNull() || !head.hasChildNodes())
    return false;

  const WebString tag_name(ASCIIToUTF16("meta"));
  const WebString attribute_name(ASCIIToUTF16("http-equiv"));

  WebNodeList children = head.childNodes();
  for (size_t i = 0; i < children.length(); ++i) {
    WebNode node = children.item(i);
    if (!node.isElementNode())
      continue;
    WebElement element = node.to<WebElement>();
    if (!element.hasTagName(tag_name))
      continue;
    WebString value = element.getAttribute(attribute_name);
    if (value.isNull() || !LowerCaseEqualsASCII(value, "refresh"))
      continue;
    return true;
  }
  return false;
}
