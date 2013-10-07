// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/content_settings_observer.h"

#include "base/command_line.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "content/public/renderer/document_state.h"
#include "content/public/renderer/navigation_state.h"
#include "content/public/renderer/render_view.h"
#include "extensions/common/constants.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/web/WebDataSource.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebFrameClient.h"
#include "third_party/WebKit/public/web/WebSecurityOrigin.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "webkit/child/weburlresponse_extradata_impl.h"

using WebKit::WebDataSource;
using WebKit::WebFrame;
using WebKit::WebFrameClient;
using WebKit::WebSecurityOrigin;
using WebKit::WebString;
using WebKit::WebURL;
using WebKit::WebView;
using content::DocumentState;
using content::NavigationState;

namespace {

GURL GetOriginOrURL(const WebFrame* frame) {
  WebString top_origin = frame->top()->document().securityOrigin().toString();
  // The the |top_origin| is unique ("null") e.g., for file:// URLs. Use the
  // document URL as the primary URL in those cases.
  if (top_origin == "null")
    return frame->top()->document().url();
  return GURL(top_origin);
}

ContentSetting GetContentSettingFromRules(
    const ContentSettingsForOneType& rules,
    const WebFrame* frame,
    const GURL& secondary_url) {
  ContentSettingsForOneType::const_iterator it;
  // If there is only one rule, it's the default rule and we don't need to match
  // the patterns.
  if (rules.size() == 1) {
    DCHECK(rules[0].primary_pattern == ContentSettingsPattern::Wildcard());
    DCHECK(rules[0].secondary_pattern == ContentSettingsPattern::Wildcard());
    return rules[0].setting;
  }
  const GURL& primary_url = GetOriginOrURL(frame);
  for (it = rules.begin(); it != rules.end(); ++it) {
    if (it->primary_pattern.Matches(primary_url) &&
        it->secondary_pattern.Matches(secondary_url)) {
      return it->setting;
    }
  }
  NOTREACHED();
  return CONTENT_SETTING_DEFAULT;
}

}  // namespace

ContentSettingsObserver::ContentSettingsObserver(
    content::RenderView* render_view)
    : content::RenderViewObserver(render_view),
      content::RenderViewObserverTracker<ContentSettingsObserver>(render_view),
      content_setting_rules_(NULL),
      is_interstitial_page_(false),
      npapi_plugins_blocked_(false) {
  ClearBlockedContentSettings();
}

ContentSettingsObserver::~ContentSettingsObserver() {
}

void ContentSettingsObserver::SetContentSettingRules(
    const RendererContentSettingRules* content_setting_rules) {
  content_setting_rules_ = content_setting_rules;
}

bool ContentSettingsObserver::IsPluginTemporarilyAllowed(
    const std::string& identifier) {
  // If the empty string is in here, it means all plug-ins are allowed.
  // TODO(bauerb): Remove this once we only pass in explicit identifiers.
  return (temporarily_allowed_plugins_.find(identifier) !=
          temporarily_allowed_plugins_.end()) ||
         (temporarily_allowed_plugins_.find(std::string()) !=
          temporarily_allowed_plugins_.end());
}

void ContentSettingsObserver::DidBlockContentType(
    ContentSettingsType settings_type,
    const std::string& resource_identifier) {
  // Always send a message when |resource_identifier| is not empty, to tell the
  // browser which resource was blocked (otherwise the browser will only show
  // the first resource to be blocked, and none that are blocked at a later
  // time).
  if (!content_blocked_[settings_type] || !resource_identifier.empty()) {
    content_blocked_[settings_type] = true;
    Send(new ChromeViewHostMsg_ContentBlocked(routing_id(), settings_type,
                                              resource_identifier));
  }
}

bool ContentSettingsObserver::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ContentSettingsObserver, message)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_SetAsInterstitial, OnSetAsInterstitial)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  if (handled)
    return true;

  // Don't swallow LoadBlockedPlugins messages, as they're sent to every
  // blocked plugin.
  IPC_BEGIN_MESSAGE_MAP(ContentSettingsObserver, message)
    IPC_MESSAGE_HANDLER(ChromeViewMsg_LoadBlockedPlugins, OnLoadBlockedPlugins)
  IPC_END_MESSAGE_MAP()

  return false;
}

