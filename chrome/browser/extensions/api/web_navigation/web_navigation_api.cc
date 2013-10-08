// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implements the Chrome Extensions WebNavigation API.

#include "chrome/browser/extensions/api/web_navigation/web_navigation_api.h"

#include "base/lazy_instance.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/web_navigation/web_navigation_api_constants.h"
#include "chrome/browser/extensions/api/web_navigation/web_navigation_api_helpers.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/retargeting_details.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_iterator.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/extensions/api/web_navigation.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/resource_request_details.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "extensions/browser/view_type_utils.h"
#include "net/base/net_errors.h"

namespace GetFrame = extensions::api::web_navigation::GetFrame;
namespace GetAllFrames = extensions::api::web_navigation::GetAllFrames;

DEFINE_WEB_CONTENTS_USER_DATA_KEY(extensions::WebNavigationTabObserver);

namespace extensions {

#if !defined(OS_ANDROID)

namespace helpers = web_navigation_api_helpers;
namespace keys = web_navigation_api_constants;

namespace {

typedef std::map<content::WebContents*, WebNavigationTabObserver*>
    TabObserverMap;
static base::LazyInstance<TabObserverMap> g_tab_observer =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

// WebNavigtionEventRouter -------------------------------------------

WebNavigationEventRouter::PendingWebContents::PendingWebContents()
    : source_web_contents(NULL),
      source_frame_id(0),
      source_frame_is_main_frame(false),
      target_web_contents(NULL),
      target_url() {
}

WebNavigationEventRouter::PendingWebContents::PendingWebContents(
    content::WebContents* source_web_contents,
    int64 source_frame_id,
    bool source_frame_is_main_frame,
    content::WebContents* target_web_contents,
    const GURL& target_url)
    : source_web_contents(source_web_contents),
      source_frame_id(source_frame_id),
      source_frame_is_main_frame(source_frame_is_main_frame),
      target_web_contents(target_web_contents),
      target_url(target_url) {
}

WebNavigationEventRouter::PendingWebContents::~PendingWebContents() {}

WebNavigationEventRouter::WebNavigationEventRouter(Profile* profile)
    : profile_(profile) {
  CHECK(registrar_.IsEmpty());
  registrar_.Add(this,
                 chrome::NOTIFICATION_RETARGETING,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_TAB_ADDED,
                 content::NotificationService::AllSources());
  registrar_.Add(this,
                 content::NOTIFICATION_WEB_CONTENTS_DESTROYED,
                 content::NotificationService::AllSources());

  BrowserList::AddObserver(this);
  for (chrome::BrowserIterator it; !it.done(); it.Next()) {
    OnBrowserAdded(*it);
  }
}

WebNavigationEventRouter::~WebNavigationEventRouter() {
  BrowserList::RemoveObserver(this);
}

void WebNavigationEventRouter::OnBrowserAdded(Browser* browser) {
  if (!profile_->IsSameProfile(browser->profile()))
    return;
  browser->tab_strip_model()->AddObserver(this);
}

void WebNavigationEventRouter::OnBrowserRemoved(Browser* browser) {
  if (!profile_->IsSameProfile(browser->profile()))
    return;
  browser->tab_strip_model()->RemoveObserver(this);
}

void WebNavigationEventRouter::TabReplacedAt(
    TabStripModel* tab_strip_model,
    content::WebContents* old_contents,
    content::WebContents* new_contents,
    int index) {
  WebNavigationTabObserver* tab_observer =
      WebNavigationTabObserver::Get(old_contents);
  if (!tab_observer) {
    // If you hit this DCHECK(), please add reproduction steps to
    // http://crbug.com/109464.
    DCHECK(GetViewType(old_contents) != VIEW_TYPE_TAB_CONTENTS);
    return;
  }
  const FrameNavigationState& frame_navigation_state =
      tab_observer->frame_navigation_state();

  if (!frame_navigation_state.IsValidUrl(old_contents->GetURL()) ||
      !frame_navigation_state.IsValidUrl(new_contents->GetURL()))
    return;

  helpers::DispatchOnTabReplaced(old_contents, profile_, new_contents);
}

void WebNavigationEventRouter::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_RETARGETING: {
      Profile* profile = content::Source<Profile>(source).ptr();
      if (profile->GetOriginalProfile() == profile_) {
        Retargeting(
            content::Details<const RetargetingDetails>(details).ptr());
      }
      break;
    }

