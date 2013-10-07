// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/tab_helper.h"

#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/activity_log/activity_log.h"
#include "chrome/browser/extensions/api/declarative/rules_registry_service.h"
#include "chrome/browser/extensions/api/declarative_content/content_rules_registry.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/extensions/extension_action.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/image_loader.h"
#include "chrome/browser/extensions/page_action_controller.h"
#include "chrome/browser/extensions/script_badge_controller.h"
#include "chrome/browser/extensions/script_bubble_controller.h"
#include "chrome/browser/extensions/script_executor.h"
#include "chrome/browser/extensions/webstore_inline_installer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_id.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/web_applications/web_app_ui.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/extensions/feature_switch.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "chrome/common/extensions/manifest_handlers/icons_handler.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "extensions/common/extension_resource.h"
#include "ui/gfx/image/image.h"

using content::NavigationController;
using content::NavigationEntry;
using content::RenderViewHost;
using content::WebContents;

DEFINE_WEB_CONTENTS_USER_DATA_KEY(extensions::TabHelper);

namespace {

const char kPermissionError[] = "permission_error";

}  // namespace

namespace extensions {

TabHelper::ScriptExecutionObserver::ScriptExecutionObserver(
    TabHelper* tab_helper)
    : tab_helper_(tab_helper) {
  tab_helper_->AddScriptExecutionObserver(this);
}

TabHelper::ScriptExecutionObserver::ScriptExecutionObserver()
    : tab_helper_(NULL) {
}

TabHelper::ScriptExecutionObserver::~ScriptExecutionObserver() {
  if (tab_helper_)
    tab_helper_->RemoveScriptExecutionObserver(this);
}

TabHelper::TabHelper(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      extension_app_(NULL),
      extension_function_dispatcher_(
          Profile::FromBrowserContext(web_contents->GetBrowserContext()), this),
      pending_web_app_action_(NONE),
      script_executor_(new ScriptExecutor(web_contents,
                                          &script_execution_observers_)),
      image_loader_ptr_factory_(this) {
  // The ActiveTabPermissionManager requires a session ID; ensure this
  // WebContents has one.
  SessionTabHelper::CreateForWebContents(web_contents);
  if (web_contents->GetRenderViewHost())
    SetTabId(web_contents->GetRenderViewHost());
  active_tab_permission_granter_.reset(new ActiveTabPermissionGranter(
      web_contents,
      SessionID::IdForTab(web_contents),
      Profile::FromBrowserContext(web_contents->GetBrowserContext())));
  if (FeatureSwitch::script_badges()->IsEnabled()) {
    location_bar_controller_.reset(
        new ScriptBadgeController(web_contents, this));
  } else {
    location_bar_controller_.reset(
        new PageActionController(web_contents));
  }

  if (FeatureSwitch::script_bubble()->IsEnabled()) {
    script_bubble_controller_.reset(
        new ScriptBubbleController(web_contents, this));
  }


  // If more classes need to listen to global content script activity, then
  // a separate routing class with an observer interface should be written.
  profile_ = Profile::FromBrowserContext(web_contents->GetBrowserContext());
  AddScriptExecutionObserver(ActivityLog::GetInstance(profile_));

  registrar_.Add(this,
                 content::NOTIFICATION_LOAD_STOP,
                 content::Source<NavigationController>(
                     &web_contents->GetController()));

  registrar_.Add(this,
                 chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::NotificationService::AllSources());
}

TabHelper::~TabHelper() {
  RemoveScriptExecutionObserver(ActivityLog::GetInstance(profile_));
}

void TabHelper::CreateApplicationShortcuts() {
  DCHECK(CanCreateApplicationShortcuts());
  NavigationEntry* entry =
      web_contents()->GetController().GetLastCommittedEntry();
  if (!entry)
    return;

  pending_web_app_action_ = CREATE_SHORTCUT;

  // Start fetching web app info for CreateApplicationShortcut dialog and show
  // the dialog when the data is available in OnDidGetApplicationInfo.
  GetApplicationInfo(entry->GetPageID());
}

bool TabHelper::CanCreateApplicationShortcuts() const {
#if defined(OS_MACOSX)
  return false;
#else
  return web_app::IsValidUrl(web_contents()->GetURL()) &&
      pending_web_app_action_ == NONE;
#endif
}

void TabHelper::SetExtensionApp(const Extension* extension) {
  DCHECK(!extension || AppLaunchInfo::GetFullLaunchURL(extension).is_valid());
  extension_app_ = extension;

  UpdateExtensionAppIcon(extension_app_);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_TAB_CONTENTS_APPLICATION_EXTENSION_CHANGED,
      content::Source<TabHelper>(this),
      content::NotificationService::NoDetails());
}

void TabHelper::SetExtensionAppById(const std::string& extension_app_id) {
  const Extension* extension = GetExtension(extension_app_id);
  if (extension)
    SetExtensionApp(extension);
}

void TabHelper::SetExtensionAppIconById(const std::string& extension_app_id) {
  const Extension* extension = GetExtension(extension_app_id);
  if (extension)
    UpdateExtensionAppIcon(extension);
}

SkBitmap* TabHelper::GetExtensionAppIcon() {
  if (extension_app_icon_.empty())
    return NULL;

  return &extension_app_icon_;
}

void TabHelper::RenderViewCreated(RenderViewHost* render_view_host) {
  SetTabId(render_view_host);
}

void TabHelper::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
#if defined(ENABLE_EXTENSIONS)
  if (ExtensionSystem::Get(profile_)->extension_service() &&
      RulesRegistryService::Get(profile_)) {
    RulesRegistryService::Get(profile_)->content_rules_registry()->
        DidNavigateMainFrame(web_contents(), details, params);
  }
#endif  // defined(ENABLE_EXTENSIONS)