void ContentSettingsObserver::DidCommitProvisionalLoad(
    WebFrame* frame, bool is_new_navigation) {
  if (frame->parent())
    return;  // Not a top-level navigation.

  DocumentState* document_state = DocumentState::FromDataSource(
      frame->dataSource());
  NavigationState* navigation_state = document_state->navigation_state();
  if (!navigation_state->was_within_same_page()) {
    // Clear "block" flags for the new page. This needs to happen before any of
    // |AllowScript()|, |AllowScriptFromSource()|, |AllowImage()|, or
    // |AllowPlugins()| is called for the new page so that these functions can
    // correctly detect that a piece of content flipped from "not blocked" to
    // "blocked".
    ClearBlockedContentSettings();
    temporarily_allowed_plugins_.clear();
  }

  GURL url = frame->document().url();
  // If we start failing this DCHECK, please makes sure we don't regress
  // this bug: http://code.google.com/p/chromium/issues/detail?id=79304
  DCHECK(frame->document().securityOrigin().toString() == "null" ||
         !url.SchemeIs(chrome::kDataScheme));
}

bool ContentSettingsObserver::AllowDatabase(WebFrame* frame,
                                            const WebString& name,
                                            const WebString& display_name,
                                            unsigned long estimated_size) {
  if (frame->document().securityOrigin().isUnique() ||
      frame->top()->document().securityOrigin().isUnique())
    return false;

  bool result = false;
  Send(new ChromeViewHostMsg_AllowDatabase(
      routing_id(), GURL(frame->document().securityOrigin().toString()),
      GURL(frame->top()->document().securityOrigin().toString()),
      name, display_name, &result));
  return result;
}

bool ContentSettingsObserver::AllowFileSystem(WebFrame* frame) {
  if (frame->document().securityOrigin().isUnique() ||
      frame->top()->document().securityOrigin().isUnique())
    return false;

  bool result = false;
  Send(new ChromeViewHostMsg_AllowFileSystem(
      routing_id(), GURL(frame->document().securityOrigin().toString()),
      GURL(frame->top()->document().securityOrigin().toString()), &result));
  return result;
}

bool ContentSettingsObserver::AllowImage(WebFrame* frame,
                                         bool enabled_per_settings,
                                         const WebURL& image_url) {
  bool allow = enabled_per_settings;
  if (enabled_per_settings) {
    if (is_interstitial_page_)
      return true;
    if (IsWhitelistedForContentSettings(frame))
      return true;

    if (content_setting_rules_) {
      GURL secondary_url(image_url);
      allow = GetContentSettingFromRules(
          content_setting_rules_->image_rules,
          frame, secondary_url) != CONTENT_SETTING_BLOCK;
    }
  }
  if (!allow)
    DidBlockContentType(CONTENT_SETTINGS_TYPE_IMAGES, std::string());
  return allow;
}

bool ContentSettingsObserver::AllowIndexedDB(WebFrame* frame,
                                             const WebString& name,
                                             const WebSecurityOrigin& origin) {
  if (frame->document().securityOrigin().isUnique() ||
      frame->top()->document().securityOrigin().isUnique())
    return false;

  bool result = false;
  Send(new ChromeViewHostMsg_AllowIndexedDB(
      routing_id(), GURL(frame->document().securityOrigin().toString()),
      GURL(frame->top()->document().securityOrigin().toString()),
      name, &result));
  return result;
}

bool ContentSettingsObserver::AllowPlugins(WebFrame* frame,
                                           bool enabled_per_settings) {
  return enabled_per_settings;
}

bool ContentSettingsObserver::AllowScript(WebFrame* frame,
                                          bool enabled_per_settings) {
  if (!enabled_per_settings)
    return false;
  if (is_interstitial_page_)
    return true;

  std::map<WebFrame*, bool>::const_iterator it =
      cached_script_permissions_.find(frame);
  if (it != cached_script_permissions_.end())
    return it->second;

  // Evaluate the content setting rules before
  // |IsWhitelistedForContentSettings|; if there is only the default rule
  // allowing all scripts, it's quicker this way.
  bool allow = true;
  if (content_setting_rules_) {
    ContentSetting setting = GetContentSettingFromRules(
        content_setting_rules_->script_rules,
        frame,
        GURL(frame->document().securityOrigin().toString()));
    allow = setting != CONTENT_SETTING_BLOCK;
  }
  allow = allow || IsWhitelistedForContentSettings(frame);

  cached_script_permissions_[frame] = allow;
  return allow;
}