    case chrome::NOTIFICATION_TAB_ADDED:
      TabAdded(content::Details<content::WebContents>(details).ptr());
      break;

    case content::NOTIFICATION_WEB_CONTENTS_DESTROYED:
      TabDestroyed(content::Source<content::WebContents>(source).ptr());
      break;

    default:
      NOTREACHED();
  }
}

void WebNavigationEventRouter::Retargeting(const RetargetingDetails* details) {
  if (details->source_frame_id == 0)
    return;
  WebNavigationTabObserver* tab_observer =
      WebNavigationTabObserver::Get(details->source_web_contents);
  if (!tab_observer) {
    // If you hit this DCHECK(), please add reproduction steps to
    // http://crbug.com/109464.
    DCHECK(GetViewType(details->source_web_contents) != VIEW_TYPE_TAB_CONTENTS);
    return;
  }
  const FrameNavigationState& frame_navigation_state =
      tab_observer->frame_navigation_state();

  FrameNavigationState::FrameID frame_id(
      details->source_frame_id,
      details->source_web_contents->GetRenderViewHost());
  if (!frame_navigation_state.CanSendEvents(frame_id))
    return;

  // If the WebContents isn't yet inserted into a tab strip, we need to delay
  // the extension event until the WebContents is fully initialized.
  if (details->not_yet_in_tabstrip) {
    pending_web_contents_[details->target_web_contents] =
        PendingWebContents(
            details->source_web_contents,
            details->source_frame_id,
            frame_navigation_state.IsMainFrame(frame_id),
            details->target_web_contents,
            details->target_url);
  } else {
    helpers::DispatchOnCreatedNavigationTarget(
        details->source_web_contents,
        details->target_web_contents->GetBrowserContext(),
        details->source_frame_id,
        frame_navigation_state.IsMainFrame(frame_id),
        details->target_web_contents,
        details->target_url);
  }
}

void WebNavigationEventRouter::TabAdded(content::WebContents* tab) {
  std::map<content::WebContents*, PendingWebContents>::iterator iter =
      pending_web_contents_.find(tab);
  if (iter == pending_web_contents_.end())
    return;

  WebNavigationTabObserver* tab_observer =
      WebNavigationTabObserver::Get(iter->second.source_web_contents);
  if (!tab_observer) {
    NOTREACHED();
    return;
  }
  const FrameNavigationState& frame_navigation_state =
      tab_observer->frame_navigation_state();

  FrameNavigationState::FrameID frame_id(
      iter->second.source_frame_id,
      iter->second.source_web_contents->GetRenderViewHost());
  if (frame_navigation_state.CanSendEvents(frame_id)) {
    helpers::DispatchOnCreatedNavigationTarget(
        iter->second.source_web_contents,
        iter->second.target_web_contents->GetBrowserContext(),
        iter->second.source_frame_id,
        iter->second.source_frame_is_main_frame,
        iter->second.target_web_contents,
        iter->second.target_url);
  }
  pending_web_contents_.erase(iter);
}

void WebNavigationEventRouter::TabDestroyed(content::WebContents* tab) {
  pending_web_contents_.erase(tab);
  for (std::map<content::WebContents*, PendingWebContents>::iterator i =
           pending_web_contents_.begin(); i != pending_web_contents_.end(); ) {
    if (i->second.source_web_contents == tab)
      pending_web_contents_.erase(i++);
    else
      ++i;
  }
}

// WebNavigationTabObserver ------------------------------------------

WebNavigationTabObserver::WebNavigationTabObserver(
    content::WebContents* web_contents)
    : WebContentsObserver(web_contents),
      render_view_host_(NULL),
      pending_render_view_host_(NULL) {
  g_tab_observer.Get().insert(TabObserverMap::value_type(web_contents, this));
  registrar_.Add(this,
                 content::NOTIFICATION_RESOURCE_RECEIVED_REDIRECT,
                 content::Source<content::WebContents>(web_contents));
  registrar_.Add(this,
                 content::NOTIFICATION_RENDER_VIEW_HOST_WILL_CLOSE_RENDER_VIEW,
                 content::NotificationService::AllSources());
}

WebNavigationTabObserver::~WebNavigationTabObserver() {}

