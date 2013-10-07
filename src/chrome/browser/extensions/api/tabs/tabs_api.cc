// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/tabs/tabs_api.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "apps/shell_window.h"
#include "base/base64.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/message_loop/message_loop.h"
#include "base/prefs/pref_service.h"
#include "base/stl_util.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/tabs/tabs_constants.h"
#include "chrome/browser/extensions/extension_function_dispatcher.h"
#include "chrome/browser/extensions/extension_function_util.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/script_executor.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/extensions/window_controller.h"
#include "chrome/browser/extensions/window_controller_list.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/translate/translate_tab_helper.h"
#include "chrome/browser/ui/apps/chrome_shell_window_delegate.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_iterator.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chrome/browser/ui/panels/panel_manager.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/window_sizer/window_sizer.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/api/i18n/default_locale_handler.h"
#include "chrome/common/extensions/api/tabs.h"
#include "chrome/common/extensions/api/windows.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_file_util.h"
#include "chrome/common/extensions/extension_l10n_util.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/extensions/incognito_handler.h"
#include "chrome/common/extensions/message_bundle.h"
#include "chrome/common/extensions/permissions/permissions_data.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/translate/language_detection_details.h"
#include "chrome/common/url_constants.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/common/url_constants.h"
#include "extensions/browser/file_reader.h"
#include "extensions/common/constants.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/user_script.h"
#include "skia/ext/image_operations.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"

#if defined(OS_WIN)
#include "win8/util/win8_util.h"
#endif  // OS_WIN

#if defined(USE_ASH)
#include "apps/shell_window_registry.h"
#include "ash/ash_switches.h"
#include "chrome/browser/extensions/api/tabs/ash_panel_contents.h"
#endif

using apps::ShellWindow;
using content::BrowserThread;
using content::NavigationController;
using content::NavigationEntry;
using content::OpenURLParams;
using content::Referrer;
using content::RenderViewHost;
using content::WebContents;