  if (details.is_in_page)
    return;

  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  ExtensionService* service = profile->GetExtensionService();
  if (!service)
    return;

  ExtensionActionManager* extension_action_manager =
      ExtensionActionManager::Get(profile);
  for (ExtensionSet::const_iterator it = service->extensions()->begin();
       it != service->extensions()->end(); ++it) {
    ExtensionAction* browser_action =
        extension_action_manager->GetBrowserAction(*it->get());
    if (browser_action) {
      browser_action->ClearAllValuesForTab(SessionID::IdForTab(web_contents()));
      content::NotificationService::current()->Notify(
          chrome::NOTIFICATION_EXTENSION_BROWSER_ACTION_UPDATED,
          content::Source<ExtensionAction>(browser_action),
          content::NotificationService::NoDetails());
    }
  }
}

bool TabHelper::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(TabHelper, message)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_DidGetApplicationInfo,
                        OnDidGetApplicationInfo)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_InlineWebstoreInstall,
                        OnInlineWebstoreInstall)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_GetAppInstallState,
                        OnGetAppInstallState);
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_Request, OnRequest)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_ContentScriptsExecuting,
                        OnContentScriptsExecuting)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_OnWatchedPageChange,
                        OnWatchedPageChange)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void TabHelper::DidCloneToNewWebContents(WebContents* old_web_contents,
                                         WebContents* new_web_contents) {
  // When the WebContents that this is attached to is cloned, give the new clone
  // a TabHelper and copy state over.
  CreateForWebContents(new_web_contents);
  TabHelper* new_helper = FromWebContents(new_web_contents);

  new_helper->SetExtensionApp(extension_app());
  new_helper->extension_app_icon_ = extension_app_icon_;
}


void TabHelper::OnDidGetApplicationInfo(int32 page_id,
                                        const WebApplicationInfo& info) {
  // Android does not implement BrowserWindow.
#if !defined(OS_MACOSX) && !defined(OS_ANDROID)
  web_app_info_ = info;

  NavigationEntry* entry =
      web_contents()->GetController().GetLastCommittedEntry();
  if (!entry || (entry->GetPageID() != page_id))
    return;

  switch (pending_web_app_action_) {
    case CREATE_SHORTCUT: {
      chrome::ShowCreateWebAppShortcutsDialog(
          web_contents()->GetView()->GetTopLevelNativeWindow(),
          web_contents());
      break;
    }
    case UPDATE_SHORTCUT: {
      web_app::UpdateShortcutForTabContents(web_contents());
      break;
    }
    default:
      NOTREACHED();
      break;
  }

  pending_web_app_action_ = NONE;
#endif
}

void TabHelper::OnInlineWebstoreInstall(
    int install_id,
    int return_route_id,
    const std::string& webstore_item_id,
    const GURL& requestor_url) {
  WebstoreStandaloneInstaller::Callback callback =
      base::Bind(&TabHelper::OnInlineInstallComplete, base::Unretained(this),
                 install_id, return_route_id);
  scoped_refptr<WebstoreInlineInstaller> installer(
      new WebstoreInlineInstaller(
          web_contents(),
          webstore_item_id,
          requestor_url,
          callback));
  installer->BeginInstall();
}

void TabHelper::OnGetAppInstallState(const GURL& requestor_url,
                                     int return_route_id,
                                     int callback_id) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  ExtensionService* extension_service = profile->GetExtensionService();
  const ExtensionSet* extensions = extension_service->extensions();
  const ExtensionSet* disabled = extension_service->disabled_extensions();

  std::string state;
  if (extensions->GetHostedAppByURL(requestor_url))
    state = extension_misc::kAppStateInstalled;
  else if (disabled->GetHostedAppByURL(requestor_url))
    state = extension_misc::kAppStateDisabled;
  else
    state = extension_misc::kAppStateNotInstalled;

  Send(new ExtensionMsg_GetAppInstallStateResponse(
      return_route_id, state, callback_id));
}

