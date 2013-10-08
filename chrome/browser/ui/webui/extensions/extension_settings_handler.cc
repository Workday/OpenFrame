// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/extensions/extension_settings_handler.h"

#include "apps/app_load_service.h"
#include "apps/app_restore_service.h"
#include "apps/saved_files_service.h"
#include "apps/shell_window.h"
#include "apps/shell_window_registry.h"
#include "base/auto_reset.h"
#include "base/base64.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/extensions/api/extension_action/extension_action_api.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/extension_disabled_ui.h"
#include "chrome/browser/extensions/extension_error_reporter.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/extension_warning_set.h"
#include "chrome/browser/extensions/lazy_background_task_queue.h"
#include "chrome/browser/extensions/management_policy.h"
#include "chrome/browser/extensions/unpacked_installer.h"
#include "chrome/browser/extensions/updater/extension_updater.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/managed_mode/managed_user_service.h"
#include "chrome/browser/managed_mode/managed_user_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/background_contents.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/ui/webui/extensions/extension_basic_info.h"
#include "chrome/browser/ui/webui/extensions/extension_icon_source.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/background_info.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "chrome/common/extensions/extension_set.h"
#include "chrome/common/extensions/feature_switch.h"
#include "chrome/common/extensions/incognito_handler.h"
#include "chrome/common/extensions/manifest_url_handler.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "extensions/browser/view_type_utils.h"
#include "extensions/common/constants.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"


using content::RenderViewHost;
using content::WebContents;

namespace extensions {

ExtensionPage::ExtensionPage(const GURL& url,
                             int render_process_id,
                             int render_view_id,
                             bool incognito)
    : url(url),
      render_process_id(render_process_id),
      render_view_id(render_view_id),
      incognito(incognito) {
}

///////////////////////////////////////////////////////////////////////////////
//
// ExtensionSettingsHandler
//
///////////////////////////////////////////////////////////////////////////////

ExtensionSettingsHandler::ExtensionSettingsHandler()
    : extension_service_(NULL),
      management_policy_(NULL),
      ignore_notifications_(false),
      deleting_rvh_(NULL),
      registered_for_notifications_(false),
      rvh_created_callback_(
          base::Bind(&ExtensionSettingsHandler::RenderViewHostCreated,
                     base::Unretained(this))),
      warning_service_observer_(this) {
}

ExtensionSettingsHandler::~ExtensionSettingsHandler() {
  content::RenderViewHost::RemoveCreatedCallback(rvh_created_callback_);

  // There may be pending file dialogs, we need to tell them that we've gone
  // away so they don't try and call back to us.
  if (load_extension_dialog_.get())
    load_extension_dialog_->ListenerDestroyed();
}

ExtensionSettingsHandler::ExtensionSettingsHandler(ExtensionService* service,
                                                   ManagementPolicy* policy)
    : extension_service_(service),
      management_policy_(policy),
      ignore_notifications_(false),
      deleting_rvh_(NULL),
      registered_for_notifications_(false),
      warning_service_observer_(this) {
}

// static
void ExtensionSettingsHandler::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      prefs::kExtensionsUIDeveloperMode,
      false,
      user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
}

