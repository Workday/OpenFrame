// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ntp/new_tab_ui.h"

#include <set>

#include "base/i18n/rtl.h"
#include "base/lazy_instance.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/histogram.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/metrics_handler.h"
#include "chrome/browser/ui/webui/ntp/favicon_webui_handler.h"
#include "chrome/browser/ui/webui/ntp/foreign_session_handler.h"
#include "chrome/browser/ui/webui/ntp/most_visited_handler.h"
#include "chrome/browser/ui/webui/ntp/ntp_resource_cache.h"
#include "chrome/browser/ui/webui/ntp/ntp_resource_cache_factory.h"
#include "chrome/browser/ui/webui/ntp/ntp_user_data_logger.h"
#include "chrome/browser/ui/webui/ntp/recently_closed_tabs_handler.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

#if !defined(OS_ANDROID)
#include "chrome/browser/ui/webui/ntp/app_launcher_handler.h"
#include "chrome/browser/ui/webui/ntp/core_app_launcher_handler.h"
#include "chrome/browser/ui/webui/ntp/new_tab_page_handler.h"
#include "chrome/browser/ui/webui/ntp/new_tab_page_sync_handler.h"
#include "chrome/browser/ui/webui/ntp/ntp_login_handler.h"
#include "chrome/browser/ui/webui/ntp/suggestions_page_handler.h"
#else
#include "chrome/browser/ui/webui/ntp/android/bookmarks_handler.h"
#include "chrome/browser/ui/webui/ntp/android/context_menu_handler.h"
#include "chrome/browser/ui/webui/ntp/android/navigation_handler.h"
#include "chrome/browser/ui/webui/ntp/android/new_tab_page_ready_handler.h"
#include "chrome/browser/ui/webui/ntp/android/promo_handler.h"
#endif

#if defined(ENABLE_THEMES)
#include "chrome/browser/ui/webui/theme_handler.h"
#endif

#if defined(USE_ASH)
#include "chrome/browser/ui/host_desktop.h"
#endif

using content::BrowserThread;
using content::RenderViewHost;
using content::WebUIController;

