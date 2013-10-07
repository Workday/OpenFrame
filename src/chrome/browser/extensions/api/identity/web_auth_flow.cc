// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/identity/web_auth_flow.h"

#include "apps/shell_window.h"
#include "base/base64.h"
#include "base/location.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/extension_constants.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/resource_request_details.h"
#include "content/public/browser/web_contents.h"
#include "crypto/random.h"
#include "grit/browser_resources.h"
#include "url/gurl.h"

using apps::ShellWindow;
using content::RenderViewHost;
using content::ResourceRedirectDetails;
using content::WebContents;
using content::WebContentsObserver;

namespace extensions {

WebAuthFlow::WebAuthFlow(
    Delegate* delegate,
    Profile* profile,
    const GURL& provider_url,
    Mode mode)
    : delegate_(delegate),
      profile_(profile),
      provider_url_(provider_url),
      mode_(mode),
      embedded_window_created_(false) {
}

WebAuthFlow::~WebAuthFlow() {
  DCHECK(delegate_ == NULL);

  // Stop listening to notifications first since some of the code
  // below may generate notifications.
  registrar_.RemoveAll();
  WebContentsObserver::Observe(NULL);

  if (!shell_window_key_.empty()) {
    apps::ShellWindowRegistry::Get(profile_)->RemoveObserver(this);

    if (shell_window_ && shell_window_->web_contents())
      shell_window_->web_contents()->Close();
  }
}

void WebAuthFlow::Start() {
  apps::ShellWindowRegistry::Get(profile_)->AddObserver(this);

  // Attach a random ID string to the window so we can recoginize it
  // in OnShellWindowAdded.
  std::string random_bytes;
  crypto::RandBytes(WriteInto(&random_bytes, 33), 32);
  bool success = base::Base64Encode(random_bytes, &shell_window_key_);
  DCHECK(success);

  // identityPrivate.onWebFlowRequest(shell_window_key, provider_url_, mode_)
  scoped_ptr<base::ListValue> args(new base::ListValue());
  args->AppendString(shell_window_key_);
  args->AppendString(provider_url_.spec());
  if (mode_ == WebAuthFlow::INTERACTIVE)
    args->AppendString("interactive");
  else
    args->AppendString("silent");

  scoped_ptr<Event> event(
      new Event("identityPrivate.onWebFlowRequest", args.Pass()));
  event->restrict_to_profile = profile_;
  ExtensionSystem* system = ExtensionSystem::Get(profile_);

  extensions::ComponentLoader* component_loader =
      system->extension_service()->component_loader();
  if (!component_loader->Exists(extension_misc::kIdentityApiUiAppId)) {
    component_loader->Add(
        IDR_IDENTITY_API_SCOPE_APPROVAL_MANIFEST,
        base::FilePath(FILE_PATH_LITERAL("identity_scope_approval_dialog")));
  }

  system->event_router()->DispatchEventWithLazyListener(
      extension_misc::kIdentityApiUiAppId, event.Pass());
}

void WebAuthFlow::DetachDelegateAndDelete() {
  delegate_ = NULL;
  base::MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}

void WebAuthFlow::OnShellWindowAdded(ShellWindow* shell_window) {
  if (shell_window->window_key() == shell_window_key_ &&
      shell_window->extension()->id() == extension_misc::kIdentityApiUiAppId) {
    shell_window_ = shell_window;
    WebContentsObserver::Observe(shell_window->web_contents());

    registrar_.Add(
        this,
        content::NOTIFICATION_WEB_CONTENTS_RENDER_VIEW_HOST_CREATED,
        content::NotificationService::AllBrowserContextsAndSources());
  }
}

void WebAuthFlow::OnShellWindowIconChanged(ShellWindow* shell_window) {}

void WebAuthFlow::OnShellWindowRemoved(ShellWindow* shell_window) {
  if (shell_window->window_key() == shell_window_key_ &&
      shell_window->extension()->id() == extension_misc::kIdentityApiUiAppId) {
    shell_window_ = NULL;
    registrar_.RemoveAll();

    if (delegate_)
      delegate_->OnAuthFlowFailure(WebAuthFlow::WINDOW_CLOSED);
  }
}

void WebAuthFlow::BeforeUrlLoaded(const GURL& url) {
  if (delegate_ && embedded_window_created_)
    delegate_->OnAuthFlowURLChange(url);
}

void WebAuthFlow::AfterUrlLoaded() {
  if (delegate_ && embedded_window_created_ && mode_ == WebAuthFlow::SILENT)
    delegate_->OnAuthFlowFailure(WebAuthFlow::INTERACTION_REQUIRED);
}

void WebAuthFlow::Observe(int type,
                          const content::NotificationSource& source,
                          const content::NotificationDetails& details) {
  DCHECK(shell_window_);

  if (!delegate_)
    return;

  if (!embedded_window_created_) {
    DCHECK(type == content::NOTIFICATION_WEB_CONTENTS_RENDER_VIEW_HOST_CREATED);

    RenderViewHost* render_view(
        content::Details<RenderViewHost>(details).ptr());
    WebContents* web_contents = WebContents::FromRenderViewHost(render_view);

    if (web_contents &&
        (web_contents->GetEmbedderWebContents() ==
         WebContentsObserver::web_contents())) {
      // Switch from watching the shell window to the guest inside it.
      embedded_window_created_ = true;
      WebContentsObserver::Observe(web_contents);

      registrar_.RemoveAll();
      registrar_.Add(this,
                     content::NOTIFICATION_RESOURCE_RECEIVED_REDIRECT,
                     content::Source<WebContents>(web_contents));
      registrar_.Add(this,
                     content::NOTIFICATION_WEB_CONTENTS_TITLE_UPDATED,
                     content::Source<WebContents>(web_contents));
    }
  } else {
    // embedded_window_created_
    switch (type) {
      case content::NOTIFICATION_RESOURCE_RECEIVED_REDIRECT: {
        ResourceRedirectDetails* redirect_details =
            content::Details<ResourceRedirectDetails>(details).ptr();
        if (redirect_details != NULL)
          BeforeUrlLoaded(redirect_details->new_url);
        break;
      }
      case content::NOTIFICATION_WEB_CONTENTS_TITLE_UPDATED: {
        std::pair<content::NavigationEntry*, bool>* title =
            content::Details<std::pair<content::NavigationEntry*, bool> >(
                details).ptr();

        if (title->first) {
          delegate_->OnAuthFlowTitleChange(
              UTF16ToUTF8(title->first->GetTitle()));
        }
        break;
      }
      default:
        NOTREACHED()
            << "Got a notification that we did not register for: " << type;
        break;
    }
  }
}

void WebAuthFlow::RenderProcessGone(base::TerminationStatus status) {
  if (delegate_)
    delegate_->OnAuthFlowFailure(WebAuthFlow::WINDOW_CLOSED);
}

void WebAuthFlow::DidStartProvisionalLoadForFrame(
    int64 frame_id,
    int64 parent_frame_id,
    bool is_main_frame,
    const GURL& validated_url,
    bool is_error_page,
    bool is_iframe_srcdoc,
    RenderViewHost* render_view_host) {
  if (is_main_frame)
    BeforeUrlLoaded(validated_url);
}

void WebAuthFlow::DidFailProvisionalLoad(int64 frame_id,
                                         bool is_main_frame,
                                         const GURL& validated_url,
                                         int error_code,
                                         const string16& error_description,
                                         RenderViewHost* render_view_host) {
  if (delegate_)
    delegate_->OnAuthFlowFailure(LOAD_FAILED);
}

void WebAuthFlow::DidStopLoading(RenderViewHost* render_view_host) {
  AfterUrlLoaded();
}

void WebAuthFlow::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
  if (delegate_ && details.http_status_code >= 400)
    delegate_->OnAuthFlowFailure(LOAD_FAILED);
}

}  // namespace extensions
