// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/fullscreen/fullscreen_controller.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/message_loop/message_loop.h"
#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/download/download_shelf.h"
#include "chrome/browser/fullscreen.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"

#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#endif

using content::RenderViewHost;
using content::UserMetricsAction;
using content::WebContents;

FullscreenController::FullscreenController(Browser* browser)
    : ptr_factory_(this),
      browser_(browser),
      window_(browser->window()),
      profile_(browser->profile()),
      fullscreened_tab_(NULL),
      state_prior_to_tab_fullscreen_(STATE_INVALID),
      tab_fullscreen_accepted_(false),
      toggled_into_fullscreen_(false),
      mouse_lock_tab_(NULL),
      mouse_lock_state_(MOUSELOCK_NOT_REQUESTED),
      reentrant_window_state_change_call_check_(false) {
  DCHECK(window_);
  DCHECK(profile_);
}

FullscreenController::~FullscreenController() {
}

bool FullscreenController::IsFullscreenForBrowser() const {
  return window_->IsFullscreen() && !IsFullscreenCausedByTab();
}

void FullscreenController::ToggleFullscreenMode() {
  extension_caused_fullscreen_ = GURL();
  ToggleFullscreenModeInternal(BROWSER);
}

bool FullscreenController::IsFullscreenForTabOrPending() const {
  return fullscreened_tab_ != NULL;
}

bool FullscreenController::IsFullscreenForTabOrPending(
    const WebContents* web_contents) const {
  if (web_contents != fullscreened_tab_)
    return false;
  DCHECK(web_contents == browser_->tab_strip_model()->GetActiveWebContents());
  return true;
}

bool FullscreenController::IsFullscreenCausedByTab() const {
  return state_prior_to_tab_fullscreen_ == STATE_NORMAL;
}

void FullscreenController::ToggleFullscreenModeForTab(WebContents* web_contents,
                                                      bool enter_fullscreen) {
  if (fullscreened_tab_) {
    if (web_contents != fullscreened_tab_)
      return;
  } else if (
      web_contents != browser_->tab_strip_model()->GetActiveWebContents()) {
    return;
  }
  if (IsFullscreenForTabOrPending() == enter_fullscreen)
    return;

#if defined(OS_WIN)
  // For now, avoid breaking when initiating full screen tab mode while in
  // a metro snap.
  // TODO(robertshield): Find a way to reconcile tab-initiated fullscreen
  //                     modes with metro snap.
  if (IsInMetroSnapMode())
    return;
#endif

  bool in_browser_or_tab_fullscreen_mode = window_->IsFullscreen();
  bool window_is_fullscreen_with_chrome = false;
#if defined(OS_MACOSX)
  window_is_fullscreen_with_chrome = window_->IsFullscreenWithChrome();
#endif

  if (enter_fullscreen) {
    SetFullscreenedTab(web_contents);
    if (!in_browser_or_tab_fullscreen_mode) {
      state_prior_to_tab_fullscreen_ = STATE_NORMAL;
      ToggleFullscreenModeInternal(TAB);
    } else if (window_is_fullscreen_with_chrome) {
#if defined(OS_MACOSX)
      state_prior_to_tab_fullscreen_ = STATE_BROWSER_FULLSCREEN_WITH_CHROME;
      EnterFullscreenModeInternal(TAB);
#else
      NOTREACHED();
#endif
    } else {
      state_prior_to_tab_fullscreen_ = STATE_BROWSER_FULLSCREEN_NO_CHROME;

      // We need to update the fullscreen exit bubble, e.g., going from browser
      // fullscreen to tab fullscreen will need to show different content.
      const GURL& url = web_contents->GetURL();
      if (!tab_fullscreen_accepted_) {
        tab_fullscreen_accepted_ =
            GetFullscreenSetting(url) == CONTENT_SETTING_ALLOW;
      }
      UpdateFullscreenExitBubbleContent();

      // This is only a change between Browser and Tab fullscreen. We generate
      // a fullscreen notification now because there is no window change.
      PostFullscreenChangeNotification(true);
    }
  } else {
    if (in_browser_or_tab_fullscreen_mode) {
      if (IsFullscreenCausedByTab()) {
        ToggleFullscreenModeInternal(TAB);
      } else {
#if defined(OS_MACOSX)
        if (state_prior_to_tab_fullscreen_ ==
            STATE_BROWSER_FULLSCREEN_WITH_CHROME) {
          EnterFullscreenModeInternal(BROWSER_WITH_CHROME);
        } else {
          // Clear the bubble URL, which forces the Mac UI to redraw.
          UpdateFullscreenExitBubbleContent();
        }
#endif
        // If currently there is a tab in "tab fullscreen" mode and fullscreen
        // was not caused by it (i.e., previously it was in "browser fullscreen"
        // mode), we need to switch back to "browser fullscreen" mode. In this
        // case, all we have to do is notifying the tab that it has exited "tab
        // fullscreen" mode.
        NotifyTabOfExitIfNecessary();

        // This is only a change between Browser and Tab fullscreen. We generate
        // a fullscreen notification now because there is no window change.
        PostFullscreenChangeNotification(true);
      }
    }
  }
}