namespace {

// The amount of time there must be no painting for us to consider painting
// finished.  Observed times are in the ~1200ms range on Windows.
const int kTimeoutMs = 2000;

// Strings sent to the page via jstemplates used to set the direction of the
// HTML document based on locale.
const char kRTLHtmlTextDirection[] = "rtl";
const char kLTRHtmlTextDirection[] = "ltr";

static base::LazyInstance<std::set<const WebUIController*> > g_live_new_tabs;

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// NewTabUI

NewTabUI::NewTabUI(content::WebUI* web_ui)
    : WebUIController(web_ui),
      showing_sync_bubble_(false) {
  g_live_new_tabs.Pointer()->insert(this);
  web_ui->OverrideTitle(l10n_util::GetStringUTF16(IDS_NEW_TAB_TITLE));

  content::WebContents* web_contents = web_ui->GetWebContents();
  NTPUserDataLogger::CreateForWebContents(web_contents);
  NTPUserDataLogger::FromWebContents(web_contents)->set_ntp_url(
      GURL(chrome::kChromeUINewTabURL));

  registrar_.Add(
      this,
      content::NOTIFICATION_WEB_CONTENTS_VISIBILITY_CHANGED,
      content::Source<content::WebContents>(web_contents));

  // We count all link clicks as AUTO_BOOKMARK, so that site can be ranked more
  // highly. Note this means we're including clicks on not only most visited
  // thumbnails, but also clicks on recently bookmarked.
  web_ui->SetLinkTransitionType(content::PAGE_TRANSITION_AUTO_BOOKMARK);

  if (!GetProfile()->IsOffTheRecord()) {
    web_ui->AddMessageHandler(new browser_sync::ForeignSessionHandler());
    web_ui->AddMessageHandler(new MostVisitedHandler());
    web_ui->AddMessageHandler(new RecentlyClosedTabsHandler());
#if !defined(OS_ANDROID)
    web_ui->AddMessageHandler(new FaviconWebUIHandler());
    web_ui->AddMessageHandler(new MetricsHandler());
    web_ui->AddMessageHandler(new NewTabPageHandler());
    web_ui->AddMessageHandler(new CoreAppLauncherHandler());
    if (NewTabUI::IsDiscoveryInNTPEnabled())
      web_ui->AddMessageHandler(new SuggestionsHandler());
    // Android doesn't have a sync promo/username on NTP.
    web_ui->AddMessageHandler(new NewTabPageSyncHandler());

    if (ShouldShowApps()) {
      ExtensionService* service = GetProfile()->GetExtensionService();
      // We might not have an ExtensionService (on ChromeOS when not logged in
      // for example).
      if (service)
        web_ui->AddMessageHandler(new AppLauncherHandler(service));
    }
#endif
  }

#if defined(OS_ANDROID)
  // These handlers are specific to the Android NTP page.
  web_ui->AddMessageHandler(new BookmarksHandler());
  web_ui->AddMessageHandler(new ContextMenuHandler());
  web_ui->AddMessageHandler(new FaviconWebUIHandler());
  web_ui->AddMessageHandler(new NavigationHandler());
  web_ui->AddMessageHandler(new NewTabPageReadyHandler());
  if (!GetProfile()->IsOffTheRecord())
    web_ui->AddMessageHandler(new PromoHandler());
#else
  // Android uses native UI for sync setup.
  if (NTPLoginHandler::ShouldShow(GetProfile()))
    web_ui->AddMessageHandler(new NTPLoginHandler());
#endif

#if defined(ENABLE_THEMES)
  // The theme handler can require some CPU, so do it after hooking up the most
  // visited handler. This allows the DB query for the new tab thumbs to happen
  // earlier.
  web_ui->AddMessageHandler(new ThemeHandler());
#endif

  scoped_ptr<NewTabHTMLSource> html_source(new NewTabHTMLSource(
      GetProfile()->GetOriginalProfile()));

  // These two resources should be loaded only if suggestions NTP is enabled.
  html_source->AddResource("suggestions_page.css", "text/css",
      NewTabUI::IsDiscoveryInNTPEnabled() ? IDR_SUGGESTIONS_PAGE_CSS : 0);
  if (NewTabUI::IsDiscoveryInNTPEnabled()) {
    html_source->AddResource("suggestions_page.js", "application/javascript",
        IDR_SUGGESTIONS_PAGE_JS);
  }
  // content::URLDataSource assumes the ownership of the html_source.
  content::URLDataSource::Add(GetProfile(), html_source.release());

  pref_change_registrar_.Init(GetProfile()->GetPrefs());
  pref_change_registrar_.Add(prefs::kShowBookmarkBar,
                             base::Bind(&NewTabUI::OnShowBookmarkBarChanged,
                                        base::Unretained(this)));
}

NewTabUI::~NewTabUI() {
  g_live_new_tabs.Pointer()->erase(this);
}

// The timer callback.  If enough time has elapsed since the last paint
// message, we say we're done painting; otherwise, we keep waiting.
void NewTabUI::PaintTimeout() {
  // The amount of time there must be no painting for us to consider painting
  // finished.  Observed times are in the ~1200ms range on Windows.
  base::TimeTicks now = base::TimeTicks::Now();
  if ((now - last_paint_) >= base::TimeDelta::FromMilliseconds(kTimeoutMs)) {
    // Painting has quieted down.  Log this as the full time to run.
    base::TimeDelta load_time = last_paint_ - start_;
    int load_time_ms = static_cast<int>(load_time.InMilliseconds());
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_INITIAL_NEW_TAB_UI_LOAD,
        content::Source<Profile>(GetProfile()),
        content::Details<int>(&load_time_ms));
    UMA_HISTOGRAM_TIMES("NewTabUI load", load_time);
  } else {
    // Not enough quiet time has elapsed.
    // Some more paints must've occurred since we set the timeout.
    // Wait some more.
    timer_.Start(FROM_HERE, base::TimeDelta::FromMilliseconds(kTimeoutMs), this,
                 &NewTabUI::PaintTimeout);
  }
}

