// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/media_router/media_router_ui.h"

#include <string>

#include "base/guid.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/media/router/create_presentation_connection_request.h"
#include "chrome/browser/media/router/issue.h"
#include "chrome/browser/media/router/issues_observer.h"
#include "chrome/browser/media/router/media_route.h"
#include "chrome/browser/media/router/media_router.h"
#include "chrome/browser/media/router/media_router_factory.h"
#include "chrome/browser/media/router/media_router_mojo_impl.h"
#include "chrome/browser/media/router/media_routes_observer.h"
#include "chrome/browser/media/router/media_sink.h"
#include "chrome/browser/media/router/media_sinks_observer.h"
#include "chrome/browser/media/router/media_source.h"
#include "chrome/browser/media/router/media_source_helper.h"
#include "chrome/browser/media/router/presentation_service_delegate_impl.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/ui/webui/media_router/media_router_localized_strings_provider.h"
#include "chrome/browser/ui/webui/media_router/media_router_resources_provider.h"
#include "chrome/browser/ui/webui/media_router/media_router_webui_message_handler.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/constants.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/web_dialogs/web_dialog_delegate.h"

namespace media_router {

namespace {

// The amount of time to wait for a response when creating a new route.
const int kCreateRouteTimeoutSeconds = 20;

std::string GetHostFromURL(const GURL& gurl) {
  if (gurl.is_empty()) return std::string();
  std::string host = gurl.host();
  if (base::StartsWith(host, "www.", base::CompareCase::INSENSITIVE_ASCII))
    host = host.substr(4);
  return host;
}

std::string TruncateHost(const std::string& host) {
  const std::string truncated =
      net::registry_controlled_domains::GetDomainAndRegistry(
          host, net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);
  // The truncation will be empty in some scenarios (e.g. host is
  // simply an IP address). Fail gracefully.
  return truncated.empty() ? host : truncated;
}

}  // namespace

// static
std::string MediaRouterUI::GetExtensionName(
    const GURL& gurl, extensions::ExtensionRegistry* registry) {
  if (gurl.is_empty() || !registry) return std::string();

  const extensions::Extension* extension =
      registry->enabled_extensions().GetExtensionOrAppByURL(gurl);

  return extension ? extension->name() : std::string();
}

// This class calls to refresh the UI when the highest priority issue is
// updated.
class MediaRouterUI::UIIssuesObserver : public IssuesObserver {
 public:
  UIIssuesObserver(MediaRouter* router, MediaRouterUI* ui)
      : IssuesObserver(router), ui_(ui) {
    DCHECK(ui);
  }

  ~UIIssuesObserver() override {}

  // IssuesObserver implementation.
  void OnIssueUpdated(const Issue* issue) override { ui_->SetIssue(issue); }

 private:
  // Reference back to the owning MediaRouterUI instance.
  MediaRouterUI* ui_;