void FullscreenController::ToggleFullscreenModeWithExtension(
    const GURL& extension_url) {
  // |extension_caused_fullscreen_| will be reset if this causes fullscreen to
  // exit.
  extension_caused_fullscreen_ = extension_url;
  ToggleFullscreenModeInternal(BROWSER);
}

bool FullscreenController::IsInMetroSnapMode() {
#if defined(OS_WIN)
  return window_->IsInMetroSnapMode();
#else
  return false;
#endif
}

#if defined(OS_WIN)
void FullscreenController::SetMetroSnapMode(bool enable) {
  reentrant_window_state_change_call_check_ = false;

  toggled_into_fullscreen_ = false;
  window_->SetMetroSnapMode(enable);

  // FullscreenController unit tests for metro snap assume that on Windows calls
  // to WindowFullscreenStateChanged are reentrant. If that assumption is
  // invalidated, the tests must be updated to maintain coverage.
  CHECK(reentrant_window_state_change_call_check_);
}
#endif  // defined(OS_WIN)

#if defined(OS_MACOSX)
void FullscreenController::ToggleFullscreenWithChrome() {
  // This method cannot be called if simplified fullscreen is enabled.
  const CommandLine* command_line = CommandLine::ForCurrentProcess();
  DCHECK(!command_line->HasSwitch(switches::kEnableSimplifiedFullscreen));
  ToggleFullscreenModeInternal(BROWSER_WITH_CHROME);
}
#endif

bool FullscreenController::IsMouseLockRequested() const {
  return mouse_lock_state_ == MOUSELOCK_REQUESTED;
}

bool FullscreenController::IsMouseLocked() const {
  return mouse_lock_state_ == MOUSELOCK_ACCEPTED ||
         mouse_lock_state_ == MOUSELOCK_ACCEPTED_SILENTLY;
}

void FullscreenController::RequestToLockMouse(WebContents* web_contents,
                                              bool user_gesture,
                                              bool last_unlocked_by_target) {
  DCHECK(!IsMouseLocked());
  NotifyMouseLockChange();

  // Must have a user gesture to prevent misbehaving sites from constantly
  // re-locking the mouse. Exceptions are when the page has unlocked
  // (i.e. not the user), or if we're in tab fullscreen (user gesture required
  // for that)
  if (!last_unlocked_by_target && !user_gesture &&
      !IsFullscreenForTabOrPending(web_contents)) {
    web_contents->GotResponseToLockMouseRequest(false);
    return;
  }
  SetMouseLockTab(web_contents);
  FullscreenExitBubbleType bubble_type = GetFullscreenExitBubbleType();

  switch (GetMouseLockSetting(web_contents->GetURL())) {
    case CONTENT_SETTING_ALLOW:
      // If bubble already displaying buttons we must not lock the mouse yet,
      // or it would prevent pressing those buttons. Instead, merge the request.
      if (fullscreen_bubble::ShowButtonsForType(bubble_type)) {
        mouse_lock_state_ = MOUSELOCK_REQUESTED;
      } else {
        // Lock mouse.
        if (web_contents->GotResponseToLockMouseRequest(true)) {
          if (last_unlocked_by_target) {
            mouse_lock_state_ = MOUSELOCK_ACCEPTED_SILENTLY;
          } else {
            mouse_lock_state_ = MOUSELOCK_ACCEPTED;
          }
        } else {
          SetMouseLockTab(NULL);
          mouse_lock_state_ = MOUSELOCK_NOT_REQUESTED;
        }
      }
      break;
    case CONTENT_SETTING_BLOCK:
      web_contents->GotResponseToLockMouseRequest(false);
      SetMouseLockTab(NULL);
      mouse_lock_state_ = MOUSELOCK_NOT_REQUESTED;
      break;
    case CONTENT_SETTING_ASK:
      mouse_lock_state_ = MOUSELOCK_REQUESTED;
      break;
    default:
      NOTREACHED();
  }
  UpdateFullscreenExitBubbleContent();
}