void NewTabUI::StartTimingPaint(RenderViewHost* render_view_host) {
  start_ = base::TimeTicks::Now();
  last_paint_ = start_;

  content::NotificationSource source =
      content::Source<content::RenderWidgetHost>(render_view_host);
  if (!registrar_.IsRegistered(this,
          content::NOTIFICATION_RENDER_WIDGET_HOST_DID_UPDATE_BACKING_STORE,
          source)) {
    registrar_.Add(
        this,
        content::NOTIFICATION_RENDER_WIDGET_HOST_DID_UPDATE_BACKING_STORE,
        source);
  }

  timer_.Start(FROM_HERE, base::TimeDelta::FromMilliseconds(kTimeoutMs), this,
               &NewTabUI::PaintTimeout);
}

void NewTabUI::RenderViewCreated(RenderViewHost* render_view_host) {
  StartTimingPaint(render_view_host);
}

void NewTabUI::RenderViewReused(RenderViewHost* render_view_host) {
  StartTimingPaint(render_view_host);
}

void NewTabUI::Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_RENDER_WIDGET_HOST_DID_UPDATE_BACKING_STORE: {
      last_paint_ = base::TimeTicks::Now();
      break;
    }
    case content::NOTIFICATION_WEB_CONTENTS_VISIBILITY_CHANGED: {
      if (!*content::Details<bool>(details).ptr()) {
        EmitMouseoverCount(
            content::Source<content::WebContents>(source).ptr());
      }
      break;
    }
    default:
      CHECK(false) << "Unexpected notification: " << type;
  }
}

void NewTabUI::EmitMouseoverCount(content::WebContents* web_contents) {
  NTPUserDataLogger* data = NTPUserDataLogger::FromWebContents(web_contents);
  if (data->ntp_url() == GURL(chrome::kChromeUINewTabURL))
    data->EmitMouseoverCount();
}

void NewTabUI::OnShowBookmarkBarChanged() {
  StringValue attached(
      GetProfile()->GetPrefs()->GetBoolean(prefs::kShowBookmarkBar) ?
          "true" : "false");
  web_ui()->CallJavascriptFunction("ntp.setBookmarkBarAttached", attached);
}

// static
void NewTabUI::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
#if !defined(OS_ANDROID)
  CoreAppLauncherHandler::RegisterProfilePrefs(registry);
  NewTabPageHandler::RegisterProfilePrefs(registry);
  if (NewTabUI::IsDiscoveryInNTPEnabled())
    SuggestionsHandler::RegisterProfilePrefs(registry);
#endif
  MostVisitedHandler::RegisterProfilePrefs(registry);
  browser_sync::ForeignSessionHandler::RegisterProfilePrefs(registry);
}

// static
bool NewTabUI::ShouldShowApps() {
// Ash shows apps in app list thus should not show apps page in NTP4.
// Android does not have apps.
#if defined(OS_ANDROID)
  return false;
#elif defined(USE_ASH)
  return chrome::GetActiveDesktop() != chrome::HOST_DESKTOP_TYPE_ASH;
#else
  return true;
#endif
}

// static
bool NewTabUI::IsDiscoveryInNTPEnabled() {
  // TODO(beaudoin): The flag was removed during a clean-up pass. We leave that
  // here to easily enable it back when we will explore this option again.
  return false;
}

