// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/page_action_controller.h"

#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/browser_event_router.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/extension_action.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_id.h"
#include "chrome/common/extensions/extension_set.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_contents.h"

namespace extensions {

PageActionController::PageActionController(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {}

PageActionController::~PageActionController() {}

std::vector<ExtensionAction*> PageActionController::GetCurrentActions() const {
  ExtensionService* service = GetExtensionService();
  if (!service)
    return std::vector<ExtensionAction*>();

  // Accumulate the list of all page actions to display.
  std::vector<ExtensionAction*> current_actions;

  ExtensionActionManager* extension_action_manager =
      ExtensionActionManager::Get(profile());

  for (ExtensionSet::const_iterator i = service->extensions()->begin();
       i != service->extensions()->end(); ++i) {
    ExtensionAction* action =
        extension_action_manager->GetPageAction(*i->get());
    if (action)
      current_actions.push_back(action);
  }

  return current_actions;
}

LocationBarController::Action PageActionController::OnClicked(
    const std::string& extension_id, int mouse_button) {
  ExtensionService* service = GetExtensionService();
  if (!service)
    return ACTION_NONE;

  const Extension* extension = service->extensions()->GetByID(extension_id);
  CHECK(extension);
  ExtensionAction* page_action =
      ExtensionActionManager::Get(profile())->GetPageAction(*extension);
  CHECK(page_action);
  int tab_id = ExtensionTabUtil::GetTabId(web_contents());

  extensions::TabHelper::FromWebContents(web_contents())->
      active_tab_permission_granter()->GrantIfRequested(extension);

  switch (mouse_button) {
    case 1:  // left
    case 2:  // middle
      if (page_action->HasPopup(tab_id))
        return ACTION_SHOW_POPUP;

      GetExtensionService()->browser_event_router()->PageActionExecuted(
          profile(), *page_action, tab_id,
          web_contents()->GetURL().spec(), mouse_button);
      return ACTION_NONE;

    case 3:  // right
      return extension->ShowConfigureContextMenus() ?
          ACTION_SHOW_CONTEXT_MENU : ACTION_NONE;
  }

  return ACTION_NONE;
}

void PageActionController::NotifyChange() {
  web_contents()->NotifyNavigationStateChanged(
      content::INVALIDATE_TYPE_PAGE_ACTIONS);
}

void PageActionController::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
  if (details.is_in_page)
    return;

  const std::vector<ExtensionAction*> current_actions = GetCurrentActions();

  if (current_actions.empty())
    return;

  for (size_t i = 0; i < current_actions.size(); ++i) {
    current_actions[i]->ClearAllValuesForTab(
        SessionID::IdForTab(web_contents()));
  }

  NotifyChange();
}

Profile* PageActionController::profile() const {
  return Profile::FromBrowserContext(web_contents()->GetBrowserContext());
}

ExtensionService* PageActionController::GetExtensionService() const {
  return ExtensionSystem::Get(profile())->extension_service();
}

}  // namespace extensions