namespace extensions {

namespace Get = api::windows::Get;
namespace GetAll = api::windows::GetAll;
namespace GetCurrent = api::windows::GetCurrent;
namespace GetLastFocused = api::windows::GetLastFocused;
namespace errors = extension_manifest_errors;
namespace keys = tabs_constants;
namespace tabs = api::tabs;
typedef tabs::CaptureVisibleTab::Params::Options FormatEnum;

using api::tabs::InjectDetails;

const int TabsCaptureVisibleTabFunction::kDefaultQuality = 90;

namespace {

// |error_message| can optionally be passed in a will be set with an appropriate
// message if the window cannot be found by id.
Browser* GetBrowserInProfileWithId(Profile* profile,
                                   const int window_id,
                                   bool include_incognito,
                                   std::string* error_message) {
  Profile* incognito_profile =
      include_incognito && profile->HasOffTheRecordProfile() ?
          profile->GetOffTheRecordProfile() : NULL;
  for (chrome::BrowserIterator it; !it.done(); it.Next()) {
    Browser* browser = *it;
    if ((browser->profile() == profile ||
         browser->profile() == incognito_profile) &&
        ExtensionTabUtil::GetWindowId(browser) == window_id &&
        browser->window()) {
      return browser;
    }
  }

  if (error_message)
    *error_message = ErrorUtils::FormatErrorMessage(
        keys::kWindowNotFoundError, base::IntToString(window_id));

  return NULL;
}

bool GetBrowserFromWindowID(
    UIThreadExtensionFunction* function, int window_id, Browser** browser) {
  if (window_id == extension_misc::kCurrentWindowId) {
    *browser = function->GetCurrentBrowser();
    if (!(*browser) || !(*browser)->window()) {
      function->SetError(keys::kNoCurrentWindowError);
      return false;
    }
  } else {
    std::string error;
    *browser = GetBrowserInProfileWithId(
        function->profile(), window_id, function->include_incognito(), &error);
    if (!*browser) {
      function->SetError(error);
      return false;
    }
  }
  return true;
}

bool GetWindowFromWindowID(UIThreadExtensionFunction* function,
                           int window_id,
                           WindowController** controller) {
  if (window_id == extension_misc::kCurrentWindowId) {
    WindowController* extension_window_controller =
        function->dispatcher()->delegate()->GetExtensionWindowController();
    // If there is a window controller associated with this extension, use that.
    if (extension_window_controller) {
      *controller = extension_window_controller;
    } else {
      // Otherwise get the focused or most recently added window.
      *controller = WindowControllerList::GetInstance()->
          CurrentWindowForFunction(function);
    }
    if (!(*controller)) {
      function->SetError(keys::kNoCurrentWindowError);
      return false;
    }
  } else {
    *controller = WindowControllerList::GetInstance()->
        FindWindowForFunctionById(function, window_id);
    if (!(*controller)) {
      function->SetError(ErrorUtils::FormatErrorMessage(
          keys::kWindowNotFoundError, base::IntToString(window_id)));
      return false;
    }
  }
  return true;
}

// |error_message| can optionally be passed in and will be set with an
// appropriate message if the tab cannot be found by id.
bool GetTabById(int tab_id,
                Profile* profile,
                bool include_incognito,
                Browser** browser,
                TabStripModel** tab_strip,
                content::WebContents** contents,
                int* tab_index,
                std::string* error_message) {
  if (ExtensionTabUtil::GetTabById(tab_id, profile, include_incognito,
                                   browser, tab_strip, contents, tab_index))
    return true;

  if (error_message)
    *error_message = ErrorUtils::FormatErrorMessage(
        keys::kTabNotFoundError, base::IntToString(tab_id));

  return false;
}

// Returns true if either |boolean| is a null pointer, or if |*boolean| and
// |value| are equal. This function is used to check if a tab's parameters match
// those of the browser.
bool MatchesBool(bool* boolean, bool value) {
  return !boolean || *boolean == value;
}

Browser* CreateBrowserWindow(const Browser::CreateParams& params,
                             Profile* profile,
                             const std::string& extension_id) {
  bool use_existing_browser_window = false;

#if defined(OS_WIN)
  // In windows 8 metro mode we don't allow windows to be created.
  if (win8::IsSingleWindowMetroMode())
    use_existing_browser_window = true;
#endif  // OS_WIN

  Browser* new_window = NULL;
  if (use_existing_browser_window)
    // The false parameter passed below is to ensure that we find a browser
    // object matching the profile passed in, instead of the original profile
    new_window = chrome::FindTabbedBrowser(profile, false,
                                           params.host_desktop_type);

  if (!new_window)
    new_window = new Browser(params);
  return new_window;
}

}  // namespace

// Windows ---------------------------------------------------------------------

bool WindowsGetFunction::RunImpl() {
  scoped_ptr<Get::Params> params(Get::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  bool populate_tabs = false;
  if (params->get_info.get() && params->get_info->populate.get())
    populate_tabs = *params->get_info->populate;

  WindowController* controller;
  if (!GetWindowFromWindowID(this, params->window_id, &controller))
    return false;

  if (populate_tabs)
    SetResult(controller->CreateWindowValueWithTabs(GetExtension()));
  else
    SetResult(controller->CreateWindowValue());
  return true;
}

bool WindowsGetCurrentFunction::RunImpl() {
  scoped_ptr<GetCurrent::Params> params(GetCurrent::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  bool populate_tabs = false;
  if (params->get_info.get() && params->get_info->populate.get())
    populate_tabs = *params->get_info->populate;

  WindowController* controller;
  if (!GetWindowFromWindowID(this,
                             extension_misc::kCurrentWindowId,
                             &controller)) {
    return false;
  }
  if (populate_tabs)
    SetResult(controller->CreateWindowValueWithTabs(GetExtension()));
  else
    SetResult(controller->CreateWindowValue());
  return true;
}

bool WindowsGetLastFocusedFunction::RunImpl() {
  scoped_ptr<GetLastFocused::Params> params(
      GetLastFocused::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  bool populate_tabs = false;
  if (params->get_info.get() && params->get_info->populate.get())
    populate_tabs = *params->get_info->populate;

  // Note: currently this returns the last active browser. If we decide to
  // include other window types (e.g. panels), we will need to add logic to
  // WindowControllerList that mirrors the active behavior of BrowserList.
  Browser* browser = chrome::FindAnyBrowser(
      profile(), include_incognito(), chrome::GetActiveDesktop());
  if (!browser || !browser->window()) {
    error_ = keys::kNoLastFocusedWindowError;
    return false;
  }
  WindowController* controller =
      browser->extension_window_controller();
  if (populate_tabs)
    SetResult(controller->CreateWindowValueWithTabs(GetExtension()));
  else
    SetResult(controller->CreateWindowValue());
  return true;
}

bool WindowsGetAllFunction::RunImpl() {
  scoped_ptr<GetAll::Params> params(GetAll::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  bool populate_tabs = false;
  if (params->get_info.get() && params->get_info->populate.get())
    populate_tabs = *params->get_info->populate;

  base::ListValue* window_list = new base::ListValue();
  const WindowControllerList::ControllerList& windows =
      WindowControllerList::GetInstance()->windows();
  for (WindowControllerList::ControllerList::const_iterator iter =
           windows.begin();
       iter != windows.end(); ++iter) {
    if (!this->CanOperateOnWindow(*iter))
      continue;
    if (populate_tabs)
      window_list->Append((*iter)->CreateWindowValueWithTabs(GetExtension()));
    else
      window_list->Append((*iter)->CreateWindowValue());
  }
  SetResult(window_list);
  return true;
}

bool WindowsCreateFunction::ShouldOpenIncognitoWindow(
    const base::DictionaryValue* args,
    std::vector<GURL>* urls,
    bool* is_error) {
  *is_error = false;
  const IncognitoModePrefs::Availability incognito_availability =
      IncognitoModePrefs::GetAvailability(profile_->GetPrefs());
  bool incognito = false;
  if (args && args->HasKey(keys::kIncognitoKey)) {
    EXTENSION_FUNCTION_VALIDATE(args->GetBoolean(keys::kIncognitoKey,
                                                 &incognito));
    if (incognito && incognito_availability == IncognitoModePrefs::DISABLED) {
      error_ = keys::kIncognitoModeIsDisabled;
      *is_error = true;
      return false;
    }
    if (!incognito && incognito_availability == IncognitoModePrefs::FORCED) {
      error_ = keys::kIncognitoModeIsForced;
      *is_error = true;
      return false;
    }
  } else if (incognito_availability == IncognitoModePrefs::FORCED) {
    // If incognito argument is not specified explicitly, we default to
    // incognito when forced so by policy.
    incognito = true;
  }

  // Remove all URLs that are not allowed in an incognito session. Note that a
  // ChromeOS guest session is not considered incognito in this case.
  if (incognito && !profile_->IsGuestSession()) {
    std::string first_url_erased;
    for (size_t i = 0; i < urls->size();) {
      if (chrome::IsURLAllowedInIncognito((*urls)[i], profile())) {
        i++;
      } else {
        if (first_url_erased.empty())
          first_url_erased = (*urls)[i].spec();
        urls->erase(urls->begin() + i);
      }
    }
    if (urls->empty() && !first_url_erased.empty()) {
      error_ = ErrorUtils::FormatErrorMessage(
          keys::kURLsNotAllowedInIncognitoError, first_url_erased);
      *is_error = true;
      return false;
    }
  }
  return incognito;
}

bool WindowsCreateFunction::RunImpl() {
  base::DictionaryValue* args = NULL;
  std::vector<GURL> urls;
  TabStripModel* source_tab_strip = NULL;
  int tab_index = -1;

  if (HasOptionalArgument(0))
    EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(0, &args));

  // Look for optional url.
  if (args) {
    if (args->HasKey(keys::kUrlKey)) {
      Value* url_value;
      std::vector<std::string> url_strings;
      args->Get(keys::kUrlKey, &url_value);

      // First, get all the URLs the client wants to open.
      if (url_value->IsType(Value::TYPE_STRING)) {
        std::string url_string;
        url_value->GetAsString(&url_string);
        url_strings.push_back(url_string);
      } else if (url_value->IsType(Value::TYPE_LIST)) {
        const base::ListValue* url_list =
            static_cast<const base::ListValue*>(url_value);
        for (size_t i = 0; i < url_list->GetSize(); ++i) {
          std::string url_string;
          EXTENSION_FUNCTION_VALIDATE(url_list->GetString(i, &url_string));
          url_strings.push_back(url_string);
        }
      }

      // Second, resolve, validate and convert them to GURLs.
      for (std::vector<std::string>::iterator i = url_strings.begin();
           i != url_strings.end(); ++i) {
        GURL url = ExtensionTabUtil::ResolvePossiblyRelativeURL(
            *i, GetExtension());
        if (!url.is_valid()) {
          error_ = ErrorUtils::FormatErrorMessage(
              keys::kInvalidUrlError, *i);
          return false;
        }
        // Don't let the extension crash the browser or renderers.
        if (ExtensionTabUtil::IsCrashURL(url)) {
          error_ = keys::kNoCrashBrowserError;
          return false;
        }
        urls.push_back(url);
      }
    }
  }

  // Look for optional tab id.
  if (args) {
    int tab_id = -1;
    if (args->HasKey(keys::kTabIdKey)) {
      EXTENSION_FUNCTION_VALIDATE(args->GetInteger(keys::kTabIdKey, &tab_id));

      // Find the tab. |source_tab_strip| and |tab_index| will later be used to
      // move the tab into the created window.
      if (!GetTabById(tab_id, profile(), include_incognito(),
                      NULL, &source_tab_strip,
                      NULL, &tab_index, &error_))
        return false;
    }
  }

  Profile* window_profile = profile();
  Browser::Type window_type = Browser::TYPE_TABBED;
  bool create_panel = false;

  // panel_create_mode only applies if create_panel = true
  PanelManager::CreateMode panel_create_mode = PanelManager::CREATE_AS_DOCKED;

  gfx::Rect window_bounds;
  bool focused = true;
  bool saw_focus_key = false;
  std::string extension_id;

  // Decide whether we are opening a normal window or an incognito window.
  bool is_error = true;
  bool open_incognito_window = ShouldOpenIncognitoWindow(args, &urls,
                                                         &is_error);
  if (is_error) {
    // error_ member variable is set inside of ShouldOpenIncognitoWindow.
    return false;
  }
  if (open_incognito_window) {
    window_profile = window_profile->GetOffTheRecordProfile();
  }

  if (args) {
    // Figure out window type before figuring out bounds so that default
    // bounds can be set according to the window type.
    std::string type_str;
    if (args->HasKey(keys::kWindowTypeKey)) {
      EXTENSION_FUNCTION_VALIDATE(args->GetString(keys::kWindowTypeKey,
                                                  &type_str));
      if (type_str == keys::kWindowTypeValuePopup) {
        window_type = Browser::TYPE_POPUP;
        extension_id = GetExtension()->id();
      } else if (type_str == keys::kWindowTypeValuePanel ||
                 type_str == keys::kWindowTypeValueDetachedPanel) {
        extension_id = GetExtension()->id();
        bool use_panels = false;
#if !defined(OS_ANDROID)
        use_panels = PanelManager::ShouldUsePanels(extension_id);
#endif
        if (use_panels) {
          create_panel = true;
#if !defined(OS_CHROMEOS)
          // Non-ChromeOS has both docked and detached panel types.
          if (type_str == keys::kWindowTypeValueDetachedPanel)
            panel_create_mode = PanelManager::CREATE_AS_DETACHED;
#endif
        } else {
          window_type = Browser::TYPE_POPUP;
        }
      } else if (type_str != keys::kWindowTypeValueNormal) {
        error_ = keys::kInvalidWindowTypeError;
        return false;
      }
    }

    // Initialize default window bounds according to window type.
    if (window_type == Browser::TYPE_TABBED ||
        window_type == Browser::TYPE_POPUP ||
        create_panel) {
      // Try to position the new browser relative to its originating
      // browser window. The call offsets the bounds by kWindowTilePixels
      // (defined in WindowSizer to be 10).
      //
      // NOTE(rafaelw): It's ok if GetCurrentBrowser() returns NULL here.
      // GetBrowserWindowBounds will default to saved "default" values for
      // the app.
      ui::WindowShowState show_state = ui::SHOW_STATE_DEFAULT;
      WindowSizer::GetBrowserWindowBoundsAndShowState(std::string(),
                                                      gfx::Rect(),
                                                      GetCurrentBrowser(),
                                                      &window_bounds,
                                                      &show_state);
    }

    if (create_panel && PanelManager::CREATE_AS_DETACHED == panel_create_mode) {
      window_bounds.set_origin(
          PanelManager::GetInstance()->GetDefaultDetachedPanelOrigin());
    }

    // Any part of the bounds can optionally be set by the caller.
    int bounds_val = -1;
    if (args->HasKey(keys::kLeftKey)) {
      EXTENSION_FUNCTION_VALIDATE(args->GetInteger(keys::kLeftKey,
                                                   &bounds_val));
      window_bounds.set_x(bounds_val);
    }

    if (args->HasKey(keys::kTopKey)) {
      EXTENSION_FUNCTION_VALIDATE(args->GetInteger(keys::kTopKey,
                                                   &bounds_val));
      window_bounds.set_y(bounds_val);
    }

    if (args->HasKey(keys::kWidthKey)) {
      EXTENSION_FUNCTION_VALIDATE(args->GetInteger(keys::kWidthKey,
                                                   &bounds_val));
      window_bounds.set_width(bounds_val);
    }

    if (args->HasKey(keys::kHeightKey)) {
      EXTENSION_FUNCTION_VALIDATE(args->GetInteger(keys::kHeightKey,
                                                   &bounds_val));
      window_bounds.set_height(bounds_val);
    }

    if (args->HasKey(keys::kFocusedKey)) {
      EXTENSION_FUNCTION_VALIDATE(args->GetBoolean(keys::kFocusedKey,
                                                   &focused));
      saw_focus_key = true;
    }
  }

  if (create_panel) {
    if (urls.empty())
      urls.push_back(GURL(chrome::kChromeUINewTabURL));

#if defined(OS_CHROMEOS)
    if (PanelManager::ShouldUsePanels(extension_id)) {
      ShellWindow::CreateParams create_params;
      create_params.window_type = ShellWindow::WINDOW_TYPE_V1_PANEL;
      create_params.bounds = window_bounds;
      create_params.focused = saw_focus_key && focused;
      ShellWindow* shell_window = new ShellWindow(
          window_profile, new ChromeShellWindowDelegate(),
          GetExtension());
      AshPanelContents* ash_panel_contents = new AshPanelContents(shell_window);
      shell_window->Init(urls[0], ash_panel_contents, create_params);
      SetResult(ash_panel_contents->GetExtensionWindowController()->
                CreateWindowValueWithTabs(GetExtension()));
      return true;
    }
#else
    std::string title =
        web_app::GenerateApplicationNameFromExtensionId(extension_id);
    // Note: Panels ignore all but the first url provided.
    Panel* panel = PanelManager::GetInstance()->CreatePanel(
        title, window_profile, urls[0], window_bounds, panel_create_mode);

    // Unlike other window types, Panels do not take focus by default.
    if (!saw_focus_key || !focused)
      panel->ShowInactive();
    else
      panel->Show();

    SetResult(
        panel->extension_window_controller()->CreateWindowValueWithTabs(
            GetExtension()));
    return true;
#endif
  }

  // Create a new BrowserWindow.
  chrome::HostDesktopType host_desktop_type = chrome::GetActiveDesktop();
  if (create_panel)
    window_type = Browser::TYPE_POPUP;
  Browser::CreateParams create_params(window_type, window_profile,
                                      host_desktop_type);
  if (extension_id.empty()) {
    create_params.initial_bounds = window_bounds;
  } else {
    create_params = Browser::CreateParams::CreateForApp(
        window_type,
        web_app::GenerateApplicationNameFromExtensionId(extension_id),
        window_bounds,
        window_profile,
        host_desktop_type);
  }
  create_params.initial_show_state = ui::SHOW_STATE_NORMAL;
  create_params.host_desktop_type = chrome::GetActiveDesktop();

  Browser* new_window = CreateBrowserWindow(create_params, window_profile,
                                            extension_id);

  for (std::vector<GURL>::iterator i = urls.begin(); i != urls.end(); ++i) {
    WebContents* tab = chrome::AddSelectedTabWithURL(
        new_window, *i, content::PAGE_TRANSITION_LINK);
    if (create_panel) {
      TabHelper::FromWebContents(tab)->SetExtensionAppIconById(extension_id);
    }
  }

  WebContents* contents = NULL;
  // Move the tab into the created window only if it's an empty popup or it's
  // a tabbed window.
  if ((window_type == Browser::TYPE_POPUP && urls.empty()) ||
      window_type == Browser::TYPE_TABBED) {
    if (source_tab_strip)
      contents = source_tab_strip->DetachWebContentsAt(tab_index);
    if (contents) {
      TabStripModel* target_tab_strip = new_window->tab_strip_model();
      target_tab_strip->InsertWebContentsAt(urls.size(), contents,
                                            TabStripModel::ADD_NONE);
    }
  }
  // Create a new tab if the created window is still empty. Don't create a new
  // tab when it is intended to create an empty popup.
  if (!contents && urls.empty() && window_type != Browser::TYPE_POPUP) {
    chrome::NewTab(new_window);
  }
  chrome::SelectNumberedTab(new_window, 0);

  // Unlike other window types, Panels do not take focus by default.
  if (!saw_focus_key && create_panel)
    focused = false;

  if (focused)
    new_window->window()->Show();
  else
    new_window->window()->ShowInactive();

  if (new_window->profile()->IsOffTheRecord() && !include_incognito()) {
    // Don't expose incognito windows if the extension isn't allowed.
    SetResult(Value::CreateNullValue());
  } else {
    SetResult(
        new_window->extension_window_controller()->CreateWindowValueWithTabs(
            GetExtension()));
  }

  return true;
}

bool WindowsUpdateFunction::RunImpl() {
  int window_id = extension_misc::kUnknownWindowId;
  EXTENSION_FUNCTION_VALIDATE(args_->GetInteger(0, &window_id));
  base::DictionaryValue* update_props;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(1, &update_props));

  WindowController* controller;
  if (!GetWindowFromWindowID(this, window_id, &controller))
    return false;

#if defined(OS_WIN)
  // Silently ignore changes on the window for metro mode.
  if (win8::IsSingleWindowMetroMode()) {
    SetResult(controller->CreateWindowValue());
    return true;
  }
#endif

  ui::WindowShowState show_state = ui::SHOW_STATE_DEFAULT;  // No change.
  std::string state_str;
  if (update_props->HasKey(keys::kShowStateKey)) {
    EXTENSION_FUNCTION_VALIDATE(update_props->GetString(keys::kShowStateKey,
                                                        &state_str));
    if (state_str == keys::kShowStateValueNormal) {
      show_state = ui::SHOW_STATE_NORMAL;
    } else if (state_str == keys::kShowStateValueMinimized) {
      show_state = ui::SHOW_STATE_MINIMIZED;
    } else if (state_str == keys::kShowStateValueMaximized) {
      show_state = ui::SHOW_STATE_MAXIMIZED;
    } else if (state_str == keys::kShowStateValueFullscreen) {
      show_state = ui::SHOW_STATE_FULLSCREEN;
    } else {
      error_ = keys::kInvalidWindowStateError;
      return false;
    }
  }

  if (show_state != ui::SHOW_STATE_FULLSCREEN &&
      show_state != ui::SHOW_STATE_DEFAULT)
    controller->SetFullscreenMode(false, GetExtension()->url());

  switch (show_state) {
    case ui::SHOW_STATE_MINIMIZED:
      controller->window()->Minimize();
      break;
    case ui::SHOW_STATE_MAXIMIZED:
      controller->window()->Maximize();
      break;
    case ui::SHOW_STATE_FULLSCREEN:
      if (controller->window()->IsMinimized() ||
          controller->window()->IsMaximized())
        controller->window()->Restore();
      controller->SetFullscreenMode(true, GetExtension()->url());
      break;
    case ui::SHOW_STATE_NORMAL:
      controller->window()->Restore();
      break;
    default:
      break;
  }

  gfx::Rect bounds;
  if (controller->window()->IsMinimized())
    bounds = controller->window()->GetRestoredBounds();
  else
    bounds = controller->window()->GetBounds();
  bool set_bounds = false;

  // Any part of the bounds can optionally be set by the caller.
  int bounds_val;
  if (update_props->HasKey(keys::kLeftKey)) {
    EXTENSION_FUNCTION_VALIDATE(update_props->GetInteger(
        keys::kLeftKey,
        &bounds_val));
    bounds.set_x(bounds_val);
    set_bounds = true;
  }

  if (update_props->HasKey(keys::kTopKey)) {
    EXTENSION_FUNCTION_VALIDATE(update_props->GetInteger(
        keys::kTopKey,
        &bounds_val));
    bounds.set_y(bounds_val);
    set_bounds = true;
  }

  if (update_props->HasKey(keys::kWidthKey)) {
    EXTENSION_FUNCTION_VALIDATE(update_props->GetInteger(
        keys::kWidthKey,
        &bounds_val));
    bounds.set_width(bounds_val);
    set_bounds = true;
  }

  if (update_props->HasKey(keys::kHeightKey)) {
    EXTENSION_FUNCTION_VALIDATE(update_props->GetInteger(
        keys::kHeightKey,
        &bounds_val));
    bounds.set_height(bounds_val);
    set_bounds = true;
  }

  if (set_bounds) {
    if (show_state == ui::SHOW_STATE_MINIMIZED ||
        show_state == ui::SHOW_STATE_MAXIMIZED ||
        show_state == ui::SHOW_STATE_FULLSCREEN) {
      error_ = keys::kInvalidWindowStateError;
      return false;
    }
    // TODO(varkha): Updating bounds during a drag can cause problems and a more
    // general solution is needed. See http://crbug.com/251813 .
    controller->window()->SetBounds(bounds);
  }

  bool active_val = false;
  if (update_props->HasKey(keys::kFocusedKey)) {
    EXTENSION_FUNCTION_VALIDATE(update_props->GetBoolean(
        keys::kFocusedKey, &active_val));
    if (active_val) {
      if (show_state == ui::SHOW_STATE_MINIMIZED) {
        error_ = keys::kInvalidWindowStateError;
        return false;
      }
      controller->window()->Activate();
    } else {
      if (show_state == ui::SHOW_STATE_MAXIMIZED ||
          show_state == ui::SHOW_STATE_FULLSCREEN) {
        error_ = keys::kInvalidWindowStateError;
        return false;
      }
      controller->window()->Deactivate();
    }
  }

  bool draw_attention = false;
  if (update_props->HasKey(keys::kDrawAttentionKey)) {
    EXTENSION_FUNCTION_VALIDATE(update_props->GetBoolean(
        keys::kDrawAttentionKey, &draw_attention));
    controller->window()->FlashFrame(draw_attention);
  }

  SetResult(controller->CreateWindowValue());

  return true;
}

bool WindowsRemoveFunction::RunImpl() {
  int window_id = -1;
  EXTENSION_FUNCTION_VALIDATE(args_->GetInteger(0, &window_id));

  WindowController* controller;
  if (!GetWindowFromWindowID(this, window_id, &controller))
    return false;

#if defined(OS_WIN)
  // In Windows 8 metro mode, an existing Browser instance is reused for
  // hosting the extension tab. We should not be closing it as we don't own it.
  if (win8::IsSingleWindowMetroMode())
    return false;
#endif

  WindowController::Reason reason;
  if (!controller->CanClose(&reason)) {
    if (reason == WindowController::REASON_NOT_EDITABLE)
      error_ = keys::kTabStripNotEditableError;
    return false;
  }
  controller->window()->Close();
  return true;
}

// Tabs ------------------------------------------------------------------------

bool TabsGetSelectedFunction::RunImpl() {
  // windowId defaults to "current" window.
  int window_id = extension_misc::kCurrentWindowId;

  scoped_ptr<tabs::GetSelected::Params> params(
      tabs::GetSelected::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  if (params->window_id.get())
    window_id = *params->window_id;

  Browser* browser = NULL;
  if (!GetBrowserFromWindowID(this, window_id, &browser))
    return false;

  TabStripModel* tab_strip = browser->tab_strip_model();
  WebContents* contents = tab_strip->GetActiveWebContents();
  if (!contents) {
    error_ = keys::kNoSelectedTabError;
    return false;
  }
  SetResult(ExtensionTabUtil::CreateTabValue(contents,
                                             tab_strip,
                                             tab_strip->active_index(),
                                             GetExtension()));
  return true;
}

bool TabsGetAllInWindowFunction::RunImpl() {
  scoped_ptr<tabs::GetAllInWindow::Params> params(
      tabs::GetAllInWindow::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  // windowId defaults to "current" window.
  int window_id = extension_misc::kCurrentWindowId;
  if (params->window_id.get())
    window_id = *params->window_id;

  Browser* browser = NULL;
  if (!GetBrowserFromWindowID(this, window_id, &browser))
    return false;

  SetResult(ExtensionTabUtil::CreateTabList(browser, GetExtension()));

  return true;
}

bool TabsQueryFunction::RunImpl() {
  scoped_ptr<tabs::Query::Params> params(tabs::Query::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  bool loading_status_set = params->query_info.status !=
      tabs::Query::Params::QueryInfo::STATUS_NONE;
  bool loading = params->query_info.status ==
      tabs::Query::Params::QueryInfo::STATUS_LOADING;

  // It is o.k. to use URLPattern::SCHEME_ALL here because this function does
  // not grant access to the content of the tabs, only to seeing their URLs and
  // meta data.
  URLPattern url_pattern(URLPattern::SCHEME_ALL, "<all_urls>");
  if (params->query_info.url.get())
    url_pattern = URLPattern(URLPattern::SCHEME_ALL, *params->query_info.url);

  std::string title;
  if (params->query_info.title.get())
    title = *params->query_info.title;

  int window_id = extension_misc::kUnknownWindowId;
  if (params->query_info.window_id.get())
    window_id = *params->query_info.window_id;

  int index = -1;
  if (params->query_info.index.get())
    index = *params->query_info.index;

  std::string window_type;
  if (params->query_info.window_type !=
      tabs::Query::Params::QueryInfo::WINDOW_TYPE_NONE) {
    window_type = tabs::Query::Params::QueryInfo::ToString(
        params->query_info.window_type);
  }

  base::ListValue* result = new base::ListValue();
  for (chrome::BrowserIterator it; !it.done(); it.Next()) {
    Browser* browser = *it;
    if (!profile()->IsSameProfile(browser->profile()))
      continue;

    if (!browser->window())
      continue;

    if (!include_incognito() && profile() != browser->profile())
      continue;

    if (window_id >= 0 && window_id != ExtensionTabUtil::GetWindowId(browser))
      continue;

    if (window_id == extension_misc::kCurrentWindowId &&
        browser != GetCurrentBrowser()) {
      continue;
    }

    if (!MatchesBool(params->query_info.current_window.get(),
                     browser == GetCurrentBrowser())) {
      continue;
    }

    if (!MatchesBool(params->query_info.last_focused_window.get(),
                     browser->window()->IsActive())) {
      continue;
    }

    if (!window_type.empty() &&
        window_type !=
            browser->extension_window_controller()->GetWindowTypeText()) {
      continue;
    }

    TabStripModel* tab_strip = browser->tab_strip_model();
    for (int i = 0; i < tab_strip->count(); ++i) {
      const WebContents* web_contents = tab_strip->GetWebContentsAt(i);

      if (index > -1 && i != index)
        continue;

      if (!MatchesBool(params->query_info.highlighted.get(),
                       tab_strip->IsTabSelected(i))) {
        continue;
      }

      if (!MatchesBool(params->query_info.active.get(),
                       i == tab_strip->active_index())) {
        continue;
      }

      if (!MatchesBool(params->query_info.pinned.get(),
                       tab_strip->IsTabPinned(i))) {
        continue;
      }

      if (!title.empty() && !MatchPattern(web_contents->GetTitle(),
                                          UTF8ToUTF16(title)))
        continue;

      if (!url_pattern.MatchesURL(web_contents->GetURL()))
        continue;

      if (loading_status_set && loading != web_contents->IsLoading())
        continue;

      result->Append(ExtensionTabUtil::CreateTabValue(
          web_contents, tab_strip, i, GetExtension()));
    }
  }

  SetResult(result);
  return true;
}

bool TabsCreateFunction::RunImpl() {
  scoped_ptr<tabs::Create::Params> params(tabs::Create::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  // windowId defaults to "current" window.
  int window_id = extension_misc::kCurrentWindowId;
  if (params->create_properties.window_id.get())
    window_id = *params->create_properties.window_id;

  Browser* browser = NULL;
  if (!GetBrowserFromWindowID(this, window_id, &browser))
    return false;

  // Ensure the selected browser is tabbed.
  if (!browser->is_type_tabbed() && browser->IsAttemptingToCloseBrowser())
    browser = chrome::FindTabbedBrowser(profile(), include_incognito(),
                                        browser->host_desktop_type());

  if (!browser || !browser->window())
    return false;

  // TODO(jstritar): Add a constant, chrome.tabs.TAB_ID_ACTIVE, that
  // represents the active tab.
  WebContents* opener = NULL;
  if (params->create_properties.opener_tab_id.get()) {
    int opener_id = *params->create_properties.opener_tab_id;

    if (!ExtensionTabUtil::GetTabById(
            opener_id, profile(), include_incognito(),
            NULL, NULL, &opener, NULL)) {
      return false;
    }
  }

  // TODO(rafaelw): handle setting remaining tab properties:
  // -title
  // -favIconUrl

  std::string url_string;
  GURL url;
  if (params->create_properties.url.get()) {
    url_string = *params->create_properties.url;
    url = ExtensionTabUtil::ResolvePossiblyRelativeURL(url_string,
                                                       GetExtension());
    if (!url.is_valid()) {
      error_ = ErrorUtils::FormatErrorMessage(keys::kInvalidUrlError,
                                                       url_string);
      return false;
    }
  }

  // Don't let extensions crash the browser or renderers.
  if (ExtensionTabUtil::IsCrashURL(url)) {
    error_ = keys::kNoCrashBrowserError;
    return false;
  }

  // Default to foreground for the new tab. The presence of 'selected' property
  // will override this default. This property is deprecated ('active' should
  // be used instead).
  bool active = true;
  if (params->create_properties.selected.get())
    active = *params->create_properties.selected;

  // The 'active' property has replaced the 'selected' property.
  if (params->create_properties.active.get())
    active = *params->create_properties.active;

  // Default to not pinning the tab. Setting the 'pinned' property to true
  // will override this default.
  bool pinned = false;
  if (params->create_properties.pinned.get())
    pinned = *params->create_properties.pinned;

  // We can't load extension URLs into incognito windows unless the extension
  // uses split mode. Special case to fall back to a tabbed window.
  if (url.SchemeIs(kExtensionScheme) &&
      !IncognitoInfo::IsSplitMode(GetExtension()) &&
      browser->profile()->IsOffTheRecord()) {
    Profile* profile = browser->profile()->GetOriginalProfile();
    chrome::HostDesktopType desktop_type = browser->host_desktop_type();

    browser = chrome::FindTabbedBrowser(profile, false, desktop_type);
    if (!browser) {
      browser = new Browser(Browser::CreateParams(Browser::TYPE_TABBED,
                                                  profile, desktop_type));
      browser->window()->Show();
    }
  }

  // If index is specified, honor the value, but keep it bound to
  // -1 <= index <= tab_strip->count() where -1 invokes the default behavior.
  int index = -1;
  if (params->create_properties.index.get())
    index = *params->create_properties.index;

  TabStripModel* tab_strip = browser->tab_strip_model();

  index = std::min(std::max(index, -1), tab_strip->count());

  int add_types = active ? TabStripModel::ADD_ACTIVE :
                             TabStripModel::ADD_NONE;
  add_types |= TabStripModel::ADD_FORCE_INDEX;
  if (pinned)
    add_types |= TabStripModel::ADD_PINNED;
  chrome::NavigateParams navigate_params(
      browser, url, content::PAGE_TRANSITION_LINK);
  navigate_params.disposition =
      active ? NEW_FOREGROUND_TAB : NEW_BACKGROUND_TAB;
  navigate_params.tabstrip_index = index;
  navigate_params.tabstrip_add_types = add_types;
  chrome::Navigate(&navigate_params);

  // The tab may have been created in a different window, so make sure we look
  // at the right tab strip.
  tab_strip = navigate_params.browser->tab_strip_model();
  int new_index = tab_strip->GetIndexOfWebContents(
      navigate_params.target_contents);
  if (opener)
    tab_strip->SetOpenerOfWebContentsAt(new_index, opener);

  if (active)
    navigate_params.target_contents->GetView()->SetInitialFocus();

  // Return data about the newly created tab.
  if (has_callback()) {
    SetResult(ExtensionTabUtil::CreateTabValue(
        navigate_params.target_contents,
        tab_strip, new_index, GetExtension()));
  }

  return true;
}

bool TabsDuplicateFunction::RunImpl() {
  scoped_ptr<tabs::Duplicate::Params> params(
      tabs::Duplicate::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  int tab_id = params->tab_id;

  Browser* browser = NULL;
  TabStripModel* tab_strip = NULL;
  int tab_index = -1;
  if (!GetTabById(tab_id, profile(), include_incognito(),
                  &browser, &tab_strip, NULL, &tab_index, &error_)) {
    return false;
  }

  WebContents* new_contents = chrome::DuplicateTabAt(browser, tab_index);
  if (!has_callback())
    return true;

  // Duplicated tab may not be in the same window as the original, so find
  // the window and the tab.
  TabStripModel* new_tab_strip = NULL;
  int new_tab_index = -1;
  ExtensionTabUtil::GetTabStripModel(new_contents,
                                     &new_tab_strip,
                                     &new_tab_index);
  if (!new_tab_strip || new_tab_index == -1) {
    return false;
  }

  // Return data about the newly created tab.
  SetResult(ExtensionTabUtil::CreateTabValue(
      new_contents,
      new_tab_strip, new_tab_index, GetExtension()));

  return true;
}

bool TabsGetFunction::RunImpl() {
  scoped_ptr<tabs::Get::Params> params(tabs::Get::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  int tab_id = params->tab_id;

  TabStripModel* tab_strip = NULL;
  WebContents* contents = NULL;
  int tab_index = -1;
  if (!GetTabById(tab_id, profile(), include_incognito(),
                  NULL, &tab_strip, &contents, &tab_index, &error_))
    return false;

  SetResult(ExtensionTabUtil::CreateTabValue(contents,
                                             tab_strip,
                                             tab_index,
                                             GetExtension()));
  return true;
}

bool TabsGetCurrentFunction::RunImpl() {
  DCHECK(dispatcher());

  WebContents* contents = dispatcher()->delegate()->GetAssociatedWebContents();
  if (contents)
    SetResult(ExtensionTabUtil::CreateTabValue(contents, GetExtension()));

  return true;
}

bool TabsHighlightFunction::RunImpl() {
  scoped_ptr<tabs::Highlight::Params> params(
      tabs::Highlight::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  // Get the window id from the params; default to current window if omitted.
  int window_id = extension_misc::kCurrentWindowId;
  if (params->highlight_info.window_id.get())
    window_id = *params->highlight_info.window_id;

  Browser* browser = NULL;
  if (!GetBrowserFromWindowID(this, window_id, &browser))
    return false;

  TabStripModel* tabstrip = browser->tab_strip_model();
  ui::ListSelectionModel selection;
  int active_index = -1;

  if (params->highlight_info.tabs.as_integers) {
    std::vector<int>& tab_indices = *params->highlight_info.tabs.as_integers;
    // Create a new selection model as we read the list of tab indices.
    for (size_t i = 0; i < tab_indices.size(); ++i) {
      if (!HighlightTab(tabstrip, &selection, &active_index, tab_indices[i]))
        return false;
    }
  } else {
    EXTENSION_FUNCTION_VALIDATE(params->highlight_info.tabs.as_integer);
    if (!HighlightTab(tabstrip,
                      &selection,
                      &active_index,
                      *params->highlight_info.tabs.as_integer)) {
      return false;
    }
  }

  // Make sure they actually specified tabs to select.
  if (selection.empty()) {
    error_ = keys::kNoHighlightedTabError;
    return false;
  }

  selection.set_active(active_index);
  browser->tab_strip_model()->SetSelectionFromModel(selection);
  SetResult(
      browser->extension_window_controller()->CreateWindowValueWithTabs(
          GetExtension()));
  return true;
}

bool TabsHighlightFunction::HighlightTab(TabStripModel* tabstrip,
                                         ui::ListSelectionModel* selection,
                                         int *active_index,
                                         int index) {
  // Make sure the index is in range.
  if (!tabstrip->ContainsIndex(index)) {
    error_ = ErrorUtils::FormatErrorMessage(
        keys::kTabIndexNotFoundError, base::IntToString(index));
    return false;
  }

  // By default, we make the first tab in the list active.
  if (*active_index == -1)
    *active_index = index;

  selection->AddIndexToSelection(index);
  return true;
}

TabsUpdateFunction::TabsUpdateFunction() : web_contents_(NULL) {
}

bool TabsUpdateFunction::RunImpl() {
  scoped_ptr<tabs::Update::Params> params(tabs::Update::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  int tab_id = -1;
  WebContents* contents = NULL;
  if (!params->tab_id.get()) {
    Browser* browser = GetCurrentBrowser();
    if (!browser) {
      error_ = keys::kNoCurrentWindowError;
      return false;
    }
    contents = browser->tab_strip_model()->GetActiveWebContents();
    if (!contents) {
      error_ = keys::kNoSelectedTabError;
      return false;
    }
    tab_id = SessionID::IdForTab(contents);
  } else {
    tab_id = *params->tab_id;
  }

  int tab_index = -1;
  TabStripModel* tab_strip = NULL;
  if (!GetTabById(tab_id, profile(), include_incognito(),
                  NULL, &tab_strip, &contents, &tab_index, &error_)) {
    return false;
  }

  web_contents_ = contents;

  // TODO(rafaelw): handle setting remaining tab properties:
  // -title
  // -favIconUrl

  // Navigate the tab to a new location if the url is different.
  bool is_async = false;
  if (params->update_properties.url.get() &&
      !UpdateURL(*params->update_properties.url, tab_id, &is_async)) {
    return false;
  }

  bool active = false;
  // TODO(rafaelw): Setting |active| from js doesn't make much sense.
  // Move tab selection management up to window.
  if (params->update_properties.selected.get())
    active = *params->update_properties.selected;

  // The 'active' property has replaced 'selected'.
  if (params->update_properties.active.get())
    active = *params->update_properties.active;

  if (active) {
    if (tab_strip->active_index() != tab_index) {
      tab_strip->ActivateTabAt(tab_index, false);
      DCHECK_EQ(contents, tab_strip->GetActiveWebContents());
    }
  }

  if (params->update_properties.highlighted.get()) {
    bool highlighted = *params->update_properties.highlighted;
    if (highlighted != tab_strip->IsTabSelected(tab_index))
      tab_strip->ToggleSelectionAt(tab_index);
  }

  if (params->update_properties.pinned.get()) {
    bool pinned = *params->update_properties.pinned;
    tab_strip->SetTabPinned(tab_index, pinned);

    // Update the tab index because it may move when being pinned.
    tab_index = tab_strip->GetIndexOfWebContents(contents);
  }

  if (params->update_properties.opener_tab_id.get()) {
    int opener_id = *params->update_properties.opener_tab_id;

    WebContents* opener_contents = NULL;
    if (!ExtensionTabUtil::GetTabById(
            opener_id, profile(), include_incognito(),
            NULL, NULL, &opener_contents, NULL))
      return false;

    tab_strip->SetOpenerOfWebContentsAt(tab_index, opener_contents);
  }

  if (!is_async) {
    PopulateResult();
    SendResponse(true);
  }
  return true;
}

bool TabsUpdateFunction::UpdateURL(const std::string &url_string,
                                   int tab_id,
                                   bool* is_async) {
  GURL url = ExtensionTabUtil::ResolvePossiblyRelativeURL(
      url_string, GetExtension());

  if (!url.is_valid()) {
    error_ = ErrorUtils::FormatErrorMessage(
        keys::kInvalidUrlError, url_string);
    return false;
  }

  // Don't let the extension crash the browser or renderers.
  if (ExtensionTabUtil::IsCrashURL(url)) {
    error_ = keys::kNoCrashBrowserError;
    return false;
  }

  // JavaScript URLs can do the same kinds of things as cross-origin XHR, so
  // we need to check host permissions before allowing them.
  if (url.SchemeIs(chrome::kJavaScriptScheme)) {
    content::RenderProcessHost* process = web_contents_->GetRenderProcessHost();
    if (!PermissionsData::CanExecuteScriptOnPage(
            GetExtension(),
            web_contents_->GetURL(),
            web_contents_->GetURL(),
            tab_id,
            NULL,
            process ? process->GetID() : -1,
            &error_)) {
      return false;
    }

    TabHelper::FromWebContents(web_contents_)->
        script_executor()->ExecuteScript(
            extension_id(),
            ScriptExecutor::JAVASCRIPT,
            url.path(),
            ScriptExecutor::TOP_FRAME,
            UserScript::DOCUMENT_IDLE,
            ScriptExecutor::MAIN_WORLD,
            false /* is_web_view */,
            base::Bind(&TabsUpdateFunction::OnExecuteCodeFinished, this));

    *is_async = true;
    return true;
  }

  web_contents_->GetController().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_LINK, std::string());

  // The URL of a tab contents never actually changes to a JavaScript URL, so
  // this check only makes sense in other cases.
  if (!url.SchemeIs(chrome::kJavaScriptScheme))
    DCHECK_EQ(url.spec(), web_contents_->GetURL().spec());

  return true;
}

void TabsUpdateFunction::PopulateResult() {
  if (!has_callback())
    return;

  SetResult(ExtensionTabUtil::CreateTabValue(web_contents_, GetExtension()));
}

void TabsUpdateFunction::OnExecuteCodeFinished(
    const std::string& error,
    int32 on_page_id,
    const GURL& url,
    const base::ListValue& script_result) {
  if (error.empty())
    PopulateResult();
  else
    error_ = error;
  SendResponse(error.empty());
}

bool TabsMoveFunction::RunImpl() {
  scoped_ptr<tabs::Move::Params> params(tabs::Move::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  int new_index = params->move_properties.index;
  int* window_id = params->move_properties.window_id.get();
  base::ListValue tab_values;

  size_t num_tabs = 0;
  if (params->tab_ids.as_integers) {
    std::vector<int>& tab_ids = *params->tab_ids.as_integers;
    num_tabs = tab_ids.size();
    for (size_t i = 0; i < tab_ids.size(); ++i) {
      if (!MoveTab(tab_ids[i], &new_index, i, &tab_values, window_id))
        return false;
    }
  } else {
    EXTENSION_FUNCTION_VALIDATE(params->tab_ids.as_integer);
    num_tabs = 1;
    if (!MoveTab(*params->tab_ids.as_integer,
                 &new_index,
                 0,
                 &tab_values,
                 window_id)) {
      return false;
    }
  }

  if (!has_callback())
    return true;

  // Only return the results as an array if there are multiple tabs.
  if (num_tabs > 1) {
    SetResult(tab_values.DeepCopy());
  } else {
    Value* value = NULL;
    CHECK(tab_values.Get(0, &value));
    SetResult(value->DeepCopy());
  }
  return true;
}

bool TabsMoveFunction::MoveTab(int tab_id,
                               int *new_index,
                               int iteration,
                               base::ListValue* tab_values,
                               int* window_id) {
  Browser* source_browser = NULL;
  TabStripModel* source_tab_strip = NULL;
  WebContents* contents = NULL;
  int tab_index = -1;
  if (!GetTabById(tab_id, profile(), include_incognito(),
                  &source_browser, &source_tab_strip, &contents,
                  &tab_index, &error_)) {
    return false;
  }

  // Don't let the extension move the tab if the user is dragging tabs.
  if (!source_browser->window()->IsTabStripEditable()) {
    error_ = keys::kTabStripNotEditableError;
    return false;
  }

  // Insert the tabs one after another.
  *new_index += iteration;

  if (window_id) {
    Browser* target_browser = NULL;

    if (!GetBrowserFromWindowID(this, *window_id, &target_browser))
      return false;

    if (!target_browser->window()->IsTabStripEditable()) {
      error_ = keys::kTabStripNotEditableError;
      return false;
    }

    if (!target_browser->is_type_tabbed()) {
      error_ = keys::kCanOnlyMoveTabsWithinNormalWindowsError;
      return false;
    }

    if (target_browser->profile() != source_browser->profile()) {
      error_ = keys::kCanOnlyMoveTabsWithinSameProfileError;
      return false;
    }

    // If windowId is different from the current window, move between windows.
    if (ExtensionTabUtil::GetWindowId(target_browser) !=
        ExtensionTabUtil::GetWindowId(source_browser)) {
      TabStripModel* target_tab_strip = target_browser->tab_strip_model();
      WebContents* web_contents =
          source_tab_strip->DetachWebContentsAt(tab_index);
      if (!web_contents) {
        error_ = ErrorUtils::FormatErrorMessage(
            keys::kTabNotFoundError, base::IntToString(tab_id));
        return false;
      }

      // Clamp move location to the last position.
      // This is ">" because it can append to a new index position.
      // -1 means set the move location to the last position.
      if (*new_index > target_tab_strip->count() || *new_index < 0)
        *new_index = target_tab_strip->count();

      target_tab_strip->InsertWebContentsAt(
          *new_index, web_contents, TabStripModel::ADD_NONE);

      if (has_callback()) {
        tab_values->Append(ExtensionTabUtil::CreateTabValue(
            web_contents,
            target_tab_strip,
            *new_index,
            GetExtension()));
      }

      return true;
    }
  }

  // Perform a simple within-window move.
  // Clamp move location to the last position.
  // This is ">=" because the move must be to an existing location.
  // -1 means set the move location to the last position.
  if (*new_index >= source_tab_strip->count() || *new_index < 0)
    *new_index = source_tab_strip->count() - 1;

  if (*new_index != tab_index)
    source_tab_strip->MoveWebContentsAt(tab_index, *new_index, false);

  if (has_callback()) {
    tab_values->Append(ExtensionTabUtil::CreateTabValue(
        contents, source_tab_strip, *new_index, GetExtension()));
  }

  return true;
}

bool TabsReloadFunction::RunImpl() {
  scoped_ptr<tabs::Reload::Params> params(
      tabs::Reload::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  bool bypass_cache = false;
  if (params->reload_properties.get() &&
      params->reload_properties->bypass_cache.get()) {
    bypass_cache = *params->reload_properties->bypass_cache;
  }

  content::WebContents* web_contents = NULL;

  // If |tab_id| is specified, look for it. Otherwise default to selected tab
  // in the current window.
  if (!params->tab_id.get()) {
    Browser* browser = GetCurrentBrowser();
    if (!browser) {
      error_ = keys::kNoCurrentWindowError;
      return false;
    }

    if (!ExtensionTabUtil::GetDefaultTab(browser, &web_contents, NULL))
      return false;
  } else {
    int tab_id = *params->tab_id;

    Browser* browser = NULL;
    if (!GetTabById(tab_id, profile(), include_incognito(),
                    &browser, NULL, &web_contents, NULL, &error_))
    return false;
  }

  if (web_contents->ShowingInterstitialPage()) {
    // This does as same as Browser::ReloadInternal.
    NavigationEntry* entry = web_contents->GetController().GetActiveEntry();
    OpenURLParams params(entry->GetURL(), Referrer(), CURRENT_TAB,
                         content::PAGE_TRANSITION_RELOAD, false);
    GetCurrentBrowser()->OpenURL(params);
  } else if (bypass_cache) {
    web_contents->GetController().ReloadIgnoringCache(true);
  } else {
    web_contents->GetController().Reload(true);
  }

  return true;
}

bool TabsRemoveFunction::RunImpl() {
  scoped_ptr<tabs::Remove::Params> params(tabs::Remove::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  if (params->tab_ids.as_integers) {
    std::vector<int>& tab_ids = *params->tab_ids.as_integers;
    for (size_t i = 0; i < tab_ids.size(); ++i) {
      if (!RemoveTab(tab_ids[i]))
        return false;
    }
  } else {
    EXTENSION_FUNCTION_VALIDATE(params->tab_ids.as_integer);
    if (!RemoveTab(*params->tab_ids.as_integer.get()))
      return false;
  }
  return true;
}

bool TabsRemoveFunction::RemoveTab(int tab_id) {
  Browser* browser = NULL;
  WebContents* contents = NULL;
  if (!GetTabById(tab_id, profile(), include_incognito(),
                  &browser, NULL, &contents, NULL, &error_)) {
    return false;
  }

  // Don't let the extension remove a tab if the user is dragging tabs around.
  if (!browser->window()->IsTabStripEditable()) {
    error_ = keys::kTabStripNotEditableError;
    return false;
  }
  // There's a chance that the tab is being dragged, or we're in some other
  // nested event loop. This code path ensures that the tab is safely closed
  // under such circumstances, whereas |TabStripModel::CloseWebContentsAt()|
  // does not.
  contents->Close();
  return true;
}

bool TabsCaptureVisibleTabFunction::GetTabToCapture(
    WebContents** web_contents) {
  scoped_ptr<tabs::CaptureVisibleTab::Params> params(
      tabs::CaptureVisibleTab::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  Browser* browser = NULL;
  // windowId defaults to "current" window.
  int window_id = extension_misc::kCurrentWindowId;

  if (params->window_id.get())
    window_id = *params->window_id;

  if (!GetBrowserFromWindowID(this, window_id, &browser))
    return false;

  *web_contents = browser->tab_strip_model()->GetActiveWebContents();
  if (*web_contents == NULL) {
    error_ = keys::kInternalVisibleTabCaptureError;
    return false;
  }

  return true;
};

bool TabsCaptureVisibleTabFunction::RunImpl() {
  scoped_ptr<tabs::CaptureVisibleTab::Params> params(
      tabs::CaptureVisibleTab::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  PrefService* service = profile()->GetPrefs();
  if (service->GetBoolean(prefs::kDisableScreenshots)) {
    error_ = keys::kScreenshotsDisabled;
    return false;
  }

  WebContents* web_contents = NULL;
  if (!GetTabToCapture(&web_contents))
    return false;

  image_format_ = FormatEnum::FORMAT_JPEG;  // Default format is JPEG.
  image_quality_ = kDefaultQuality;  // Default quality setting.

  if (params->options.get()) {
    if (params->options->format != FormatEnum::FORMAT_NONE)
      image_format_ = params->options->format;

    if (params->options->quality.get())
      image_quality_ = *params->options->quality;
  }

  // Use the last committed URL rather than the active URL for permissions
  // checking, since the visible page won't be updated until it has been
  // committed. A canonical example of this is interstitials, which show the
  // URL of the new/loading page (active) but would capture the content of the
  // old page (last committed).
  //
  // TODO(creis): Use WebContents::GetLastCommittedURL instead.
  // http://crbug.com/237908.
  NavigationEntry* last_committed_entry =
      web_contents->GetController().GetLastCommittedEntry();
  GURL last_committed_url = last_committed_entry ?
      last_committed_entry->GetURL() : GURL();
  if (!PermissionsData::CanCaptureVisiblePage(
          GetExtension(),
          last_committed_url,
          SessionID::IdForTab(web_contents),
          &error_)) {
    return false;
  }

  RenderViewHost* render_view_host = web_contents->GetRenderViewHost();
  content::RenderWidgetHostView* view = render_view_host->GetView();
  if (!view) {
    error_ = keys::kInternalVisibleTabCaptureError;
    return false;
  }
  render_view_host->CopyFromBackingStore(
      gfx::Rect(),
      view->GetViewBounds().size(),
      base::Bind(&TabsCaptureVisibleTabFunction::CopyFromBackingStoreComplete,
                 this));
  return true;
}

void TabsCaptureVisibleTabFunction::CopyFromBackingStoreComplete(
    bool succeeded,
    const SkBitmap& bitmap) {
  if (succeeded) {
    VLOG(1) << "captureVisibleTab() got image from backing store.";
    SendResultFromBitmap(bitmap);
    return;
  }

  WebContents* web_contents = NULL;
  if (!GetTabToCapture(&web_contents)) {
    SendInternalError();
    return;
  }

  // Ask the renderer for a snapshot of the tab.
  content::RenderWidgetHost* render_widget_host =
      web_contents->GetRenderViewHost();
  if (!render_widget_host) {
    SendInternalError();
    return;
  }

  render_widget_host->GetSnapshotFromRenderer(
      gfx::Rect(),
      base::Bind(
          &TabsCaptureVisibleTabFunction::GetSnapshotFromRendererComplete,
          this));
}

// If a backing store was not available in
// TabsCaptureVisibleTabFunction::RunImpl, than the renderer was asked for a
// snapshot.
void TabsCaptureVisibleTabFunction::GetSnapshotFromRendererComplete(
    bool succeeded,
    const SkBitmap& bitmap) {
  if (!succeeded) {
    SendInternalError();
  } else {
    VLOG(1) << "captureVisibleTab() got image from renderer.";
    SendResultFromBitmap(bitmap);
  }
}

void TabsCaptureVisibleTabFunction::SendInternalError() {
  error_ = keys::kInternalVisibleTabCaptureError;
  SendResponse(false);
}

// Turn a bitmap of the screen into an image, set that image as the result,
// and call SendResponse().
void TabsCaptureVisibleTabFunction::SendResultFromBitmap(
    const SkBitmap& screen_capture) {
  std::vector<unsigned char> data;
  SkAutoLockPixels screen_capture_lock(screen_capture);
  bool encoded = false;
  std::string mime_type;
  switch (image_format_) {
    case FormatEnum::FORMAT_JPEG:
      encoded = gfx::JPEGCodec::Encode(
          reinterpret_cast<unsigned char*>(screen_capture.getAddr32(0, 0)),
          gfx::JPEGCodec::FORMAT_SkBitmap,
          screen_capture.width(),
          screen_capture.height(),
          static_cast<int>(screen_capture.rowBytes()),
          image_quality_,
          &data);
      mime_type = keys::kMimeTypeJpeg;
      break;
    case FormatEnum::FORMAT_PNG:
      encoded = gfx::PNGCodec::EncodeBGRASkBitmap(
          screen_capture,
          true,  // Discard transparency.
          &data);
      mime_type = keys::kMimeTypePng;
      break;
    default:
      NOTREACHED() << "Invalid image format.";
  }

  if (!encoded) {
    error_ = keys::kInternalVisibleTabCaptureError;
    SendResponse(false);
    return;
  }

  std::string base64_result;
  base::StringPiece stream_as_string(
      reinterpret_cast<const char*>(vector_as_array(&data)), data.size());

  base::Base64Encode(stream_as_string, &base64_result);
  base64_result.insert(0, base::StringPrintf("data:%s;base64,",
                                             mime_type.c_str()));
  SetResult(new StringValue(base64_result));
  SendResponse(true);
}

void TabsCaptureVisibleTabFunction::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      prefs::kDisableScreenshots,
      false,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

bool TabsDetectLanguageFunction::RunImpl() {
  scoped_ptr<tabs::DetectLanguage::Params> params(
      tabs::DetectLanguage::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  int tab_id = 0;
  Browser* browser = NULL;
  WebContents* contents = NULL;

  // If |tab_id| is specified, look for it. Otherwise default to selected tab
  // in the current window.
  if (params->tab_id.get()) {
    tab_id = *params->tab_id;
    if (!GetTabById(tab_id, profile(), include_incognito(),
                    &browser, NULL, &contents, NULL, &error_)) {
      return false;
    }
    if (!browser || !contents)
      return false;
  } else {
    browser = GetCurrentBrowser();
    if (!browser)
      return false;
    contents = browser->tab_strip_model()->GetActiveWebContents();
    if (!contents)
      return false;
  }

  if (contents->GetController().NeedsReload()) {
    // If the tab hasn't been loaded, don't wait for the tab to load.
    error_ = keys::kCannotDetermineLanguageOfUnloadedTab;
    return false;
  }

  AddRef();  // Balanced in GotLanguage().

  TranslateTabHelper* translate_tab_helper =
      TranslateTabHelper::FromWebContents(contents);
  if (!translate_tab_helper->language_state().original_language().empty()) {
    // Delay the callback invocation until after the current JS call has
    // returned.
    base::MessageLoop::current()->PostTask(FROM_HERE, base::Bind(
        &TabsDetectLanguageFunction::GotLanguage, this,
        translate_tab_helper->language_state().original_language()));
    return true;
  }
  // The tab contents does not know its language yet.  Let's wait until it
  // receives it, or until the tab is closed/navigates to some other page.
  registrar_.Add(this, chrome::NOTIFICATION_TAB_LANGUAGE_DETERMINED,
                 content::Source<WebContents>(contents));
  registrar_.Add(
      this, chrome::NOTIFICATION_TAB_CLOSING,
      content::Source<NavigationController>(&(contents->GetController())));
  registrar_.Add(
      this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
      content::Source<NavigationController>(&(contents->GetController())));
  return true;
}

void TabsDetectLanguageFunction::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  std::string language;
  if (type == chrome::NOTIFICATION_TAB_LANGUAGE_DETERMINED) {
    const LanguageDetectionDetails* lang_det_details =
        content::Details<const LanguageDetectionDetails>(details).ptr();
    language = lang_det_details->adopted_language;
  }

  registrar_.RemoveAll();

  // Call GotLanguage in all cases as we want to guarantee the callback is
  // called for every API call the extension made.
  GotLanguage(language);
}

void TabsDetectLanguageFunction::GotLanguage(const std::string& language) {
  SetResult(Value::CreateStringValue(language.c_str()));
  SendResponse(true);

  Release();  // Balanced in Run()
}

ExecuteCodeInTabFunction::ExecuteCodeInTabFunction()
    : execute_tab_id_(-1) {
}

ExecuteCodeInTabFunction::~ExecuteCodeInTabFunction() {}

bool ExecuteCodeInTabFunction::HasPermission() {
  if (Init() && PermissionsData::HasAPIPermissionForTab(
                    extension_.get(), execute_tab_id_, APIPermission::kTab)) {
    return true;
  }
  return ExtensionFunction::HasPermission();
}

bool ExecuteCodeInTabFunction::CanExecuteScriptOnPage() {
  content::WebContents* contents = NULL;

  // If |tab_id| is specified, look for the tab. Otherwise default to selected
  // tab in the current window.
  CHECK_GE(execute_tab_id_, 0);
  if (!GetTabById(execute_tab_id_, profile(), include_incognito(),
                  NULL, NULL, &contents, NULL, &error_)) {
    return false;
  }

  CHECK(contents);

  // NOTE: This can give the wrong answer due to race conditions, but it is OK,
  // we check again in the renderer.
  content::RenderProcessHost* process = contents->GetRenderProcessHost();
  if (!PermissionsData::CanExecuteScriptOnPage(
          GetExtension(),
          contents->GetURL(),
          contents->GetURL(),
          execute_tab_id_,
          NULL,
          process ? process->GetID() : -1,
          &error_)) {
    return false;
  }

  return true;
}

ScriptExecutor* ExecuteCodeInTabFunction::GetScriptExecutor() {
  Browser* browser = NULL;
  content::WebContents* contents = NULL;

  bool success = GetTabById(
      execute_tab_id_, profile(), include_incognito(), &browser, NULL,
      &contents, NULL, &error_) && contents && browser;

  if (!success)
    return NULL;

  return TabHelper::FromWebContents(contents)->script_executor();
}

bool ExecuteCodeInTabFunction::IsWebView() const {
  return false;
}

bool TabsExecuteScriptFunction::ShouldInsertCSS() const {
  return false;
}

void TabsExecuteScriptFunction::OnExecuteCodeFinished(
    const std::string& error,
    int32 on_page_id,
    const GURL& on_url,
    const base::ListValue& result) {
  if (error.empty())
    SetResult(result.DeepCopy());
  ExecuteCodeInTabFunction::OnExecuteCodeFinished(error, on_page_id, on_url,
                                                  result);
}

bool ExecuteCodeInTabFunction::Init() {
  if (details_.get())
    return true;

  // |tab_id| is optional so it's ok if it's not there.
  int tab_id = -1;
  if (args_->GetInteger(0, &tab_id))
    EXTENSION_FUNCTION_VALIDATE(tab_id >= 0);

  // |details| are not optional.
  base::DictionaryValue* details_value = NULL;
  if (!args_->GetDictionary(1, &details_value))
    return false;
  scoped_ptr<InjectDetails> details(new InjectDetails());
  if (!InjectDetails::Populate(*details_value, details.get()))
    return false;

  // If the tab ID wasn't given then it needs to be converted to the
  // currently active tab's ID.
  if (tab_id == -1) {
    Browser* browser = GetCurrentBrowser();
    if (!browser)
      return false;
    content::WebContents* web_contents = NULL;
    if (!ExtensionTabUtil::GetDefaultTab(browser, &web_contents, &tab_id))
      return false;
  }

  execute_tab_id_ = tab_id;
  details_ = details.Pass();
  return true;
}

bool TabsInsertCSSFunction::ShouldInsertCSS() const {
  return true;
}

}  // namespace extensions