// static
void NewTabUI::SetUrlTitleAndDirection(DictionaryValue* dictionary,
                                       const string16& title,
                                       const GURL& gurl) {
  dictionary->SetString("url", gurl.spec());

  bool using_url_as_the_title = false;
  string16 title_to_set(title);
  if (title_to_set.empty()) {
    using_url_as_the_title = true;
    title_to_set = UTF8ToUTF16(gurl.spec());
  }

  // We set the "dir" attribute of the title, so that in RTL locales, a LTR
  // title is rendered left-to-right and truncated from the right. For example,
  // the title of http://msdn.microsoft.com/en-us/default.aspx is "MSDN:
  // Microsoft developer network". In RTL locales, in the [New Tab] page, if
  // the "dir" of this title is not specified, it takes Chrome UI's
  // directionality. So the title will be truncated as "soft developer
  // network". Setting the "dir" attribute as "ltr" renders the truncated title
  // as "MSDN: Microsoft D...". As another example, the title of
  // http://yahoo.com is "Yahoo!". In RTL locales, in the [New Tab] page, the
  // title will be rendered as "!Yahoo" if its "dir" attribute is not set to
  // "ltr".
  std::string direction;
  if (!using_url_as_the_title &&
      base::i18n::IsRTL() &&
      base::i18n::StringContainsStrongRTLChars(title)) {
    direction = kRTLHtmlTextDirection;
  } else {
    direction = kLTRHtmlTextDirection;
  }
  dictionary->SetString("title", title_to_set);
  dictionary->SetString("direction", direction);
}

// static
NewTabUI* NewTabUI::FromWebUIController(WebUIController* ui) {
  if (!g_live_new_tabs.Pointer()->count(ui))
    return NULL;
  return static_cast<NewTabUI*>(ui);
}

Profile* NewTabUI::GetProfile() const {
  return Profile::FromWebUI(web_ui());
}

///////////////////////////////////////////////////////////////////////////////
// NewTabHTMLSource

NewTabUI::NewTabHTMLSource::NewTabHTMLSource(Profile* profile)
    : profile_(profile) {
}

std::string NewTabUI::NewTabHTMLSource::GetSource() const {
  return chrome::kChromeUINewTabHost;
}

void NewTabUI::NewTabHTMLSource::StartDataRequest(
    const std::string& path,
    int render_process_id,
    int render_view_id,
    const content::URLDataSource::GotDataCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  std::map<std::string, std::pair<std::string, int> >::iterator it =
    resource_map_.find(path);
  if (it != resource_map_.end()) {
    scoped_refptr<base::RefCountedStaticMemory> resource_bytes(
        it->second.second ?
            ResourceBundle::GetSharedInstance().LoadDataResourceBytes(
                it->second.second) :
            new base::RefCountedStaticMemory);
    callback.Run(resource_bytes.get());
    return;
  }

  if (!path.empty() && path[0] != '#') {
    // A path under new-tab was requested; it's likely a bad relative
    // URL from the new tab page, but in any case it's an error.

    // TODO(dtrainor): Can remove this #if check once we update the
    // accessibility script to no longer try to access urls like
    // '?2314124523523'.
    // See http://crbug.com/150252.
#if !defined(OS_ANDROID)
    NOTREACHED() << path << " should not have been requested on the NTP";
#endif
    callback.Run(NULL);
    return;
  }

  content::RenderProcessHost* render_host =
      content::RenderProcessHost::FromID(render_process_id);
  bool is_incognito = render_host->GetBrowserContext()->IsOffTheRecord();
  scoped_refptr<base::RefCountedMemory> html_bytes(
      NTPResourceCacheFactory::GetForProfile(profile_)->
      GetNewTabHTML(is_incognito));

  callback.Run(html_bytes.get());
}

std::string NewTabUI::NewTabHTMLSource::GetMimeType(const std::string& resource)
    const {
  std::map<std::string, std::pair<std::string, int> >::const_iterator it =
      resource_map_.find(resource);
  if (it != resource_map_.end())
    return it->second.first;
  return "text/html";
}

bool NewTabUI::NewTabHTMLSource::ShouldReplaceExistingSource() const {
  return false;
}

bool NewTabUI::NewTabHTMLSource::ShouldAddContentSecurityPolicy() const {
  return false;
}

void NewTabUI::NewTabHTMLSource::AddResource(const char* resource,
                                             const char* mime_type,
                                             int resource_id) {
  DCHECK(resource);
  DCHECK(mime_type);
  resource_map_[std::string(resource)] =
      std::make_pair(std::string(mime_type), resource_id);
}

NewTabUI::NewTabHTMLSource::~NewTabHTMLSource() {}