// static
WebNavigationTabObserver* WebNavigationTabObserver::Get(
    content::WebContents* web_contents) {
  TabObserverMap::iterator i = g_tab_observer.Get().find(web_contents);
  return i == g_tab_observer.Get().end() ? NULL : i->second;
}

content::RenderViewHost* WebNavigationTabObserver::GetRenderViewHostInProcess(
    int process_id) const {
  if (render_view_host_ &&
      render_view_host_->GetProcess()->GetID() == process_id) {
    return render_view_host_;
  }
  if (pending_render_view_host_ &&
      pending_render_view_host_->GetProcess()->GetID() == process_id) {
    return pending_render_view_host_;
  }
  return NULL;
}

void WebNavigationTabObserver::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_RESOURCE_RECEIVED_REDIRECT: {
      content::ResourceRedirectDetails* resource_redirect_details =
          content::Details<content::ResourceRedirectDetails>(details).ptr();
      ResourceType::Type resource_type =
          resource_redirect_details->resource_type;
      if (resource_type == ResourceType::MAIN_FRAME ||
          resource_type == ResourceType::SUB_FRAME) {
        content::RenderViewHost* render_view_host = NULL;
        if (render_view_host_ &&
            resource_redirect_details->origin_child_id ==
                render_view_host_->GetProcess()->GetID() &&
            resource_redirect_details->origin_route_id ==
                render_view_host_->GetRoutingID()) {
          render_view_host = render_view_host_;
        } else if (pending_render_view_host_ &&
                   resource_redirect_details->origin_child_id ==
                       pending_render_view_host_->GetProcess()->GetID() &&
                   resource_redirect_details->origin_route_id ==
                       pending_render_view_host_->GetRoutingID()) {
          render_view_host = pending_render_view_host_;
        }
        if (!render_view_host)
          return;
        FrameNavigationState::FrameID frame_id(
            resource_redirect_details->frame_id, render_view_host);
        navigation_state_.SetIsServerRedirected(frame_id);
      }
      break;
    }

    case content::NOTIFICATION_RENDER_VIEW_HOST_WILL_CLOSE_RENDER_VIEW: {
      // The RenderView is technically not yet deleted, but the RenderViewHost
      // already starts to filter out some IPCs. In order to not get confused,
      // we consider the RenderView dead already now.
      RenderViewDeleted(content::Source<content::RenderViewHost>(source).ptr());
      break;
    }

    default:
      NOTREACHED();
  }
}

void WebNavigationTabObserver::RenderViewDeleted(
    content::RenderViewHost* render_view_host) {
  if (render_view_host == render_view_host_) {
    render_view_host_ = NULL;
    if (pending_render_view_host_) {
      render_view_host_ = pending_render_view_host_;
      pending_render_view_host_ = NULL;
    }
  } else if (render_view_host == pending_render_view_host_) {
    pending_render_view_host_ = NULL;
  } else {
    return;
  }
  SendErrorEvents(
      web_contents(), render_view_host, FrameNavigationState::FrameID());
}

void WebNavigationTabObserver::AboutToNavigateRenderView(
    content::RenderViewHost* render_view_host) {
  if (!render_view_host_) {
    render_view_host_ = render_view_host;
  } else if (render_view_host != render_view_host_) {
    if (pending_render_view_host_) {
      SendErrorEvents(web_contents(),
                      pending_render_view_host_,
                      FrameNavigationState::FrameID());
    }
    pending_render_view_host_ = render_view_host;
  }
}

void WebNavigationTabObserver::DidStartProvisionalLoadForFrame(
    int64 frame_num,
    int64 parent_frame_num,
    bool is_main_frame,
    const GURL& validated_url,
    bool is_error_page,
    bool is_iframe_srcdoc,
    content::RenderViewHost* render_view_host) {
  DVLOG(2) << "DidStartProvisionalLoad("
           << "render_view_host=" << render_view_host
           << ", frame_num=" << frame_num
           << ", url=" << validated_url << ")";
  if (!render_view_host_)
    render_view_host_ = render_view_host;
  if (render_view_host != render_view_host_ &&
      render_view_host != pending_render_view_host_)
    return;

  FrameNavigationState::FrameID frame_id(frame_num, render_view_host);
  FrameNavigationState::FrameID parent_frame_id(
      parent_frame_num, render_view_host);

  navigation_state_.TrackFrame(frame_id,
                               parent_frame_id,
                               validated_url,
                               is_main_frame,
                               is_error_page,
                               is_iframe_srcdoc);

  if (!navigation_state_.CanSendEvents(frame_id))
    return;

  helpers::DispatchOnBeforeNavigate(
      web_contents(),
      render_view_host->GetProcess()->GetID(),
      frame_num,
      is_main_frame,
      parent_frame_num,
      navigation_state_.IsMainFrame(parent_frame_id),
      navigation_state_.GetUrl(frame_id));
}