bool ContentSettingsObserver::AllowScriptFromSource(
    WebFrame* frame,
    bool enabled_per_settings,
    const WebKit::WebURL& script_url) {
  if (!enabled_per_settings)
    return false;
  if (is_interstitial_page_)
    return true;

  bool allow = true;
  if (content_setting_rules_) {
    ContentSetting setting = GetContentSettingFromRules(
        content_setting_rules_->script_rules,
        frame,
        GURL(script_url));
    allow = setting != CONTENT_SETTING_BLOCK;
  }
  return allow || IsWhitelistedForContentSettings(frame);
}

bool ContentSettingsObserver::AllowStorage(WebFrame* frame, bool local) {
  if (frame->document().securityOrigin().isUnique() ||
      frame->top()->document().securityOrigin().isUnique())
    return false;
  bool result = false;

  StoragePermissionsKey key(
      GURL(frame->document().securityOrigin().toString()), local);
  std::map<StoragePermissionsKey, bool>::const_iterator permissions =
      cached_storage_permissions_.find(key);
  if (permissions != cached_storage_permissions_.end())
    return permissions->second;

  Send(new ChromeViewHostMsg_AllowDOMStorage(
      routing_id(), GURL(frame->document().securityOrigin().toString()),
      GURL(frame->top()->document().securityOrigin().toString()),
      local, &result));
  cached_storage_permissions_[key] = result;
  return result;
}

void ContentSettingsObserver::DidNotAllowPlugins() {
  DidBlockContentType(CONTENT_SETTINGS_TYPE_PLUGINS, std::string());
}

void ContentSettingsObserver::DidNotAllowScript() {
  DidBlockContentType(CONTENT_SETTINGS_TYPE_JAVASCRIPT, std::string());
}

void ContentSettingsObserver::DidNotAllowMixedScript() {
  DidBlockContentType(CONTENT_SETTINGS_TYPE_MIXEDSCRIPT, std::string());
}

void ContentSettingsObserver::BlockNPAPIPlugins() {
  npapi_plugins_blocked_ = true;
}

bool ContentSettingsObserver::AreNPAPIPluginsBlocked() const {
  return npapi_plugins_blocked_;
}

void ContentSettingsObserver::OnLoadBlockedPlugins(
    const std::string& identifier) {
  temporarily_allowed_plugins_.insert(identifier);
}

void ContentSettingsObserver::OnSetAsInterstitial() {
  is_interstitial_page_ = true;
}

void ContentSettingsObserver::ClearBlockedContentSettings() {
  for (size_t i = 0; i < arraysize(content_blocked_); ++i)
    content_blocked_[i] = false;
  cached_storage_permissions_.clear();
  cached_script_permissions_.clear();
}

bool ContentSettingsObserver::IsWhitelistedForContentSettings(WebFrame* frame) {
  // Whitelist Instant processes.
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kInstantProcess))
    return true;

  // Whitelist ftp directory listings, as they require JavaScript to function
  // properly.
  webkit_glue::WebURLResponseExtraDataImpl* extra_data =
      static_cast<webkit_glue::WebURLResponseExtraDataImpl*>(
          frame->dataSource()->response().extraData());
  if (extra_data && extra_data->is_ftp_directory_listing())
    return true;
  return IsWhitelistedForContentSettings(frame->document().securityOrigin(),
                                         frame->document().url());
}

bool ContentSettingsObserver::IsWhitelistedForContentSettings(
    const WebSecurityOrigin& origin,
    const GURL& document_url) {
  if (document_url == GURL(content::kUnreachableWebDataURL))
    return true;

  if (origin.isUnique())
    return false;  // Uninitialized document?

  if (EqualsASCII(origin.protocol(), chrome::kChromeUIScheme))
    return true;  // Browser UI elements should still work.

  if (EqualsASCII(origin.protocol(), chrome::kChromeDevToolsScheme))
    return true;  // DevTools UI elements should still work.

  if (EqualsASCII(origin.protocol(), extensions::kExtensionScheme))
    return true;

  if (EqualsASCII(origin.protocol(), chrome::kChromeInternalScheme))
    return true;

  // If the scheme is file:, an empty file name indicates a directory listing,
  // which requires JavaScript to function properly.
  if (EqualsASCII(origin.protocol(), chrome::kFileScheme)) {
    return document_url.SchemeIs(chrome::kFileScheme) &&
           document_url.ExtractFileName().empty();
  }

  return false;
}