  DISALLOW_COPY_AND_ASSIGN(UIIssuesObserver);
};

MediaRouterUI::UIMediaRoutesObserver::UIMediaRoutesObserver(
    MediaRouter* router, const RoutesUpdatedCallback& callback)
    : MediaRoutesObserver(router), callback_(callback) {
  DCHECK(!callback_.is_null());
}

MediaRouterUI::UIMediaRoutesObserver::~UIMediaRoutesObserver() {}

void MediaRouterUI::UIMediaRoutesObserver::OnRoutesUpdated(
    const std::vector<MediaRoute>& routes) {
  std::vector<MediaRoute> routes_for_display;
  for (const MediaRoute& route : routes) {
    if (route.for_display()) {
#ifndef NDEBUG
      for (const MediaRoute& existing_route : routes_for_display) {
        if (existing_route.media_sink_id() == route.media_sink_id()) {
          DVLOG(2) << "Received another route for display with the same sink"
                   << " id as an existing route. " << route.media_route_id()
                   << " has the same sink id as "
                   << existing_route.media_sink_id() << ".";
        }
      }
#endif
      routes_for_display.push_back(route);
    }
  }

  callback_.Run(routes_for_display);
}

MediaRouterUI::MediaRouterUI(content::WebUI* web_ui)
    : ConstrainedWebDialogUI(web_ui),
      handler_(new MediaRouterWebUIMessageHandler(this)),
      ui_initialized_(false),
      requesting_route_for_default_source_(false),
      current_route_request_id_(-1),
      route_request_counter_(0),
      initiator_(nullptr),
      router_(nullptr),
      weak_factory_(this) {
  // Create a WebUIDataSource containing the chrome://media-router page's
  // content.
  scoped_ptr<content::WebUIDataSource> html_source(
      content::WebUIDataSource::Create(chrome::kChromeUIMediaRouterHost));

  content::WebContents* wc = web_ui->GetWebContents();
  DCHECK(wc);

  router_ = static_cast<MediaRouterMojoImpl*>(
      MediaRouterFactory::GetApiForBrowserContext(wc->GetBrowserContext()));

  // Allows UI to load extensionview.
  // TODO(haibinlu): limit object-src to current extension once crbug/514866
  // is fixed.
  html_source->OverrideContentSecurityPolicyObjectSrc("object-src *;");

  AddLocalizedStrings(html_source.get());
  AddMediaRouterUIResources(html_source.get());
  // Ownership of |html_source| is transferred to the BrowserContext.
  content::WebUIDataSource::Add(Profile::FromWebUI(web_ui),
                                html_source.release());

  // Ownership of |handler_| is transferred to |web_ui|.
  web_ui->AddMessageHandler(handler_);
}

MediaRouterUI::~MediaRouterUI() {
  if (issues_observer_) issues_observer_->UnregisterObserver();

  if (query_result_manager_.get()) query_result_manager_->RemoveObserver(this);
  if (presentation_service_delegate_.get())
    presentation_service_delegate_->RemoveDefaultPresentationRequestObserver(
        this);
  // If |create_session_request_| still exists, then it means presentation route
  // request was never attempted.
  if (create_session_request_) {
    create_session_request_->InvokeErrorCallback(content::PresentationError(
        content::PRESENTATION_ERROR_SESSION_REQUEST_CANCELLED,
        "Dialog closed."));
  }
}

void MediaRouterUI::InitWithDefaultMediaSource(
    const base::WeakPtr<PresentationServiceDelegateImpl>& delegate) {
  DCHECK(delegate);
  DCHECK(!presentation_service_delegate_);
  DCHECK(!query_result_manager_.get());

  presentation_service_delegate_ = delegate;
  presentation_service_delegate_->AddDefaultPresentationRequestObserver(this);
  InitCommon(presentation_service_delegate_->web_contents());
  if (presentation_service_delegate_->HasDefaultPresentationRequest()) {
    OnDefaultPresentationChanged(
        presentation_service_delegate_->GetDefaultPresentationRequest());
  }
}

void MediaRouterUI::InitWithPresentationSessionRequest(
    content::WebContents* initiator,
    const base::WeakPtr<PresentationServiceDelegateImpl>& delegate,
    scoped_ptr<CreatePresentationConnectionRequest> create_session_request) {
  DCHECK(initiator);
  DCHECK(create_session_request);
  DCHECK(!create_session_request_);
  DCHECK(!query_result_manager_);

  create_session_request_ = create_session_request.Pass();
  presentation_service_delegate_ = delegate;
  InitCommon(initiator);
  OnDefaultPresentationChanged(create_session_request_->presentation_request());
}

void MediaRouterUI::InitCommon(content::WebContents* initiator) {
  DCHECK(initiator);
  DCHECK(router_);

  TRACE_EVENT_NESTABLE_ASYNC_INSTANT1("media_router", "UI", initiator,
                                      "MediaRouterUI::InitCommon", this);
  // Register for MediaRoute updates.
  routes_observer_.reset(new UIMediaRoutesObserver(
      router_,
      base::Bind(&MediaRouterUI::OnRoutesUpdated, base::Unretained(this))));

  query_result_manager_.reset(new QueryResultManager(router_));
  query_result_manager_->AddObserver(this);

  // These modes are always available.
  query_result_manager_->StartSinksQuery(MediaCastMode::DESKTOP_MIRROR,
                                         MediaSourceForDesktop());
  initiator_ = initiator;
  MediaSource mirroring_source(
      MediaSourceForTab(SessionTabHelper::IdForTab(initiator)));
  query_result_manager_->StartSinksQuery(MediaCastMode::TAB_MIRROR,
                                         mirroring_source);
  UpdateCastModes();
}

void MediaRouterUI::OnDefaultPresentationChanged(
    const PresentationRequest& presentation_request) {
  presentation_request_.reset(new PresentationRequest(presentation_request));
  query_result_manager_->StartSinksQuery(
      MediaCastMode::DEFAULT, presentation_request_->GetMediaSource());
  UpdateCastModes();
}

void MediaRouterUI::OnDefaultPresentationRemoved() {
  presentation_request_.reset();
  query_result_manager_->StopSinksQuery(MediaCastMode::DEFAULT);
  UpdateCastModes();
}

void MediaRouterUI::UpdateCastModes() {
  // Gets updated cast modes from |query_result_manager_| and forwards it to UI.
  query_result_manager_->GetSupportedCastModes(&cast_modes_);
  if (ui_initialized_) {
    handler_->UpdateCastModes(cast_modes_, GetPresentationRequestSourceName());
  }
}

void MediaRouterUI::Close() {
  ConstrainedWebDialogDelegate* delegate = GetConstrainedDelegate();
  if (delegate) {
    delegate->GetWebDialogDelegate()->OnDialogClosed(std::string());
    delegate->OnDialogCloseFromWebUI();
  }
}

void MediaRouterUI::UIInitialized() {
  TRACE_EVENT_NESTABLE_ASYNC_END0("media_router", "UI", initiator_);
  ui_initialized_ = true;

  // Register for Issue updates.
  if (!issues_observer_)
    issues_observer_.reset(new UIIssuesObserver(router_, this));
  issues_observer_->RegisterObserver();
}

bool MediaRouterUI::CreateRoute(const MediaSink::Id& sink_id,
                                MediaCastMode cast_mode) {
  DCHECK(query_result_manager_.get());
  DCHECK(initiator_);

  // Note that there is a rarely-encountered bug, where the MediaCastMode to
  // MediaSource mapping could have been updated, between when the user clicked
  // on the UI to start a create route request, and when this function is
  // called. However, since the user does not have visibility into the
  // MediaSource, and that it occurs very rarely in practice, we leave it as-is
  // for now.
  MediaSource source = query_result_manager_->GetSourceForCastMode(cast_mode);
  if (source.Empty()) {
    LOG(ERROR) << "No corresponding MediaSource for cast mode " << cast_mode;
    return false;
  }

  requesting_route_for_default_source_ = cast_mode == MediaCastMode::DEFAULT;
  if (requesting_route_for_default_source_ && !presentation_request_) {
    DLOG(ERROR) << "Requested to create a route for presentation, but "
                << "presentation request is missing.";
    return false;
  }

  current_route_request_id_ = ++route_request_counter_;
  GURL origin;
  if (requesting_route_for_default_source_) {
    origin = presentation_request_->frame_url().GetOrigin();
  } else {
    // Requesting route for mirroring. Use a placeholder URL as origin.
    origin = GURL(chrome::kChromeUIMediaRouterURL);
  }
  DCHECK(origin.is_valid());

  DVLOG(1) << "DoCreateRoute: origin: " << origin;

  // There are 3 cases. In all cases the MediaRouterUI will need to be notified.
  // (1) Non-presentation route request (e.g., mirroring). No additional
  // notification necessary.
  // (2) Presentation route request for a Presentation API startSession call.
  // The startSession (CreatePresentationConnectionRequest) will need to be
  // answered with the
  // route response.
  // (3) Browser-initiated presentation route request. If successful,
  // PresentationServiceDelegateImpl will have to be notified. Note that we
  // treat subsequent route requests from a Presentation API-initiated dialogs
  // as browser-initiated.
  std::vector<MediaRouteResponseCallback> route_response_callbacks;
  route_response_callbacks.push_back(base::Bind(
      &MediaRouterUI::OnRouteResponseReceived, weak_factory_.GetWeakPtr(),
      current_route_request_id_, sink_id));
  if (requesting_route_for_default_source_) {
    if (create_session_request_) {
      // |create_session_request_| will be nullptr after this call, as the
      // object will be transferred to the callback.
      route_response_callbacks.push_back(
          base::Bind(&CreatePresentationConnectionRequest::HandleRouteResponse,
                     base::Passed(&create_session_request_)));
    } else if (presentation_service_delegate_) {
      route_response_callbacks.push_back(
          base::Bind(&PresentationServiceDelegateImpl::OnRouteResponse,
                     presentation_service_delegate_, *presentation_request_));
    }
  }

  // Start the timer.
  route_creation_timer_.Start(
      FROM_HERE, base::TimeDelta::FromSeconds(kCreateRouteTimeoutSeconds), this,
      &MediaRouterUI::RouteCreationTimeout);

  router_->CreateRoute(source.id(), sink_id, origin, initiator_,
                       route_response_callbacks);
  return true;
}

void MediaRouterUI::CloseRoute(const MediaRoute::Id& route_id) {
  router_->CloseRoute(route_id);
}

void MediaRouterUI::AddIssue(const Issue& issue) { router_->AddIssue(issue); }

void MediaRouterUI::ClearIssue(const std::string& issue_id) {
  router_->ClearIssue(issue_id);
}

void MediaRouterUI::OnResultsUpdated(
    const std::vector<MediaSinkWithCastModes>& sinks) {
  sinks_ = sinks;
  if (ui_initialized_) handler_->UpdateSinks(sinks_);
}

void MediaRouterUI::SetIssue(const Issue* issue) {
  if (ui_initialized_) handler_->UpdateIssue(issue);
}

void MediaRouterUI::OnRoutesUpdated(const std::vector<MediaRoute>& routes) {
  routes_ = routes;
  if (ui_initialized_) handler_->UpdateRoutes(routes_);
}

void MediaRouterUI::OnRouteResponseReceived(const int route_request_id,
                                            const MediaSink::Id& sink_id,
                                            const MediaRoute* route,
                                            const std::string& presentation_id,
                                            const std::string& error) {
  DVLOG(1) << "OnRouteResponseReceived";
  // If we receive a new route that we aren't expecting, do nothing.
  if (route_request_id != current_route_request_id_) return;

  if (!route) {
    // The provider will handle sending an issue for a failed route request.
    DVLOG(0) << "MediaRouteResponse returned error: " << error;
  }

  handler_->OnCreateRouteResponseReceived(sink_id, route);
  requesting_route_for_default_source_ = false;
  current_route_request_id_ = -1;
  route_creation_timer_.Stop();
}

void MediaRouterUI::RouteCreationTimeout() {
  requesting_route_for_default_source_ = false;
  current_route_request_id_ = -1;

  base::string16 host =
      base::UTF8ToUTF16(GetTruncatedPresentationRequestSourceName());

  // TODO(apacible): Update error messages based on current cast mode
  // (e.g. desktop).
  std::string issue_title =
      host.empty() ? l10n_util::GetStringUTF8(
                         IDS_MEDIA_ROUTER_ISSUE_CREATE_ROUTE_TIMEOUT_FOR_TAB)
                   : l10n_util::GetStringFUTF8(
                         IDS_MEDIA_ROUTER_ISSUE_CREATE_ROUTE_TIMEOUT, host);

  Issue issue(issue_title, std::string(),
              IssueAction(IssueAction::TYPE_DISMISS),
              std::vector<IssueAction>(), std::string(), Issue::NOTIFICATION,
              false, std::string());
  AddIssue(issue);
  handler_->NotifyRouteCreationTimeout();
}

GURL MediaRouterUI::GetFrameURL() const {
  return presentation_request_ ? presentation_request_->frame_url() : GURL();
}

std::string MediaRouterUI::GetPresentationRequestSourceName() const {
  GURL gurl = GetFrameURL();
  return gurl.SchemeIs(extensions::kExtensionScheme)
             ? GetExtensionName(gurl, extensions::ExtensionRegistry::Get(
                                          Profile::FromWebUI(web_ui())))
             : GetHostFromURL(gurl);
}

std::string MediaRouterUI::GetTruncatedPresentationRequestSourceName() const {
  GURL gurl = GetFrameURL();
  return gurl.SchemeIs(extensions::kExtensionScheme)
             ? GetExtensionName(gurl, extensions::ExtensionRegistry::Get(
                                          Profile::FromWebUI(web_ui())))
             : TruncateHost(GetHostFromURL(gurl));
}

const std::string& MediaRouterUI::GetRouteProviderExtensionId() const {
  return router_->media_route_provider_extension_id();
}

}  // namespace media_router
