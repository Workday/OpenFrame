// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_host/chrome_render_view_host_observer.h"

#include <vector>

#include "base/command_line.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/net/predictor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/search_terms_data.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/extensions/manifest.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_transition_types.h"
#include "extensions/common/constants.h"
#include "net/http/http_request_headers.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/codec/jpeg_codec.h"

#if defined(OS_WIN)
#include "base/win/win_util.h"
#endif  // OS_WIN

using content::ChildProcessSecurityPolicy;
using content::OpenURLParams;
using content::RenderViewHost;
using content::SiteInstance;
using content::WebContents;
using extensions::Extension;
using extensions::Manifest;

ChromeRenderViewHostObserver::ChromeRenderViewHostObserver(
    RenderViewHost* render_view_host, chrome_browser_net::Predictor* predictor)
    : content::RenderViewHostObserver(render_view_host),
      predictor_(predictor) {
  SiteInstance* site_instance = render_view_host->GetSiteInstance();
  profile_ = Profile::FromBrowserContext(
      site_instance->GetBrowserContext());

  InitRenderViewForExtensions();
}

ChromeRenderViewHostObserver::~ChromeRenderViewHostObserver() {
  if (render_view_host())
    RemoveRenderViewHostForExtensions(render_view_host());
}

void ChromeRenderViewHostObserver::RenderViewHostInitialized() {
  // This reinitializes some state in the case where a render process crashes
  // but we keep the same RenderViewHost instance.
  InitRenderViewForExtensions();
}

void ChromeRenderViewHostObserver::RenderViewHostDestroyed(
    RenderViewHost* rvh) {
  RemoveRenderViewHostForExtensions(rvh);
  delete this;
}

void ChromeRenderViewHostObserver::Navigate(const GURL& url) {
  if (!predictor_)
    return;
  if (!CommandLine::ForCurrentProcess()->HasSwitch(switches::kChromeFrame) &&
     (url.SchemeIs(chrome::kHttpScheme) || url.SchemeIs(chrome::kHttpsScheme)))
    predictor_->PreconnectUrlAndSubresources(url, GURL());
}

bool ChromeRenderViewHostObserver::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ChromeRenderViewHostObserver, message)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_FocusedNodeTouched,
                        OnFocusedNodeTouched)
    IPC_MESSAGE_HANDLER(ChromeViewHostMsg_RequestThumbnailForContextNode_ACK,
                        OnRequestThumbnailForContextNodeACK)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void ChromeRenderViewHostObserver::InitRenderViewForExtensions() {
  const Extension* extension = GetExtension();
  if (!extension)
    return;

  content::RenderProcessHost* process = render_view_host()->GetProcess();

  // Some extensions use chrome:// URLs.
  // This is a temporary solution. Replace it with access to chrome-static://
  // once it is implemented. See: crbug.com/226927.
  Manifest::Type type = extension->GetType();
  if (type == Manifest::TYPE_EXTENSION ||
      type == Manifest::TYPE_LEGACY_PACKAGED_APP ||
      (type == Manifest::TYPE_PLATFORM_APP &&
       extension->location() == Manifest::COMPONENT)) {
    ChildProcessSecurityPolicy::GetInstance()->GrantScheme(
        process->GetID(), chrome::kChromeUIScheme);
  }

  // Some extensions use file:// URLs.
  if (type == Manifest::TYPE_EXTENSION ||
      type == Manifest::TYPE_LEGACY_PACKAGED_APP) {
    if (extensions::ExtensionSystem::Get(profile_)->extension_service()->
            extension_prefs()->AllowFileAccess(extension->id())) {
      ChildProcessSecurityPolicy::GetInstance()->GrantScheme(
          process->GetID(), chrome::kFileScheme);
    }
  }

  switch (type) {
    case Manifest::TYPE_EXTENSION:
    case Manifest::TYPE_USER_SCRIPT:
    case Manifest::TYPE_HOSTED_APP:
    case Manifest::TYPE_LEGACY_PACKAGED_APP:
    case Manifest::TYPE_PLATFORM_APP:
      // Always send a Loaded message before ActivateExtension so that
      // ExtensionDispatcher knows what Extension is active, not just its ID.
      // This is important for classifying the Extension's JavaScript context
      // correctly (see ExtensionDispatcher::ClassifyJavaScriptContext).
      Send(new ExtensionMsg_Loaded(
          std::vector<ExtensionMsg_Loaded_Params>(
              1, ExtensionMsg_Loaded_Params(extension))));
      Send(new ExtensionMsg_ActivateExtension(extension->id()));
      break;

    case Manifest::TYPE_UNKNOWN:
    case Manifest::TYPE_THEME:
    case Manifest::TYPE_SHARED_MODULE:
      break;
  }
}