void WebNavigationTabObserver::DidCommitProvisionalLoadForFrame(
    int64 frame_num,
    bool is_main_frame,
    const GURL& url,
    content::PageTransition transition_type,
    content::RenderViewHost* render_view_host) {
  DVLOG(2) << "DidCommitProvisionalLoad("
           << "render_view_host=" << render_view_host
           << ", frame_num=" << frame_num
           << ", url=" << url << ")";
  if (render_view_host != render_view_host_ &&
      render_view_host != pending_render_view_host_)
    return;
  FrameNavigationState::FrameID frame_id(frame_num, render_view_host);

  bool is_reference_fragment_navigation =
      IsReferenceFragmentNavigation(frame_id, url);
  bool is_history_state_modification =
      navigation_state_.GetNavigationCommitted(frame_id);

  if (is_main_frame && render_view_host_ == render_view_host) {
    // Changing the reference fragment or the history state using
    // history.pushState or history.replaceState does not cancel on-going
    // iframe navigations.
    if (!is_reference_fragment_navigation && !is_history_state_modification)
      SendErrorEvents(web_contents(), render_view_host_, frame_id);
    if (pending_render_view_host_) {
      SendErrorEvents(web_contents(),
                      pending_render_view_host_,
                      FrameNavigationState::FrameID());
      pending_render_view_host_ = NULL;
    }
  } else if (pending_render_view_host_ == render_view_host) {
    SendErrorEvents(
        web_contents(), render_view_host_, FrameNavigationState::FrameID());
    render_view_host_ = pending_render_view_host_;
    pending_render_view_host_ = NULL;
  }

  // Update the URL as it might have changed.
  navigation_state_.UpdateFrame(frame_id, url);
  navigation_state_.SetNavigationCommitted(frame_id);

  if (is_reference_fragment_navigation || is_history_state_modification)
    navigation_state_.SetNavigationCompleted(frame_id);

  if (!navigation_state_.CanSendEvents(frame_id))
    return;

  if (is_reference_fragment_navigation) {
    helpers::DispatchOnCommitted(
        keys::kOnReferenceFragmentUpdated,
        web_contents(),
        frame_num,
        is_main_frame,
        navigation_state_.GetUrl(frame_id),
        transition_type);
  } else if (is_history_state_modification) {
    helpers::DispatchOnCommitted(
        keys::kOnHistoryStateUpdated,
        web_contents(),
        frame_num,
        is_main_frame,
        navigation_state_.GetUrl(frame_id),
        transition_type);
  } else {
    if (navigation_state_.GetIsServerRedirected(frame_id)) {
      transition_type = static_cast<content::PageTransition>(
          transition_type | content::PAGE_TRANSITION_SERVER_REDIRECT);
    }
    helpers::DispatchOnCommitted(
        keys::kOnCommitted,
        web_contents(),
        frame_num,
        is_main_frame,
        navigation_state_.GetUrl(frame_id),
        transition_type);
  }
}

void WebNavigationTabObserver::DidFailProvisionalLoad(
    int64 frame_num,
    bool is_main_frame,
    const GURL& validated_url,
    int error_code,
    const string16& error_description,
    content::RenderViewHost* render_view_host) {
  DVLOG(2) << "DidFailProvisionalLoad("
           << "render_view_host=" << render_view_host
           << ", frame_num=" << frame_num
           << ", url=" << validated_url << ")";
  if (render_view_host != render_view_host_ &&
      render_view_host != pending_render_view_host_)
    return;
  bool stop_tracking_frames = false;
  if (render_view_host == pending_render_view_host_) {
    pending_render_view_host_ = NULL;
    stop_tracking_frames = true;
  }

  FrameNavigationState::FrameID frame_id(frame_num, render_view_host);
  if (navigation_state_.CanSendEvents(frame_id)) {
    helpers::DispatchOnErrorOccurred(
        web_contents(),
        render_view_host->GetProcess()->GetID(),
        navigation_state_.GetUrl(frame_id),
        frame_num,
        is_main_frame,
        error_code);
  }
  navigation_state_.SetErrorOccurredInFrame(frame_id);
  if (stop_tracking_frames) {
    navigation_state_.StopTrackingFramesInRVH(render_view_host,
                                              FrameNavigationState::FrameID());
  }
}