void FullscreenController::OnTabDeactivated(WebContents* web_contents) {
  if (web_contents == fullscreened_tab_ || web_contents == mouse_lock_tab_)
    ExitTabFullscreenOrMouseLockIfNecessary();
}

void FullscreenController::OnTabClosing(WebContents* web_contents) {
  if (web_contents == fullscreened_tab_ || web_contents == mouse_lock_tab_) {
    ExitTabFullscreenOrMouseLockIfNecessary();
    // The call to exit fullscreen may result in asynchronous notification of
    // fullscreen state change (e.g., on Linux). We don't want to rely on it
    // to call NotifyTabOfExitIfNecessary(), because at that point
    // |fullscreened_tab_| may not be valid. Instead, we call it here to clean
    // up tab fullscreen related state.
    NotifyTabOfExitIfNecessary();
  }
}

void FullscreenController::WindowFullscreenStateChanged() {
  reentrant_window_state_change_call_check_ = true;

  bool exiting_fullscreen = !window_->IsFullscreen();

  PostFullscreenChangeNotification(!exiting_fullscreen);
  if (exiting_fullscreen) {
    toggled_into_fullscreen_ = false;
    extension_caused_fullscreen_ = GURL();
    NotifyTabOfExitIfNecessary();
  }
  if (exiting_fullscreen)
    window_->GetDownloadShelf()->Unhide();
  else
    window_->GetDownloadShelf()->Hide();
}

bool FullscreenController::HandleUserPressedEscape() {
  if (IsFullscreenForTabOrPending() ||
      IsMouseLocked() || IsMouseLockRequested()) {
    ExitTabFullscreenOrMouseLockIfNecessary();
    return true;
  }

  return false;
}

void FullscreenController::ExitTabOrBrowserFullscreenToPreviousState() {
  if (IsFullscreenForTabOrPending())
    ExitTabFullscreenOrMouseLockIfNecessary();
  else if (IsFullscreenForBrowser())
    ExitFullscreenModeInternal();
}

void FullscreenController::OnAcceptFullscreenPermission() {
  FullscreenExitBubbleType bubble_type = GetFullscreenExitBubbleType();
  bool mouse_lock = false;
  bool fullscreen = false;
  fullscreen_bubble::PermissionRequestedByType(bubble_type, &fullscreen,
                                               &mouse_lock);
  DCHECK(!(fullscreen && tab_fullscreen_accepted_));
  DCHECK(!(mouse_lock && IsMouseLocked()));

  HostContentSettingsMap* settings_map = profile_->GetHostContentSettingsMap();

  GURL url = GetFullscreenExitBubbleURL();
  ContentSettingsPattern pattern = ContentSettingsPattern::FromURL(url);

  if (mouse_lock && !IsMouseLocked()) {
    DCHECK(IsMouseLockRequested());
    // TODO(markusheintz): We should allow patterns for all possible URLs here.
    if (pattern.IsValid()) {
      settings_map->SetContentSetting(
          pattern, ContentSettingsPattern::Wildcard(),
          CONTENT_SETTINGS_TYPE_MOUSELOCK, std::string(),
          CONTENT_SETTING_ALLOW);
    }

    if (mouse_lock_tab_ &&
        mouse_lock_tab_->GotResponseToLockMouseRequest(true)) {
      mouse_lock_state_ = MOUSELOCK_ACCEPTED;
    } else {
      mouse_lock_state_ = MOUSELOCK_NOT_REQUESTED;
      SetMouseLockTab(NULL);
    }
    NotifyMouseLockChange();
  }

  if (fullscreen && !tab_fullscreen_accepted_) {
    DCHECK(fullscreened_tab_);
    if (pattern.IsValid()) {
      settings_map->SetContentSetting(
          pattern, ContentSettingsPattern::Wildcard(),
          CONTENT_SETTINGS_TYPE_FULLSCREEN, std::string(),
          CONTENT_SETTING_ALLOW);
    }
    tab_fullscreen_accepted_ = true;
  }
  UpdateFullscreenExitBubbleContent();
}