DictionaryValue* ExtensionSettingsHandler::CreateExtensionDetailValue(
    const Extension* extension,
    const std::vector<ExtensionPage>& pages,
    const ExtensionWarningService* warning_service) {
  DictionaryValue* extension_data = new DictionaryValue();
  bool enabled = extension_service_->IsExtensionEnabled(extension->id());
  GetExtensionBasicInfo(extension, enabled, extension_data);

  extension_data->SetBoolean(
      "userModifiable",
      management_policy_->UserMayModifySettings(extension, NULL));

  GURL icon =
      ExtensionIconSource::GetIconURL(extension,
                                      extension_misc::EXTENSION_ICON_MEDIUM,
                                      ExtensionIconSet::MATCH_BIGGER,
                                      !enabled, NULL);
  if (Manifest::IsUnpackedLocation(extension->location()))
    extension_data->SetString("path", extension->path().value());
  extension_data->SetString("icon", icon.spec());
  extension_data->SetBoolean("isUnpacked",
      Manifest::IsUnpackedLocation(extension->location()));
  extension_data->SetBoolean("terminated",
      extension_service_->terminated_extensions()->Contains(extension->id()));
  extension_data->SetBoolean("enabledIncognito",
      extension_service_->IsIncognitoEnabled(extension->id()));
  extension_data->SetBoolean("incognitoCanBeToggled",
                             extension->can_be_incognito_enabled() &&
                             !extension->force_incognito_enabled());
  extension_data->SetBoolean("wantsFileAccess", extension->wants_file_access());
  extension_data->SetBoolean("allowFileAccess",
                             extension_service_->AllowFileAccess(extension));
  extension_data->SetBoolean("allow_reload",
      Manifest::IsUnpackedLocation(extension->location()));
  extension_data->SetBoolean("is_hosted_app", extension->is_hosted_app());
  extension_data->SetBoolean("is_platform_app", extension->is_platform_app());
  extension_data->SetBoolean("homepageProvided",
      ManifestURL::GetHomepageURL(extension).is_valid());

  string16 location_text;
  if (extension->location() == Manifest::EXTERNAL_POLICY_DOWNLOAD) {
    location_text = l10n_util::GetStringUTF16(
        IDS_OPTIONS_INSTALL_LOCATION_ENTERPRISE);
  } else if (extension->location() == Manifest::INTERNAL &&
      !ManifestURL::UpdatesFromGallery(extension)) {
    location_text = l10n_util::GetStringUTF16(
        IDS_OPTIONS_INSTALL_LOCATION_UNKNOWN);
  } else if (extension->location() == Manifest::EXTERNAL_REGISTRY) {
    location_text = l10n_util::GetStringUTF16(
        IDS_OPTIONS_INSTALL_LOCATION_3RD_PARTY);
  }
  extension_data->SetString("locationText", location_text);

  // Determine the sort order: Extensions loaded through --load-extensions show
  // up at the top. Disabled extensions show up at the bottom.
  if (Manifest::IsUnpackedLocation(extension->location()))
    extension_data->SetInteger("order", 1);
  else
    extension_data->SetInteger("order", 2);

  if (!ExtensionActionAPI::GetBrowserActionVisibility(
          extension_service_->extension_prefs(), extension->id())) {
    extension_data->SetBoolean("enable_show_button", true);
  }

  // Add views
  ListValue* views = new ListValue;
  for (std::vector<ExtensionPage>::const_iterator iter = pages.begin();
       iter != pages.end(); ++iter) {
    DictionaryValue* view_value = new DictionaryValue;
    if (iter->url.scheme() == kExtensionScheme) {
      // No leading slash.
      view_value->SetString("path", iter->url.path().substr(1));
    } else {
      // For live pages, use the full URL.
      view_value->SetString("path", iter->url.spec());
    }
    view_value->SetInteger("renderViewId", iter->render_view_id);
    view_value->SetInteger("renderProcessId", iter->render_process_id);
    view_value->SetBoolean("incognito", iter->incognito);
    views->Append(view_value);
  }
  extension_data->Set("views", views);
  ExtensionActionManager* extension_action_manager =
      ExtensionActionManager::Get(extension_service_->profile());
  extension_data->SetBoolean(
      "hasPopupAction",
      extension_action_manager->GetBrowserAction(*extension) ||
      extension_action_manager->GetPageAction(*extension));

  // Add warnings.
  if (warning_service) {
    std::vector<std::string> warnings =
        warning_service->GetWarningMessagesForExtension(extension->id());

    if (!warnings.empty()) {
      ListValue* warnings_list = new ListValue;
      for (std::vector<std::string>::const_iterator iter = warnings.begin();
           iter != warnings.end(); ++iter) {
        warnings_list->Append(Value::CreateStringValue(*iter));
      }
      extension_data->Set("warnings", warnings_list);
    }
  }

  // Add install warnings (these are not the same as warnings!).
  if (Manifest::IsUnpackedLocation(extension->location())) {
    const std::vector<InstallWarning>& install_warnings =
        extension->install_warnings();
    if (!install_warnings.empty()) {
      scoped_ptr<ListValue> list(new ListValue());
      for (std::vector<InstallWarning>::const_iterator it =
               install_warnings.begin(); it != install_warnings.end(); ++it) {
        DictionaryValue* item = new DictionaryValue();
        item->SetBoolean("isHTML", it->format == InstallWarning::FORMAT_HTML);
        item->SetString("message", it->message);
        list->Append(item);
      }
      extension_data->Set("installWarnings", list.release());
    }
  }

  return extension_data;
}