const Extension* ChromeRenderViewHostObserver::GetExtension() {
  // Note that due to ChromeContentBrowserClient::GetEffectiveURL(), hosted apps
  // (excluding bookmark apps) will have a chrome-extension:// URL for their
  // site, so we can ignore that wrinkle here.
  SiteInstance* site_instance = render_view_host()->GetSiteInstance();
  const GURL& site = site_instance->GetSiteURL();

  if (!site.SchemeIs(extensions::kExtensionScheme))
    return NULL;

  ExtensionService* service =
      extensions::ExtensionSystem::Get(profile_)->extension_service();
  if (!service)
    return NULL;

  // Reload the extension if it has crashed.
  // TODO(yoz): This reload doesn't happen synchronously for unpacked
  //            extensions. It seems to be fast enough, but there is a race.
  //            We should delay loading until the extension has reloaded.
  if (service->GetTerminatedExtension(site.host()))
    service->ReloadExtension(site.host());

  // May be null if the extension doesn't exist, for example if somebody typos
  // a chrome-extension:// URL.
  return service->extensions()->GetByID(site.host());
}

void ChromeRenderViewHostObserver::RemoveRenderViewHostForExtensions(
    RenderViewHost* rvh) {
  ExtensionProcessManager* process_manager =
      extensions::ExtensionSystem::Get(profile_)->process_manager();
  if (process_manager)
    process_manager->UnregisterRenderViewHost(rvh);
}

void ChromeRenderViewHostObserver::OnFocusedNodeTouched(bool editable) {
  if (editable) {
#if defined(OS_WIN) && defined(USE_AURA)
    base::win::DisplayVirtualKeyboard();
#endif
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_FOCUSED_NODE_TOUCHED,
        content::Source<RenderViewHost>(render_view_host()),
        content::Details<bool>(&editable));
  } else {
#if defined(OS_WIN) && defined(USE_AURA)
    base::win::DismissVirtualKeyboard();
#endif
  }
}

// Handles the image thumbnail for the context node, composes a image search
// request based on the received thumbnail and opens the request in a new tab.
void ChromeRenderViewHostObserver::OnRequestThumbnailForContextNodeACK(
    const SkBitmap& bitmap) {
  WebContents* web_contents =
      WebContents::FromRenderViewHost(render_view_host());
  const TemplateURL* const default_provider =
      TemplateURLServiceFactory::GetForProfile(profile_)->
          GetDefaultSearchProvider();
  if (!web_contents || !default_provider)
    return;

  const int kDefaultQualityForImageSearch = 90;
  std::vector<unsigned char> data;
  if (!gfx::JPEGCodec::Encode(
      reinterpret_cast<unsigned char*>(bitmap.getAddr32(0, 0)),
      gfx::JPEGCodec::FORMAT_SkBitmap, bitmap.width(), bitmap.height(),
      static_cast<int>(bitmap.rowBytes()), kDefaultQualityForImageSearch,
      &data))
    return;

  TemplateURLRef::SearchTermsArgs search_args =
      TemplateURLRef::SearchTermsArgs(base::string16());
  search_args.image_thumbnail_content = std::string(data.begin(), data.end());
  // TODO(jnd): Add a method in WebContentsViewDelegate to get the image URL
  // from the ContextMenuParams which creates current context menu.
  search_args.image_url = GURL();
  TemplateURLRef::PostContent post_content;
  GURL result(default_provider->image_url_ref().ReplaceSearchTerms(
      search_args, &post_content));
  if (!result.is_valid())
    return;

  OpenURLParams open_url_params(result, content::Referrer(), NEW_FOREGROUND_TAB,
                                content::PAGE_TRANSITION_LINK, false);
  const std::string& content_type = post_content.first;
  std::string* post_data = &post_content.second;
  if (!post_data->empty()) {
    DCHECK(!content_type.empty());
    open_url_params.uses_post = true;
    open_url_params.browser_initiated_post_data =
        base::RefCountedString::TakeString(post_data);
    open_url_params.extra_headers += base::StringPrintf(
        "%s: %s\r\n", net::HttpRequestHeaders::kContentType,
        content_type.c_str());
  }
  web_contents->OpenURL(open_url_params);
}