void FullscreenController::OnDenyFullscreenPermission() {
  if (!fullscreened_tab_ && !mouse_lock_tab_)
    return;

  if (IsMouseLockRequested()) {
    mouse_lock_state_ = MOUSELOCK_NOT_REQUESTED;
    if (mouse_lock_tab_)
      mouse_lock_tab_->GotResponseToLockMouseRequest(false);
    SetMouseLockTab(NULL);
    NotifyMouseLockChange();

    // UpdateFullscreenExitBubbleContent() must be called, but to avoid
    // duplicate calls we do so only if not adjusting the fullscreen state
    // below, which also calls UpdateFullscreenExitBubbleContent().
    if (!IsFullscreenForTabOrPending())
      UpdateFullscreenExitBubbleContent();
  }

  if (IsFullscreenForTabOrPending())
    ExitTabFullscreenOrMouseLockIfNecessary();
}

void FullscreenController::LostMouseLock() {
  mouse_lock_state_ = MOUSELOCK_NOT_REQUESTED;
  SetMouseLockTab(NULL);
  NotifyMouseLockChange();
  UpdateFullscreenExitBubbleContent();
}

void FullscreenController::Observe(int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_NAV_ENTRY_COMMITTED:
      if (content::Details<content::LoadCommittedDetails>(details)->
              is_navigation_to_different_page()) {
        ExitTabFullscreenOrMouseLockIfNecessary();
      }
      break;

    default:
      NOTREACHED() << "Got a notification we didn't register for.";
  }
}

GURL FullscreenController::GetFullscreenExitBubbleURL() const {
  if (fullscreened_tab_)
    return fullscreened_tab_->GetURL();
  else if (mouse_lock_tab_)
    return mouse_lock_tab_->GetURL();
  else if (!extension_caused_fullscreen_.is_empty())
    return extension_caused_fullscreen_;
  else
    return GURL();
}

FullscreenExitBubbleType FullscreenController::GetFullscreenExitBubbleType()
    const {
  // In kiosk and exclusive app mode we always want to be fullscreen and do not
  // want to show exit instructions for browser mode fullscreen.
  bool app_mode = false;
#if !defined(OS_MACOSX)  // App mode (kiosk) is not available on Mac yet.
  app_mode = chrome::IsRunningInAppMode();
#endif

  if (mouse_lock_state_ == MOUSELOCK_ACCEPTED_SILENTLY) {
    return FEB_TYPE_NONE;
  }

  if (fullscreened_tab_) {
    if (tab_fullscreen_accepted_) {
      if (IsMouseLocked()) {
        return FEB_TYPE_FULLSCREEN_MOUSELOCK_EXIT_INSTRUCTION;
      } else if (IsMouseLockRequested()) {
        return FEB_TYPE_MOUSELOCK_BUTTONS;
      } else {
        return FEB_TYPE_FULLSCREEN_EXIT_INSTRUCTION;
      }
    } else {  // Full screen not yet accepted.
      if (IsMouseLockRequested()) {
        return FEB_TYPE_FULLSCREEN_MOUSELOCK_BUTTONS;
      } else {
        return FEB_TYPE_FULLSCREEN_BUTTONS;
      }
    }
  } else {  // Not tab full screen.
    if (IsMouseLocked()) {
      return FEB_TYPE_MOUSELOCK_EXIT_INSTRUCTION;
    } else if (IsMouseLockRequested()) {
      return FEB_TYPE_MOUSELOCK_BUTTONS;
    } else {
      if (!extension_caused_fullscreen_.is_empty()) {
        return FEB_TYPE_BROWSER_EXTENSION_FULLSCREEN_EXIT_INSTRUCTION;
      } else if (toggled_into_fullscreen_ && !app_mode) {
        return FEB_TYPE_BROWSER_FULLSCREEN_EXIT_INSTRUCTION;
      } else {
        return FEB_TYPE_NONE;
      }
    }
  }
  NOTREACHED();
  return FEB_TYPE_NONE;
}