void ExtensionSettingsHandler::GetLocalizedValues(
    content::WebUIDataSource* source) {
  source->AddString("extensionSettings",
      l10n_util::GetStringUTF16(IDS_MANAGE_EXTENSIONS_SETTING_WINDOWS_TITLE));

  source->AddString("extensionSettingsDeveloperMode",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_DEVELOPER_MODE_LINK));
  source->AddString("extensionSettingsNoExtensions",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_NONE_INSTALLED));
  source->AddString("extensionSettingsSuggestGallery",
      l10n_util::GetStringFUTF16(IDS_EXTENSIONS_NONE_INSTALLED_SUGGEST_GALLERY,
          ASCIIToUTF16(google_util::AppendGoogleLocaleParam(
              GURL(extension_urls::GetExtensionGalleryURL())).spec())));
  source->AddString("extensionSettingsGetMoreExtensions",
      l10n_util::GetStringUTF16(IDS_GET_MORE_EXTENSIONS));
  source->AddString("extensionSettingsGetMoreExtensionsUrl",
      ASCIIToUTF16(google_util::AppendGoogleLocaleParam(
          GURL(extension_urls::GetExtensionGalleryURL())).spec()));
  source->AddString("extensionSettingsExtensionId",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_ID));
  source->AddString("extensionSettingsExtensionPath",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_PATH));
  source->AddString("extensionSettingsInspectViews",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_INSPECT_VIEWS));
  source->AddString("extensionSettingsInstallWarnings",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_INSTALL_WARNINGS));
  source->AddString("viewIncognito",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_VIEW_INCOGNITO));
  source->AddString("viewInactive",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_VIEW_INACTIVE));
  source->AddString("extensionSettingsEnable",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_ENABLE));
  source->AddString("extensionSettingsEnabled",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_ENABLED));
  source->AddString("extensionSettingsRemove",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_REMOVE));
  source->AddString("extensionSettingsEnableIncognito",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_ENABLE_INCOGNITO));
  source->AddString("extensionSettingsAllowFileAccess",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_ALLOW_FILE_ACCESS));
  source->AddString("extensionSettingsIncognitoWarning",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_INCOGNITO_WARNING));
  source->AddString("extensionSettingsReloadTerminated",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_RELOAD_TERMINATED));
  source->AddString("extensionSettingsLaunch",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_LAUNCH));
  source->AddString("extensionSettingsRestart",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_RESTART));
  source->AddString("extensionSettingsReloadUnpacked",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_RELOAD_UNPACKED));
  source->AddString("extensionSettingsOptions",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_OPTIONS_LINK));
  source->AddString("extensionSettingsPermissions",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_PERMISSIONS_LINK));
  source->AddString("extensionSettingsVisitWebsite",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_VISIT_WEBSITE));
  source->AddString("extensionSettingsVisitWebStore",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_VISIT_WEBSTORE));
  source->AddString("extensionSettingsPolicyControlled",
     l10n_util::GetStringUTF16(IDS_EXTENSIONS_POLICY_CONTROLLED));
  source->AddString("extensionSettingsManagedMode",
     l10n_util::GetStringUTF16(IDS_EXTENSIONS_LOCKED_MANAGED_USER));
  source->AddString("extensionSettingsUseAppsDevTools",
     l10n_util::GetStringUTF16(IDS_EXTENSIONS_USE_APPS_DEV_TOOLS));
  source->AddString("extensionSettingsOpenAppsDevTools",
     l10n_util::GetStringUTF16(IDS_EXTENSIONS_OPEN_APPS_DEV_TOOLS));
  source->AddString("sideloadWipeoutUrl",
      chrome::kSideloadWipeoutHelpURL);
  source->AddString("sideloadWipoutLearnMore",
      l10n_util::GetStringUTF16(IDS_LEARN_MORE));
  source->AddString("extensionSettingsShowButton",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_SHOW_BUTTON));
  source->AddString("extensionSettingsLoadUnpackedButton",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_LOAD_UNPACKED_BUTTON));
  source->AddString("extensionSettingsPackButton",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_PACK_BUTTON));
  source->AddString("extensionSettingsCommandsLink",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_COMMANDS_CONFIGURE));
  source->AddString("extensionSettingsUpdateButton",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_UPDATE_BUTTON));
  source->AddString("extensionSettingsCrashMessage",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_CRASHED_EXTENSION));
  source->AddString("extensionSettingsInDevelopment",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_IN_DEVELOPMENT));
  source->AddString("extensionSettingsWarningsTitle",
      l10n_util::GetStringUTF16(IDS_EXTENSION_WARNINGS_TITLE));
  source->AddString("extensionSettingsShowDetails",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_SHOW_DETAILS));
  source->AddString("extensionSettingsHideDetails",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_HIDE_DETAILS));

  // TODO(estade): comb through the above strings to find ones no longer used in
  // uber extensions.
  source->AddString("extensionUninstall",
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_UNINSTALL));
}

void ExtensionSettingsHandler::RenderViewHostCreated(
    content::RenderViewHost* render_view_host) {
  Profile* source_profile = Profile::FromBrowserContext(
      render_view_host->GetSiteInstance()->GetBrowserContext());
  if (!Profile::FromWebUI(web_ui())->IsSameProfile(source_profile))
    return;
  MaybeUpdateAfterNotification();
}

void ExtensionSettingsHandler::RenderViewDeleted(
    content::RenderViewHost* render_view_host) {
  deleting_rvh_ = render_view_host;
  Profile* source_profile = Profile::FromBrowserContext(
      render_view_host->GetSiteInstance()->GetBrowserContext());
  if (!Profile::FromWebUI(web_ui())->IsSameProfile(source_profile))
    return;
  MaybeUpdateAfterNotification();
}

void ExtensionSettingsHandler::NavigateToPendingEntry(const GURL& url,
    content::NavigationController::ReloadType reload_type) {
  if (reload_type != content::NavigationController::NO_RELOAD)
    ReloadUnpackedExtensions();
}