void TabHelper::OnRequest(const ExtensionHostMsg_Request_Params& request) {
  extension_function_dispatcher_.Dispatch(request,
                                          web_contents()->GetRenderViewHost());
}

void TabHelper::OnContentScriptsExecuting(
    const ScriptExecutionObserver::ExecutingScriptsMap& executing_scripts_map,
    int32 on_page_id,
    const GURL& on_url) {
  FOR_EACH_OBSERVER(ScriptExecutionObserver, script_execution_observers_,
                    OnScriptsExecuted(web_contents(),
                                      executing_scripts_map,
                                      on_page_id,
                                      on_url));
}

void TabHelper::OnWatchedPageChange(
    const std::vector<std::string>& css_selectors) {
#if defined(ENABLE_EXTENSIONS)
  if (ExtensionSystem::Get(profile_)->extension_service() &&
      RulesRegistryService::Get(profile_)) {
    RulesRegistryService::Get(profile_)->content_rules_registry()->Apply(
        web_contents(), css_selectors);
  }
#endif  // defined(ENABLE_EXTENSIONS)
}

const Extension* TabHelper::GetExtension(const std::string& extension_app_id) {
  if (extension_app_id.empty())
    return NULL;

  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  ExtensionService* extension_service = profile->GetExtensionService();
  if (!extension_service || !extension_service->is_ready())
    return NULL;

  const Extension* extension =
      extension_service->GetExtensionById(extension_app_id, false);
  return extension;
}

void TabHelper::UpdateExtensionAppIcon(const Extension* extension) {
  extension_app_icon_.reset();
  // Ensure previously enqueued callbacks are ignored.
  image_loader_ptr_factory_.InvalidateWeakPtrs();

  // Enqueue OnImageLoaded callback.
  if (extension) {
    Profile* profile =
        Profile::FromBrowserContext(web_contents()->GetBrowserContext());
    extensions::ImageLoader* loader = extensions::ImageLoader::Get(profile);
    loader->LoadImageAsync(
        extension,
        IconsInfo::GetIconResource(extension,
                                   extension_misc::EXTENSION_ICON_SMALLISH,
                                   ExtensionIconSet::MATCH_EXACTLY),
        gfx::Size(extension_misc::EXTENSION_ICON_SMALLISH,
                  extension_misc::EXTENSION_ICON_SMALLISH),
        base::Bind(&TabHelper::OnImageLoaded,
                   image_loader_ptr_factory_.GetWeakPtr()));
  }
}

void TabHelper::SetAppIcon(const SkBitmap& app_icon) {
  extension_app_icon_ = app_icon;
  web_contents()->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TITLE);
}

void TabHelper::OnImageLoaded(const gfx::Image& image) {
  if (!image.IsEmpty()) {
    extension_app_icon_ = *image.ToSkBitmap();
    web_contents()->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TAB);
  }
}

WindowController* TabHelper::GetExtensionWindowController() const  {
  return ExtensionTabUtil::GetWindowControllerOfTab(web_contents());
}

void TabHelper::OnInlineInstallComplete(int install_id,
                                        int return_route_id,
                                        bool success,
                                        const std::string& error) {
  Send(new ExtensionMsg_InlineWebstoreInstallResponse(
      return_route_id, install_id, success, success ? std::string() : error));
}

WebContents* TabHelper::GetAssociatedWebContents() const {
  return web_contents();
}

void TabHelper::GetApplicationInfo(int32 page_id) {
  Send(new ExtensionMsg_GetApplicationInfo(routing_id(), page_id));
}

void TabHelper::Observe(int type,
                        const content::NotificationSource& source,
                        const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_LOAD_STOP: {
      const NavigationController& controller =
          *content::Source<NavigationController>(source).ptr();
      DCHECK_EQ(controller.GetWebContents(), web_contents());

      if (pending_web_app_action_ == UPDATE_SHORTCUT) {
        // Schedule a shortcut update when web application info is available if
        // last committed entry is not NULL. Last committed entry could be NULL
        // when an interstitial page is injected (e.g. bad https certificate,
        // malware site etc). When this happens, we abort the shortcut update.
        NavigationEntry* entry = controller.GetLastCommittedEntry();
        if (entry)
          GetApplicationInfo(entry->GetPageID());
        else
          pending_web_app_action_ = NONE;
      }
      break;
    }

    case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
      if (script_bubble_controller_) {
        script_bubble_controller_->OnExtensionUnloaded(
            content::Details<extensions::UnloadedExtensionInfo>(
                details)->extension->id());
        break;
      }
    }
  }
}

void TabHelper::SetTabId(RenderViewHost* render_view_host) {
  render_view_host->Send(
      new ExtensionMsg_SetTabId(render_view_host->GetRoutingID(),
                                SessionID::IdForTab(web_contents())));
}

}  // namespace extensions