void FullscreenController::UpdateNotificationRegistrations() {
  if (fullscreened_tab_ && mouse_lock_tab_)
    DCHECK(fullscreened_tab_ == mouse_lock_tab_);

  WebContents* tab = fullscreened_tab_ ? fullscreened_tab_ : mouse_lock_tab_;

  if (tab && registrar_.IsEmpty()) {
    registrar_.Add(this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
        content::Source<content::NavigationController>(&tab->GetController()));
  } else if (!tab && !registrar_.IsEmpty()) {
    registrar_.RemoveAll();
  }
}

void FullscreenController::PostFullscreenChangeNotification(
    bool is_fullscreen) {
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&FullscreenController::NotifyFullscreenChange,
                 ptr_factory_.GetWeakPtr(),
                 is_fullscreen));
}

void FullscreenController::NotifyFullscreenChange(bool is_fullscreen) {
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_FULLSCREEN_CHANGED,
      content::Source<FullscreenController>(this),
      content::Details<bool>(&is_fullscreen));
}

void FullscreenController::NotifyTabOfExitIfNecessary() {
  if (fullscreened_tab_) {
    RenderViewHost* rvh = fullscreened_tab_->GetRenderViewHost();
    SetFullscreenedTab(NULL);
    state_prior_to_tab_fullscreen_ = STATE_INVALID;
    tab_fullscreen_accepted_ = false;
    if (rvh)
      rvh->ExitFullscreen();
  }

  if (mouse_lock_tab_) {
    if (IsMouseLockRequested()) {
      mouse_lock_tab_->GotResponseToLockMouseRequest(false);
      NotifyMouseLockChange();
    } else if (mouse_lock_tab_->GetRenderViewHost() &&
               mouse_lock_tab_->GetRenderViewHost()->GetView()) {
      mouse_lock_tab_->GetRenderViewHost()->GetView()->UnlockMouse();
    }
    SetMouseLockTab(NULL);
    mouse_lock_state_ = MOUSELOCK_NOT_REQUESTED;
  }

  UpdateFullscreenExitBubbleContent();
}

void FullscreenController::NotifyMouseLockChange() {
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_MOUSE_LOCK_CHANGED,
      content::Source<FullscreenController>(this),
      content::NotificationService::NoDetails());
}

// TODO(koz): Change |for_tab| to an enum.
void FullscreenController::ToggleFullscreenModeInternal(
    FullscreenInternalOption option) {
#if defined(OS_WIN)
  // When in Metro snap mode, toggling in and out of fullscreen is prevented.
  if (IsInMetroSnapMode())
    return;
#endif

  bool enter_fullscreen = !window_->IsFullscreen();
#if defined(OS_MACOSX)
  // When a Mac user requests a toggle they may be toggling between
  // FullscreenWithoutChrome and FullscreenWithChrome.
  if (!IsFullscreenForTabOrPending()) {
    if (option == BROWSER_WITH_CHROME)
      enter_fullscreen |= window_->IsFullscreenWithoutChrome();
    else
      enter_fullscreen |= window_->IsFullscreenWithChrome();
  }
#endif

  // In kiosk mode, we always want to be fullscreen. When the browser first
  // starts we're not yet fullscreen, so let the initial toggle go through.
  if (chrome::IsRunningInAppMode() && window_->IsFullscreen())
    return;

  if (enter_fullscreen)
    EnterFullscreenModeInternal(option);
  else
    ExitFullscreenModeInternal();
}