void WebNavigationTabObserver::DocumentLoadedInFrame(
    int64 frame_num,
    content::RenderViewHost* render_view_host) {
  DVLOG(2) << "DocumentLoadedInFrame("
           << "render_view_host=" << render_view_host
           << ", frame_num=" << frame_num << ")";
  if (render_view_host != render_view_host_)
    return;
  FrameNavigationState::FrameID frame_id(frame_num, render_view_host);
  if (!navigation_state_.CanSendEvents(frame_id))
    return;
  navigation_state_.SetParsingFinished(frame_id);
  helpers::DispatchOnDOMContentLoaded(web_contents(),
                                      navigation_state_.GetUrl(frame_id),
                                      navigation_state_.IsMainFrame(frame_id),
                                      frame_num);

  if (!navigation_state_.GetNavigationCompleted(frame_id))
    return;

  // The load might already have finished by the time we finished parsing. For
  // compatibility reasons, we artifically delay the load completed signal until
  // after parsing was completed.
  helpers::DispatchOnCompleted(web_contents(),
                               navigation_state_.GetUrl(frame_id),
                               navigation_state_.IsMainFrame(frame_id),
                               frame_num);
}

void WebNavigationTabObserver::DidFinishLoad(
    int64 frame_num,
    const GURL& validated_url,
    bool is_main_frame,
    content::RenderViewHost* render_view_host) {
  DVLOG(2) << "DidFinishLoad("
           << "render_view_host=" << render_view_host
           << ", frame_num=" << frame_num
           << ", url=" << validated_url << ")";
  if (render_view_host != render_view_host_)
    return;
  FrameNavigationState::FrameID frame_id(frame_num, render_view_host);
  // When showing replacement content, we might get load signals for frames
  // that weren't reguarly loaded.
  if (!navigation_state_.IsValidFrame(frame_id))
    return;
  navigation_state_.SetNavigationCompleted(frame_id);
  if (!navigation_state_.CanSendEvents(frame_id))
    return;
  DCHECK(
      navigation_state_.GetUrl(frame_id) == validated_url ||
      (navigation_state_.GetUrl(frame_id) == GURL(content::kAboutSrcDocURL) &&
       validated_url == GURL(content::kAboutBlankURL)))
      << "validated URL is " << validated_url << " but we expected "
      << navigation_state_.GetUrl(frame_id);
  DCHECK_EQ(navigation_state_.IsMainFrame(frame_id), is_main_frame);

  // The load might already have finished by the time we finished parsing. For
  // compatibility reasons, we artifically delay the load completed signal until
  // after parsing was completed.
  if (!navigation_state_.GetParsingFinished(frame_id))
    return;
  helpers::DispatchOnCompleted(web_contents(),
                               navigation_state_.GetUrl(frame_id),
                               is_main_frame,
                               frame_num);
}

void WebNavigationTabObserver::DidFailLoad(
    int64 frame_num,
    const GURL& validated_url,
    bool is_main_frame,
    int error_code,
    const string16& error_description,
    content::RenderViewHost* render_view_host) {
  DVLOG(2) << "DidFailLoad("
           << "render_view_host=" << render_view_host
           << ", frame_num=" << frame_num
           << ", url=" << validated_url << ")";
  if (render_view_host != render_view_host_)
    return;
  FrameNavigationState::FrameID frame_id(frame_num, render_view_host);
  // When showing replacement content, we might get load signals for frames
  // that weren't reguarly loaded.
  if (!navigation_state_.IsValidFrame(frame_id))
    return;
  if (navigation_state_.CanSendEvents(frame_id)) {
    helpers::DispatchOnErrorOccurred(
        web_contents(),
        render_view_host->GetProcess()->GetID(),
        navigation_state_.GetUrl(frame_id),
        frame_num,
        is_main_frame,
        error_code);
  }
  navigation_state_.SetErrorOccurredInFrame(frame_id);
}