void ExtensionSettingsHandler::RegisterMessages() {
  // Don't override an |extension_service_| or |management_policy_| injected
  // for testing.
  if (!extension_service_) {
    extension_service_ = Profile::FromWebUI(web_ui())->GetOriginalProfile()->
        GetExtensionService();
  }
  if (!management_policy_) {
    management_policy_ = ExtensionSystem::Get(
        extension_service_->profile())->management_policy();
  }

  web_ui()->RegisterMessageCallback("extensionSettingsRequestExtensionsData",
      base::Bind(&ExtensionSettingsHandler::HandleRequestExtensionsData,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsToggleDeveloperMode",
      base::Bind(&ExtensionSettingsHandler::HandleToggleDeveloperMode,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsInspect",
      base::Bind(&ExtensionSettingsHandler::HandleInspectMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsLaunch",
      base::Bind(&ExtensionSettingsHandler::HandleLaunchMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsRestart",
      base::Bind(&ExtensionSettingsHandler::HandleRestartMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsReload",
      base::Bind(&ExtensionSettingsHandler::HandleReloadMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsEnable",
      base::Bind(&ExtensionSettingsHandler::HandleEnableMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsEnableIncognito",
      base::Bind(&ExtensionSettingsHandler::HandleEnableIncognitoMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsAllowFileAccess",
      base::Bind(&ExtensionSettingsHandler::HandleAllowFileAccessMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsUninstall",
      base::Bind(&ExtensionSettingsHandler::HandleUninstallMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsOptions",
      base::Bind(&ExtensionSettingsHandler::HandleOptionsMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsPermissions",
      base::Bind(&ExtensionSettingsHandler::HandlePermissionsMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsShowButton",
      base::Bind(&ExtensionSettingsHandler::HandleShowButtonMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsAutoupdate",
      base::Bind(&ExtensionSettingsHandler::HandleAutoUpdateMessage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("extensionSettingsLoadUnpackedExtension",
      base::Bind(&ExtensionSettingsHandler::HandleLoadUnpackedExtensionMessage,
                 base::Unretained(this)));
}

void ExtensionSettingsHandler::FileSelected(const base::FilePath& path,
                                            int index,
                                            void* params) {
  last_unpacked_directory_ = base::FilePath(path);
  UnpackedInstaller::Create(extension_service_)->Load(path);
}

void ExtensionSettingsHandler::MultiFilesSelected(
    const std::vector<base::FilePath>& files, void* params) {
  NOTREACHED();
}

void ExtensionSettingsHandler::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  Profile* profile = Profile::FromWebUI(web_ui());
  Profile* source_profile = NULL;
  switch (type) {
    // We listen for notifications that will result in the page being
    // repopulated with data twice for the same event in certain cases.
    // For instance, EXTENSION_LOADED & EXTENSION_HOST_CREATED because
    // we don't know about the views for an extension at EXTENSION_LOADED, but
    // if we only listen to EXTENSION_HOST_CREATED, we'll miss extensions
    // that don't have a process at startup.
    //
    // Doing it this way gets everything but causes the page to be rendered
    // more than we need. It doesn't seem to result in any noticeable flicker.
    case chrome::NOTIFICATION_BACKGROUND_CONTENTS_DELETED:
      deleting_rvh_ = content::Details<BackgroundContents>(details)->
          web_contents()->GetRenderViewHost();
      // Fall through.
    case chrome::NOTIFICATION_BACKGROUND_CONTENTS_NAVIGATED:
    case chrome::NOTIFICATION_EXTENSION_HOST_CREATED:
      source_profile = content::Source<Profile>(source).ptr();
      if (!profile->IsSameProfile(source_profile))
          return;
      MaybeUpdateAfterNotification();
      break;
    case chrome::NOTIFICATION_EXTENSION_LOADED:
    case chrome::NOTIFICATION_EXTENSION_UNLOADED:
    case chrome::NOTIFICATION_EXTENSION_UNINSTALLED:
    case chrome::NOTIFICATION_EXTENSION_UPDATE_DISABLED:
    case chrome::NOTIFICATION_EXTENSION_BROWSER_ACTION_VISIBILITY_CHANGED:
      MaybeUpdateAfterNotification();
      break;
    default:
      NOTREACHED();
  }
}

void ExtensionSettingsHandler::ExtensionUninstallAccepted() {
  DCHECK(!extension_id_prompting_.empty());

  bool was_terminated = false;

  // The extension can be uninstalled in another window while the UI was
  // showing. Do nothing in that case.
  const Extension* extension =
      extension_service_->GetExtensionById(extension_id_prompting_, true);
  if (!extension) {
    extension = extension_service_->GetTerminatedExtension(
        extension_id_prompting_);
    was_terminated = true;
  }
  if (!extension)
    return;

  extension_service_->UninstallExtension(extension_id_prompting_,
                                         false,  // External uninstall.
                                         NULL);  // Error.
  extension_id_prompting_ = "";

  // There will be no EXTENSION_UNLOADED notification for terminated
  // extensions as they were already unloaded.
  if (was_terminated)
    HandleRequestExtensionsData(NULL);
}

void ExtensionSettingsHandler::ExtensionUninstallCanceled() {
  extension_id_prompting_ = "";
}

void ExtensionSettingsHandler::ExtensionWarningsChanged() {
  MaybeUpdateAfterNotification();
}

// This is called when the user clicks "Revoke File Access."
void ExtensionSettingsHandler::InstallUIProceed() {
  Profile* profile = Profile::FromWebUI(web_ui());
  apps::SavedFilesService::Get(profile)->ClearQueue(
      extension_service_->GetExtensionById(extension_id_prompting_, true));
  if (apps::AppRestoreService::Get(profile)->
          IsAppRestorable(extension_id_prompting_)) {
    apps::AppLoadService::Get(profile)->RestartApplication(
        extension_id_prompting_);
  }
  extension_id_prompting_.clear();
}

void ExtensionSettingsHandler::InstallUIAbort(bool user_initiated) {
  extension_id_prompting_.clear();
}

void ExtensionSettingsHandler::ReloadUnpackedExtensions() {
  const ExtensionSet* extensions = extension_service_->extensions();
  std::vector<const Extension*> unpacked_extensions;
  for (ExtensionSet::const_iterator extension = extensions->begin();
       extension != extensions->end(); ++extension) {
    if (Manifest::IsUnpackedLocation((*extension)->location()))
      unpacked_extensions.push_back(extension->get());
  }

  for (std::vector<const Extension*>::iterator iter =
       unpacked_extensions.begin(); iter != unpacked_extensions.end(); ++iter) {
    extension_service_->ReloadExtension((*iter)->id());
  }
}

void ExtensionSettingsHandler::HandleRequestExtensionsData(
    const ListValue* args) {
  DictionaryValue results;

  Profile* profile = Profile::FromWebUI(web_ui());

  // Add the extensions to the results structure.
  ListValue* extensions_list = new ListValue();

  ExtensionWarningService* warnings =
      ExtensionSystem::Get(profile)->warning_service();

  const ExtensionSet* extensions = extension_service_->extensions();
  for (ExtensionSet::const_iterator extension = extensions->begin();
       extension != extensions->end(); ++extension) {
    if ((*extension)->ShouldDisplayInExtensionSettings()) {
      extensions_list->Append(CreateExtensionDetailValue(
          extension->get(),
          GetInspectablePagesForExtension(extension->get(), true),
          warnings));
    }
  }
  extensions = extension_service_->disabled_extensions();
  for (ExtensionSet::const_iterator extension = extensions->begin();
       extension != extensions->end(); ++extension) {
    if ((*extension)->ShouldDisplayInExtensionSettings()) {
      extensions_list->Append(CreateExtensionDetailValue(
          extension->get(),
          GetInspectablePagesForExtension(extension->get(), false),
          warnings));
    }
  }
  extensions = extension_service_->terminated_extensions();
  std::vector<ExtensionPage> empty_pages;
  for (ExtensionSet::const_iterator extension = extensions->begin();
       extension != extensions->end(); ++extension) {
    if ((*extension)->ShouldDisplayInExtensionSettings()) {
      extensions_list->Append(CreateExtensionDetailValue(
          extension->get(),
          empty_pages,  // Terminated process has no active pages.
          warnings));
    }
  }
  results.Set("extensions", extensions_list);

  bool is_managed = profile->IsManaged();
  bool developer_mode =
      !is_managed &&
      profile->GetPrefs()->GetBoolean(prefs::kExtensionsUIDeveloperMode);
  results.SetBoolean("profileIsManaged", is_managed);
  results.SetBoolean("developerMode", developer_mode);
  results.SetBoolean(
      "appsDevToolsEnabled",
      CommandLine::ForCurrentProcess()->HasSwitch(switches::kAppsDevtool));

  bool load_unpacked_disabled =
      extension_service_->extension_prefs()->ExtensionsBlacklistedByDefault();
  results.SetBoolean("loadUnpackedDisabled", load_unpacked_disabled);

  web_ui()->CallJavascriptFunction("ExtensionSettings.returnExtensionsData",
                                   results);

  MaybeRegisterForNotifications();
}

void ExtensionSettingsHandler::HandleToggleDeveloperMode(
    const ListValue* args) {
  Profile* profile = Profile::FromWebUI(web_ui());
  if (profile->IsManaged())
    return;

  bool developer_mode =
      profile->GetPrefs()->GetBoolean(prefs::kExtensionsUIDeveloperMode);
  profile->GetPrefs()->SetBoolean(
      prefs::kExtensionsUIDeveloperMode, !developer_mode);
}

void ExtensionSettingsHandler::HandleInspectMessage(const ListValue* args) {
  std::string extension_id;
  std::string render_process_id_str;
  std::string render_view_id_str;
  int render_process_id;
  int render_view_id;
  bool incognito;
  CHECK_EQ(4U, args->GetSize());
  CHECK(args->GetString(0, &extension_id));
  CHECK(args->GetString(1, &render_process_id_str));
  CHECK(args->GetString(2, &render_view_id_str));
  CHECK(args->GetBoolean(3, &incognito));
  CHECK(base::StringToInt(render_process_id_str, &render_process_id));
  CHECK(base::StringToInt(render_view_id_str, &render_view_id));

  if (render_process_id == -1) {
    // This message is for a lazy background page. Start the page if necessary.
    const Extension* extension =
        extension_service_->extensions()->GetByID(extension_id);
    DCHECK(extension);

    ExtensionService* service = extension_service_;
    if (incognito)
      service = ExtensionSystem::Get(extension_service_->
          profile()->GetOffTheRecordProfile())->extension_service();

    service->InspectBackgroundPage(extension);
    return;
  }

  RenderViewHost* host = RenderViewHost::FromID(render_process_id,
                                                render_view_id);
  if (!host) {
    // This can happen if the host has gone away since the page was displayed.
    return;
  }

  DevToolsWindow::OpenDevToolsWindow(host);
}

void ExtensionSettingsHandler::HandleLaunchMessage(const ListValue* args) {
  CHECK_EQ(1U, args->GetSize());
  std::string extension_id;
  CHECK(args->GetString(0, &extension_id));
  const Extension* extension =
      extension_service_->GetExtensionById(extension_id, false);
  chrome::OpenApplication(chrome::AppLaunchParams(extension_service_->profile(),
                                                  extension,
                                                  extension_misc::LAUNCH_WINDOW,
                                                  NEW_WINDOW));
}

void ExtensionSettingsHandler::HandleRestartMessage(const ListValue* args) {
  CHECK_EQ(1U, args->GetSize());
  std::string extension_id;
  CHECK(args->GetString(0, &extension_id));
  apps::AppLoadService::Get(extension_service_->profile())->RestartApplication(
      extension_id);
}

void ExtensionSettingsHandler::HandleReloadMessage(const ListValue* args) {
  std::string extension_id = UTF16ToUTF8(ExtractStringValue(args));
  CHECK(!extension_id.empty());
  extension_service_->ReloadExtension(extension_id);
}

void ExtensionSettingsHandler::HandleEnableMessage(const ListValue* args) {
  CHECK_EQ(2U, args->GetSize());
  std::string extension_id, enable_str;
  CHECK(args->GetString(0, &extension_id));
  CHECK(args->GetString(1, &enable_str));

  const Extension* extension =
      extension_service_->GetInstalledExtension(extension_id);
  if (!extension ||
      !management_policy_->UserMayModifySettings(extension, NULL)) {
    LOG(ERROR) << "Attempt to enable an extension that is non-usermanagable was"
               << "made. Extension id: " << extension->id();
    return;
  }

  if (enable_str == "true") {
    ExtensionPrefs* prefs = extension_service_->extension_prefs();
    if (prefs->DidExtensionEscalatePermissions(extension_id)) {
      ShowExtensionDisabledDialog(
          extension_service_, web_ui()->GetWebContents(), extension);
    } else if ((prefs->GetDisableReasons(extension_id) &
                   Extension::DISABLE_UNSUPPORTED_REQUIREMENT) &&
               !requirements_checker_.get()) {
      // Recheck the requirements.
      scoped_refptr<const Extension> extension =
          extension_service_->GetExtensionById(extension_id,
                                               true /* include disabled */);
      requirements_checker_.reset(new RequirementsChecker);
      requirements_checker_->Check(
          extension,
          base::Bind(&ExtensionSettingsHandler::OnRequirementsChecked,
                     AsWeakPtr(), extension_id));
    } else {
      extension_service_->EnableExtension(extension_id);

      // Make sure any browser action contained within it is not hidden.
      ExtensionActionAPI::SetBrowserActionVisibility(
          prefs, extension->id(), true);
    }
  } else {
    extension_service_->DisableExtension(
        extension_id, Extension::DISABLE_USER_ACTION);
  }
}

void ExtensionSettingsHandler::HandleEnableIncognitoMessage(
    const ListValue* args) {
  CHECK_EQ(2U, args->GetSize());
  std::string extension_id, enable_str;
  CHECK(args->GetString(0, &extension_id));
  CHECK(args->GetString(1, &enable_str));
  const Extension* extension =
      extension_service_->GetInstalledExtension(extension_id);
  if (!extension)
    return;

  // Flipping the incognito bit will generate unload/load notifications for the
  // extension, but we don't want to reload the page, because a) we've already
  // updated the UI to reflect the change, and b) we want the yellow warning
  // text to stay until the user has left the page.
  //
  // TODO(aa): This creates crappiness in some cases. For example, in a main
  // window, when toggling this, the browser action will flicker because it gets
  // unloaded, then reloaded. It would be better to have a dedicated
  // notification for this case.
  //
  // Bug: http://crbug.com/41384
  base::AutoReset<bool> auto_reset_ignore_notifications(
      &ignore_notifications_, true);
  extension_service_->SetIsIncognitoEnabled(extension->id(),
                                            enable_str == "true");
}

void ExtensionSettingsHandler::HandleAllowFileAccessMessage(
    const ListValue* args) {
  CHECK_EQ(2U, args->GetSize());
  std::string extension_id, allow_str;
  CHECK(args->GetString(0, &extension_id));
  CHECK(args->GetString(1, &allow_str));
  const Extension* extension =
      extension_service_->GetInstalledExtension(extension_id);
  if (!extension)
    return;

  if (!management_policy_->UserMayModifySettings(extension, NULL)) {
    LOG(ERROR) << "Attempt to change allow file access of an extension that is "
               << "non-usermanagable was made. Extension id : "
               << extension->id();
    return;
  }

  extension_service_->SetAllowFileAccess(extension, allow_str == "true");
}

void ExtensionSettingsHandler::HandleUninstallMessage(const ListValue* args) {
  CHECK_EQ(1U, args->GetSize());
  std::string extension_id;
  CHECK(args->GetString(0, &extension_id));
  const Extension* extension =
      extension_service_->GetInstalledExtension(extension_id);
  if (!extension)
    return;

  if (!management_policy_->UserMayModifySettings(extension, NULL)) {
    LOG(ERROR) << "Attempt to uninstall an extension that is non-usermanagable "
               << "was made. Extension id : " << extension->id();
    return;
  }

  if (!extension_id_prompting_.empty())
    return;  // Only one prompt at a time.

  extension_id_prompting_ = extension_id;

  GetExtensionUninstallDialog()->ConfirmUninstall(extension);
}

void ExtensionSettingsHandler::HandleOptionsMessage(const ListValue* args) {
  const Extension* extension = GetActiveExtension(args);
  if (!extension || ManifestURL::GetOptionsPage(extension).is_empty())
    return;
  ExtensionTabUtil::OpenOptionsPage(extension,
      chrome::FindBrowserWithWebContents(web_ui()->GetWebContents()));
}

void ExtensionSettingsHandler::HandlePermissionsMessage(const ListValue* args) {
  std::string extension_id(UTF16ToUTF8(ExtractStringValue(args)));
  CHECK(!extension_id.empty());
  const Extension* extension =
      extension_service_->GetExtensionById(extension_id, true);
  if (!extension)
    return;

  if (!extension_id_prompting_.empty())
    return;  // Only one prompt at a time.

  extension_id_prompting_ = extension->id();
  prompt_.reset(new ExtensionInstallPrompt(web_contents()));
  std::vector<base::FilePath> retained_file_paths;
  if (extension->HasAPIPermission(APIPermission::kFileSystem)) {
    std::vector<apps::SavedFileEntry> retained_file_entries =
        apps::SavedFilesService::Get(Profile::FromWebUI(
            web_ui()))->GetAllFileEntries(extension_id_prompting_);
    for (size_t i = 0; i < retained_file_entries.size(); ++i) {
      retained_file_paths.push_back(retained_file_entries[i].path);
    }
  }
  prompt_->ReviewPermissions(this, extension, retained_file_paths);
}

void ExtensionSettingsHandler::HandleShowButtonMessage(const ListValue* args) {
  const Extension* extension = GetActiveExtension(args);
  if (!extension)
    return;
  ExtensionActionAPI::SetBrowserActionVisibility(
      extension_service_->extension_prefs(), extension->id(), true);
}

void ExtensionSettingsHandler::HandleAutoUpdateMessage(const ListValue* args) {
  ExtensionUpdater* updater = extension_service_->updater();
  if (updater) {
    ExtensionUpdater::CheckParams params;
    params.install_immediately = true;
    updater->CheckNow(params);
  }
}

void ExtensionSettingsHandler::HandleLoadUnpackedExtensionMessage(
    const ListValue* args) {
  DCHECK(args->empty());

  string16 select_title =
      l10n_util::GetStringUTF16(IDS_EXTENSION_LOAD_FROM_DIRECTORY);

  const int kFileTypeIndex = 0;  // No file type information to index.
  const ui::SelectFileDialog::Type kSelectType =
      ui::SelectFileDialog::SELECT_FOLDER;
  load_extension_dialog_ = ui::SelectFileDialog::Create(
      this, new ChromeSelectFilePolicy(web_ui()->GetWebContents()));
  load_extension_dialog_->SelectFile(
      kSelectType,
      select_title,
      last_unpacked_directory_,
      NULL,
      kFileTypeIndex,
      base::FilePath::StringType(),
      web_ui()->GetWebContents()->GetView()->GetTopLevelNativeWindow(),
      NULL);

  content::RecordComputedAction("Options_LoadUnpackedExtension");
}

void ExtensionSettingsHandler::ShowAlert(const std::string& message) {
  ListValue arguments;
  arguments.Append(Value::CreateStringValue(message));
  web_ui()->CallJavascriptFunction("alert", arguments);
}

const Extension* ExtensionSettingsHandler::GetActiveExtension(
    const ListValue* args) {
  std::string extension_id = UTF16ToUTF8(ExtractStringValue(args));
  CHECK(!extension_id.empty());
  return extension_service_->GetExtensionById(extension_id, false);
}

void ExtensionSettingsHandler::MaybeUpdateAfterNotification() {
  WebContents* contents = web_ui()->GetWebContents();
  if (!ignore_notifications_ && contents && contents->GetRenderViewHost())
    HandleRequestExtensionsData(NULL);
  deleting_rvh_ = NULL;
}

void ExtensionSettingsHandler::MaybeRegisterForNotifications() {
  if (registered_for_notifications_)
    return;

  registered_for_notifications_  = true;
  Profile* profile = Profile::FromWebUI(web_ui());

  // Register for notifications that we need to reload the page.
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_LOADED,
                 content::Source<Profile>(profile));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::Source<Profile>(profile));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNINSTALLED,
                 content::Source<Profile>(profile));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UPDATE_DISABLED,
                 content::Source<Profile>(profile));
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_HOST_CREATED,
                 content::NotificationService::AllBrowserContextsAndSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_BACKGROUND_CONTENTS_NAVIGATED,
                 content::NotificationService::AllBrowserContextsAndSources());
  registrar_.Add(this,
                 chrome::NOTIFICATION_BACKGROUND_CONTENTS_DELETED,
                 content::NotificationService::AllBrowserContextsAndSources());
  registrar_.Add(
      this,
      chrome::NOTIFICATION_EXTENSION_BROWSER_ACTION_VISIBILITY_CHANGED,
      content::Source<ExtensionPrefs>(
          profile->GetExtensionService()->extension_prefs()));

  content::RenderViewHost::AddCreatedCallback(rvh_created_callback_);

  content::WebContentsObserver::Observe(web_ui()->GetWebContents());

  warning_service_observer_.Add(
      ExtensionSystem::Get(profile)->warning_service());

  base::Closure callback = base::Bind(
      &ExtensionSettingsHandler::MaybeUpdateAfterNotification,
      base::Unretained(this));

  pref_registrar_.Init(profile->GetPrefs());
  pref_registrar_.Add(prefs::kExtensionInstallDenyList, callback);
}

std::vector<ExtensionPage>
ExtensionSettingsHandler::GetInspectablePagesForExtension(
    const Extension* extension, bool extension_is_enabled) {
  std::vector<ExtensionPage> result;

  // Get the extension process's active views.
  ExtensionProcessManager* process_manager =
      ExtensionSystem::Get(extension_service_->profile())->process_manager();
  GetInspectablePagesForExtensionProcess(
      process_manager->GetRenderViewHostsForExtension(extension->id()),
      &result);

  // Get shell window views
  GetShellWindowPagesForExtensionProfile(extension,
      extension_service_->profile(), &result);

  // Include a link to start the lazy background page, if applicable.
  if (BackgroundInfo::HasLazyBackgroundPage(extension) &&
      extension_is_enabled &&
      !process_manager->GetBackgroundHostForExtension(extension->id())) {
    result.push_back(ExtensionPage(
        BackgroundInfo::GetBackgroundURL(extension), -1, -1, false));
  }

  // Repeat for the incognito process, if applicable. Don't try to get
  // shell windows for incognito processes.
  if (extension_service_->profile()->HasOffTheRecordProfile() &&
      IncognitoInfo::IsSplitMode(extension)) {
    ExtensionProcessManager* process_manager =
        ExtensionSystem::Get(extension_service_->profile()->
            GetOffTheRecordProfile())->process_manager();
    GetInspectablePagesForExtensionProcess(
        process_manager->GetRenderViewHostsForExtension(extension->id()),
        &result);

    if (BackgroundInfo::HasLazyBackgroundPage(extension) &&
        extension_is_enabled &&
        !process_manager->GetBackgroundHostForExtension(extension->id())) {
      result.push_back(ExtensionPage(
          BackgroundInfo::GetBackgroundURL(extension), -1, -1, true));
    }
  }

  return result;
}

void ExtensionSettingsHandler::GetInspectablePagesForExtensionProcess(
    const std::set<RenderViewHost*>& views,
    std::vector<ExtensionPage>* result) {
  for (std::set<RenderViewHost*>::const_iterator iter = views.begin();
       iter != views.end(); ++iter) {
    RenderViewHost* host = *iter;
    WebContents* web_contents = WebContents::FromRenderViewHost(host);
    ViewType host_type = GetViewType(web_contents);
    if (host == deleting_rvh_ ||
        VIEW_TYPE_EXTENSION_POPUP == host_type ||
        VIEW_TYPE_EXTENSION_DIALOG == host_type)
      continue;

    GURL url = web_contents->GetURL();
    content::RenderProcessHost* process = host->GetProcess();
    result->push_back(
        ExtensionPage(url, process->GetID(), host->GetRoutingID(),
                      process->GetBrowserContext()->IsOffTheRecord()));
  }
}

void ExtensionSettingsHandler::GetShellWindowPagesForExtensionProfile(
    const Extension* extension,
    Profile* profile,
    std::vector<ExtensionPage>* result) {
  apps::ShellWindowRegistry* registry = apps::ShellWindowRegistry::Get(profile);
  if (!registry) return;

  const apps::ShellWindowRegistry::ShellWindowList windows =
      registry->GetShellWindowsForApp(extension->id());

  for (apps::ShellWindowRegistry::const_iterator it = windows.begin();
       it != windows.end(); ++it) {
    WebContents* web_contents = (*it)->web_contents();
    RenderViewHost* host = web_contents->GetRenderViewHost();
    content::RenderProcessHost* process = host->GetProcess();

    result->push_back(
        ExtensionPage(web_contents->GetURL(), process->GetID(),
                      host->GetRoutingID(),
                      process->GetBrowserContext()->IsOffTheRecord()));
  }
}

ExtensionUninstallDialog*
ExtensionSettingsHandler::GetExtensionUninstallDialog() {
#if !defined(OS_ANDROID)
  if (!extension_uninstall_dialog_.get()) {
    Browser* browser = chrome::FindBrowserWithWebContents(
        web_ui()->GetWebContents());
    extension_uninstall_dialog_.reset(
        ExtensionUninstallDialog::Create(extension_service_->profile(),
                                         browser, this));
  }
  return extension_uninstall_dialog_.get();
#else
  return NULL;
#endif  // !defined(OS_ANDROID)
}

void ExtensionSettingsHandler::OnRequirementsChecked(
    std::string extension_id,
    std::vector<std::string> requirement_errors) {
  if (requirement_errors.empty()) {
    extension_service_->EnableExtension(extension_id);
  } else {
    ExtensionErrorReporter::GetInstance()->ReportError(
        UTF8ToUTF16(JoinString(requirement_errors, ' ')),
        true /* be noisy */);
  }
  requirements_checker_.reset();
}

}  // namespace extensions