void FullscreenController::EnterFullscreenModeInternal(
    FullscreenInternalOption option) {
  toggled_into_fullscreen_ = true;
  GURL url;
  if (option == TAB) {
    url = browser_->tab_strip_model()->GetActiveWebContents()->GetURL();
    tab_fullscreen_accepted_ =
        GetFullscreenSetting(url) == CONTENT_SETTING_ALLOW;
  } else {
    if (!extension_caused_fullscreen_.is_empty())
      url = extension_caused_fullscreen_;
  }

  if (option == BROWSER)
    content::RecordAction(UserMetricsAction("ToggleFullscreen"));
  // TODO(scheib): Record metrics for WITH_CHROME, without counting transitions
  // from tab fullscreen out to browser with chrome.

#if defined(OS_MACOSX)
  if (option == BROWSER_WITH_CHROME) {
    CHECK(chrome::mac::SupportsSystemFullscreen());
    window_->EnterFullscreenWithChrome();
  } else {
#else
  {
#endif
    window_->EnterFullscreen(url, GetFullscreenExitBubbleType());
  }

  UpdateFullscreenExitBubbleContent();

  // Once the window has become fullscreen it'll call back to
  // WindowFullscreenStateChanged(). We don't do this immediately as
  // BrowserWindow::EnterFullscreen() asks for bookmark_bar_state_, so we let
  // the BrowserWindow invoke WindowFullscreenStateChanged when appropriate.
}

void FullscreenController::ExitFullscreenModeInternal() {
  toggled_into_fullscreen_ = false;
#if defined(OS_MACOSX)
  // Mac windows report a state change instantly, and so we must also clear
  // state_prior_to_tab_fullscreen_ to match them else other logic using
  // state_prior_to_tab_fullscreen_ will be incorrect.
  NotifyTabOfExitIfNecessary();
#endif
  window_->ExitFullscreen();
  extension_caused_fullscreen_ = GURL();

  UpdateFullscreenExitBubbleContent();
}

void FullscreenController::SetFullscreenedTab(WebContents* tab) {
  fullscreened_tab_ = tab;
  UpdateNotificationRegistrations();
}

void FullscreenController::SetMouseLockTab(WebContents* tab) {
  mouse_lock_tab_ = tab;
  UpdateNotificationRegistrations();
}

void FullscreenController::ExitTabFullscreenOrMouseLockIfNecessary() {
  if (IsFullscreenForTabOrPending())
    ToggleFullscreenModeForTab(fullscreened_tab_, false);
  else
    NotifyTabOfExitIfNecessary();
}

void FullscreenController::UpdateFullscreenExitBubbleContent() {
  GURL url = GetFullscreenExitBubbleURL();
  FullscreenExitBubbleType bubble_type = GetFullscreenExitBubbleType();

  // If bubble displays buttons, unlock mouse to allow pressing them.
  if (fullscreen_bubble::ShowButtonsForType(bubble_type) &&
      IsMouseLocked() &&
      mouse_lock_tab_ &&
      mouse_lock_tab_->GetRenderViewHost() &&
      mouse_lock_tab_->GetRenderViewHost()->GetView()) {
    mouse_lock_tab_->GetRenderViewHost()->GetView()->UnlockMouse();
  }

  window_->UpdateFullscreenExitBubbleContent(url, bubble_type);
}

ContentSetting
FullscreenController::GetFullscreenSetting(const GURL& url) const {
  if (url.SchemeIsFile())
    return CONTENT_SETTING_ALLOW;

  return profile_->GetHostContentSettingsMap()->GetContentSetting(url, url,
      CONTENT_SETTINGS_TYPE_FULLSCREEN, std::string());
}

ContentSetting
FullscreenController::GetMouseLockSetting(const GURL& url) const {
  if (url.SchemeIsFile())
    return CONTENT_SETTING_ALLOW;

  HostContentSettingsMap* settings_map = profile_->GetHostContentSettingsMap();
  return settings_map->GetContentSetting(url, url,
      CONTENT_SETTINGS_TYPE_MOUSELOCK, std::string());
}