void WebNavigationTabObserver::DidOpenRequestedURL(
    content::WebContents* new_contents,
    const GURL& url,
    const content::Referrer& referrer,
    WindowOpenDisposition disposition,
    content::PageTransition transition,
    int64 source_frame_num) {
  FrameNavigationState::FrameID frame_id(source_frame_num, render_view_host_);
  if (!navigation_state_.CanSendEvents(frame_id))
    return;

  // We only send the onCreatedNavigationTarget if we end up creating a new
  // window.
  if (disposition != SINGLETON_TAB &&
      disposition != NEW_FOREGROUND_TAB &&
      disposition != NEW_BACKGROUND_TAB &&
      disposition != NEW_POPUP &&
      disposition != NEW_WINDOW &&
      disposition != OFF_THE_RECORD)
    return;

  helpers::DispatchOnCreatedNavigationTarget(
      web_contents(),
      new_contents->GetBrowserContext(),
      source_frame_num,
      navigation_state_.IsMainFrame(frame_id),
      new_contents,
      url);
}

void WebNavigationTabObserver::FrameDetached(
    content::RenderViewHost* render_view_host,
    int64 frame_num) {
  if (render_view_host != render_view_host_ &&
      render_view_host != pending_render_view_host_) {
    return;
  }
  FrameNavigationState::FrameID frame_id(frame_num, render_view_host);
  if (navigation_state_.CanSendEvents(frame_id) &&
      !navigation_state_.GetNavigationCompleted(frame_id)) {
    helpers::DispatchOnErrorOccurred(
        web_contents(),
        render_view_host->GetProcess()->GetID(),
        navigation_state_.GetUrl(frame_id),
        frame_num,
        navigation_state_.IsMainFrame(frame_id),
        net::ERR_ABORTED);
  }
  navigation_state_.FrameDetached(frame_id);
}

void WebNavigationTabObserver::WebContentsDestroyed(content::WebContents* tab) {
  g_tab_observer.Get().erase(tab);
  registrar_.RemoveAll();
  SendErrorEvents(tab, NULL, FrameNavigationState::FrameID());
}

void WebNavigationTabObserver::SendErrorEvents(
    content::WebContents* web_contents,
    content::RenderViewHost* render_view_host,
    FrameNavigationState::FrameID id_to_skip) {
  for (FrameNavigationState::const_iterator frame = navigation_state_.begin();
       frame != navigation_state_.end(); ++frame) {
    if (!navigation_state_.GetNavigationCompleted(*frame) &&
        navigation_state_.CanSendEvents(*frame) &&
        *frame != id_to_skip &&
        (!render_view_host || frame->render_view_host == render_view_host)) {
      navigation_state_.SetErrorOccurredInFrame(*frame);
      helpers::DispatchOnErrorOccurred(
          web_contents,
          frame->render_view_host->GetProcess()->GetID(),
          navigation_state_.GetUrl(*frame),
          frame->frame_num,
          navigation_state_.IsMainFrame(*frame),
          net::ERR_ABORTED);
    }
  }
  if (render_view_host)
    navigation_state_.StopTrackingFramesInRVH(render_view_host, id_to_skip);
}

// See also NavigationController::IsURLInPageNavigation.
bool WebNavigationTabObserver::IsReferenceFragmentNavigation(
    FrameNavigationState::FrameID frame_id,
    const GURL& url) {
  GURL existing_url = navigation_state_.GetUrl(frame_id);
  if (existing_url == url)
    return false;

  url_canon::Replacements<char> replacements;
  replacements.ClearRef();
  return existing_url.ReplaceComponents(replacements) ==
      url.ReplaceComponents(replacements);
}

bool WebNavigationGetFrameFunction::RunImpl() {
  scoped_ptr<GetFrame::Params> params(GetFrame::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  int tab_id = params->details.tab_id;
  int frame_id = params->details.frame_id;
  int process_id = params->details.process_id;

  SetResult(Value::CreateNullValue());

  content::WebContents* web_contents;
  if (!ExtensionTabUtil::GetTabById(tab_id,
                                    profile(),
                                    include_incognito(),
                                    NULL, NULL,
                                    &web_contents,
                                    NULL) ||
      !web_contents) {
    return true;
  }

  WebNavigationTabObserver* observer =
      WebNavigationTabObserver::Get(web_contents);
  DCHECK(observer);

  const FrameNavigationState& frame_navigation_state =
      observer->frame_navigation_state();

  if (frame_id == 0)
    frame_id = frame_navigation_state.GetMainFrameID().frame_num;

  content::RenderViewHost* render_view_host =
      observer->GetRenderViewHostInProcess(process_id);
  if (!render_view_host)
    return true;

  FrameNavigationState::FrameID internal_frame_id(frame_id, render_view_host);
  if (!frame_navigation_state.IsValidFrame(internal_frame_id))
    return true;

  GURL frame_url = frame_navigation_state.GetUrl(internal_frame_id);
  if (!frame_navigation_state.IsValidUrl(frame_url))
    return true;

  GetFrame::Results::Details frame_details;
  frame_details.url = frame_url.spec();
  frame_details.error_occurred =
      frame_navigation_state.GetErrorOccurredInFrame(internal_frame_id);
  FrameNavigationState::FrameID parent_frame_id =
      frame_navigation_state.GetParentFrameID(internal_frame_id);
  frame_details.parent_frame_id = helpers::GetFrameId(
      frame_navigation_state.IsMainFrame(parent_frame_id),
      parent_frame_id.frame_num);
  results_ = GetFrame::Results::Create(frame_details);
  return true;
}

bool WebNavigationGetAllFramesFunction::RunImpl() {
  scoped_ptr<GetAllFrames::Params> params(GetAllFrames::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  int tab_id = params->details.tab_id;

  SetResult(Value::CreateNullValue());

  content::WebContents* web_contents;
  if (!ExtensionTabUtil::GetTabById(tab_id,
                                    profile(),
                                    include_incognito(),
                                    NULL, NULL,
                                    &web_contents,
                                    NULL) ||
      !web_contents) {
    return true;
  }

  WebNavigationTabObserver* observer =
      WebNavigationTabObserver::Get(web_contents);
  DCHECK(observer);

  const FrameNavigationState& navigation_state =
      observer->frame_navigation_state();

  std::vector<linked_ptr<GetAllFrames::Results::DetailsType> > result_list;
  for (FrameNavigationState::const_iterator it = navigation_state.begin();
       it != navigation_state.end(); ++it) {
    FrameNavigationState::FrameID frame_id = *it;
    FrameNavigationState::FrameID parent_frame_id =
        navigation_state.GetParentFrameID(frame_id);
    GURL frame_url = navigation_state.GetUrl(frame_id);
    if (!navigation_state.IsValidUrl(frame_url))
      continue;
    linked_ptr<GetAllFrames::Results::DetailsType> frame(
        new GetAllFrames::Results::DetailsType());
    frame->url = frame_url.spec();
    frame->frame_id = helpers::GetFrameId(
        navigation_state.IsMainFrame(frame_id), frame_id.frame_num);
    frame->parent_frame_id = helpers::GetFrameId(
        navigation_state.IsMainFrame(parent_frame_id),
        parent_frame_id.frame_num);
    frame->process_id = frame_id.render_view_host->GetProcess()->GetID();
    frame->error_occurred = navigation_state.GetErrorOccurredInFrame(frame_id);
    result_list.push_back(frame);
  }
  results_ = GetAllFrames::Results::Create(result_list);
  return true;
}

WebNavigationAPI::WebNavigationAPI(Profile* profile)
    : profile_(profile) {
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, keys::kOnBeforeNavigate);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, keys::kOnCommitted);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, keys::kOnCompleted);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, keys::kOnCreatedNavigationTarget);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, keys::kOnDOMContentLoaded);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, keys::kOnHistoryStateUpdated);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, keys::kOnErrorOccurred);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, keys::kOnReferenceFragmentUpdated);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, keys::kOnTabReplaced);
}

WebNavigationAPI::~WebNavigationAPI() {
}

void WebNavigationAPI::Shutdown() {
  ExtensionSystem::Get(profile_)->event_router()->UnregisterObserver(this);
}

static base::LazyInstance<ProfileKeyedAPIFactory<WebNavigationAPI> >
g_factory = LAZY_INSTANCE_INITIALIZER;

// static
ProfileKeyedAPIFactory<WebNavigationAPI>*
WebNavigationAPI::GetFactoryInstance() {
  return &g_factory.Get();
}

void WebNavigationAPI::OnListenerAdded(const EventListenerInfo& details) {
  web_navigation_event_router_.reset(new WebNavigationEventRouter(profile_));
  ExtensionSystem::Get(profile_)->event_router()->UnregisterObserver(this);
}

#endif  // OS_ANDROID

}  // namespace extensions
