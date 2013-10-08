// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/management/management_api.h"

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/json/json_writer.h"
#include "base/lazy_instance.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/management/management_api_constants.h"
#include "chrome/browser/extensions/event_names.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_uninstall_dialog.h"
#include "chrome/browser/extensions/management_policy.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/ui/webui/extensions/extension_icon_source.h"
#include "chrome/common/chrome_utility_messages.h"
#include "chrome/common/extensions/api/management.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_icon_set.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "chrome/common/extensions/manifest_handlers/icons_handler.h"
#include "chrome/common/extensions/manifest_handlers/offline_enabled_info.h"
#include "chrome/common/extensions/manifest_url_handler.h"
#include "chrome/common/extensions/permissions/permission_set.h"
#include "chrome/common/extensions/permissions/permissions_data.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/utility_process_host.h"
#include "content/public/browser/utility_process_host_client.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/url_pattern.h"

#if !defined(OS_ANDROID)
#include "chrome/browser/ui/webui/ntp/core_app_launcher_handler.h"
#endif

using base::IntToString;
using content::BrowserThread;
using content::UtilityProcessHost;
using content::UtilityProcessHostClient;

namespace keys = extension_management_api_constants;

namespace extensions {

namespace events = event_names;
namespace management = api::management;

namespace {

typedef std::vector<linked_ptr<management::ExtensionInfo> > ExtensionInfoList;
typedef std::vector<linked_ptr<management::IconInfo> > IconInfoList;

enum AutoConfirmForTest {
  DO_NOT_SKIP = 0,
  PROCEED,
  ABORT
};

AutoConfirmForTest auto_confirm_for_test = DO_NOT_SKIP;

std::vector<std::string> CreateWarningsList(const Extension* extension) {
  std::vector<std::string> warnings_list;
  PermissionMessages warnings =
      PermissionsData::GetPermissionMessages(extension);
  for (PermissionMessages::const_iterator iter = warnings.begin();
       iter != warnings.end(); ++iter) {
    warnings_list.push_back(UTF16ToUTF8(iter->message()));
  }

  return warnings_list;
}

scoped_ptr<management::ExtensionInfo> CreateExtensionInfo(
    const Extension& extension,
    ExtensionSystem* system) {
  scoped_ptr<management::ExtensionInfo> info(new management::ExtensionInfo());
  ExtensionService* service = system->extension_service();

  info->id = extension.id();
  info->name = extension.name();
  info->enabled = service->IsExtensionEnabled(info->id);
  info->offline_enabled = OfflineEnabledInfo::IsOfflineEnabled(&extension);
  info->version = extension.VersionString();
  info->description = extension.description();
  info->options_url = ManifestURL::GetOptionsPage(&extension).spec();
  info->homepage_url.reset(new std::string(
      ManifestURL::GetHomepageURL(&extension).spec()));
  info->may_disable = system->management_policy()->
      UserMayModifySettings(&extension, NULL);
  info->is_app = extension.is_app();
  if (info->is_app) {
    if (extension.is_legacy_packaged_app())
      info->type = management::ExtensionInfo::TYPE_LEGACY_PACKAGED_APP;
    else if (extension.is_hosted_app())
      info->type = management::ExtensionInfo::TYPE_HOSTED_APP;
    else
      info->type = management::ExtensionInfo::TYPE_PACKAGED_APP;
  } else if (extension.is_theme()) {
    info->type = management::ExtensionInfo::TYPE_THEME;
  } else {
    info->type = management::ExtensionInfo::TYPE_EXTENSION;
  }

  if (info->enabled) {
    info->disabled_reason = management::ExtensionInfo::DISABLED_REASON_NONE;
  } else {
    ExtensionPrefs* prefs = service->extension_prefs();
    if (prefs->DidExtensionEscalatePermissions(extension.id())) {
      info->disabled_reason =
          management::ExtensionInfo::DISABLED_REASON_PERMISSIONS_INCREASE;
    } else {
      info->disabled_reason =
          management::ExtensionInfo::DISABLED_REASON_UNKNOWN;
    }
  }

  if (!ManifestURL::GetUpdateURL(&extension).is_empty()) {
    info->update_url.reset(new std::string(
        ManifestURL::GetUpdateURL(&extension).spec()));
  }

  if (extension.is_app()) {
    info->app_launch_url.reset(new std::string(
        AppLaunchInfo::GetFullLaunchURL(&extension).spec()));
  }

  const ExtensionIconSet::IconMap& icons =
      IconsInfo::GetIcons(&extension).map();
  if (!icons.empty()) {
    info->icons.reset(new IconInfoList());
    ExtensionIconSet::IconMap::const_iterator icon_iter;
    for (icon_iter = icons.begin(); icon_iter != icons.end(); ++icon_iter) {
      management::IconInfo* icon_info = new management::IconInfo();
      icon_info->size = icon_iter->first;
      GURL url = ExtensionIconSource::GetIconURL(
          &extension, icon_info->size, ExtensionIconSet::MATCH_EXACTLY, false,
          NULL);
      icon_info->url = url.spec();
      info->icons->push_back(make_linked_ptr<management::IconInfo>(icon_info));
    }
  }

  const std::set<std::string> perms =
      extension.GetActivePermissions()->GetAPIsAsStrings();
  if (!perms.empty()) {
    std::set<std::string>::const_iterator perms_iter;
    for (perms_iter = perms.begin(); perms_iter != perms.end(); ++perms_iter)
      info->permissions.push_back(*perms_iter);
  }

  if (!extension.is_hosted_app()) {
    // Skip host permissions for hosted apps.
    const URLPatternSet host_perms =
        extension.GetActivePermissions()->explicit_hosts();
    if (!host_perms.is_empty()) {
      for (URLPatternSet::const_iterator iter = host_perms.begin();
           iter != host_perms.end(); ++iter) {
        info->host_permissions.push_back(iter->GetAsString());
      }
    }
  }

  switch (extension.location()) {
    case Manifest::INTERNAL:
      info->install_type = management::ExtensionInfo::INSTALL_TYPE_NORMAL;
      break;
    case Manifest::UNPACKED:
    case Manifest::COMMAND_LINE:
      info->install_type = management::ExtensionInfo::INSTALL_TYPE_DEVELOPMENT;
      break;
    case Manifest::EXTERNAL_PREF:
    case Manifest::EXTERNAL_REGISTRY:
    case Manifest::EXTERNAL_PREF_DOWNLOAD:
      info->install_type = management::ExtensionInfo::INSTALL_TYPE_SIDELOAD;
      break;
    case Manifest::EXTERNAL_POLICY_DOWNLOAD:
      info->install_type = management::ExtensionInfo::INSTALL_TYPE_ADMIN;
      break;
    default:
      info->install_type = management::ExtensionInfo::INSTALL_TYPE_OTHER;
      break;
  }

  return info.Pass();
}

void AddExtensionInfo(const ExtensionSet& extensions,
                            ExtensionSystem* system,
                            ExtensionInfoList* extension_list) {
  for (ExtensionSet::const_iterator iter = extensions.begin();
       iter != extensions.end(); ++iter) {
    const Extension& extension = *iter->get();

    if (extension.location() == Manifest::COMPONENT)
      continue;  // Skip built-in extensions.

    extension_list->push_back(make_linked_ptr<management::ExtensionInfo>(
        CreateExtensionInfo(extension, system).release()));
  }
}

} // namespace

ExtensionService* ManagementFunction::service() {
  return profile()->GetExtensionService();
}

ExtensionService* AsyncManagementFunction::service() {
  return profile()->GetExtensionService();
}

bool ManagementGetAllFunction::RunImpl() {
  ExtensionInfoList extensions;
  ExtensionSystem* system = ExtensionSystem::Get(profile());

  AddExtensionInfo(*service()->extensions(), system, &extensions);
  AddExtensionInfo(*service()->disabled_extensions(), system, &extensions);
  AddExtensionInfo(*service()->terminated_extensions(), system, &extensions);

  results_ = management::GetAll::Results::Create(extensions);
  return true;
}

bool ManagementGetFunction::RunImpl() {
  scoped_ptr<management::Get::Params> params(
      management::Get::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  const Extension* extension = service()->GetExtensionById(params->id, true);
  if (!extension) {
    error_ = ErrorUtils::FormatErrorMessage(keys::kNoExtensionError,
                                                     params->id);
    return false;
  }

  scoped_ptr<management::ExtensionInfo> info = CreateExtensionInfo(
      *extension, ExtensionSystem::Get(profile()));
  results_ = management::Get::Results::Create(*info);

  return true;
}

bool ManagementGetPermissionWarningsByIdFunction::RunImpl() {
  scoped_ptr<management::GetPermissionWarningsById::Params> params(
      management::GetPermissionWarningsById::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  const Extension* extension = service()->GetExtensionById(params->id, true);
  if (!extension) {
    error_ = ErrorUtils::FormatErrorMessage(keys::kNoExtensionError,
                                                     params->id);
    return false;
  }

  std::vector<std::string> warnings = CreateWarningsList(extension);
  results_ = management::GetPermissionWarningsById::Results::Create(warnings);
  return true;
}

namespace {

// This class helps ManagementGetPermissionWarningsByManifestFunction manage
// sending manifest JSON strings to the utility process for parsing.
class SafeManifestJSONParser : public UtilityProcessHostClient {
 public:
  SafeManifestJSONParser(
      ManagementGetPermissionWarningsByManifestFunction* client,
      const std::string& manifest)
      : client_(client),
        manifest_(manifest) {}

  void Start() {
    CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    BrowserThread::PostTask(
        BrowserThread::IO,
        FROM_HERE,
        base::Bind(&SafeManifestJSONParser::StartWorkOnIOThread, this));
  }

  void StartWorkOnIOThread() {
    CHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    UtilityProcessHost* host = UtilityProcessHost::Create(
        this, base::MessageLoopProxy::current().get());
    host->EnableZygote();
    host->Send(new ChromeUtilityMsg_ParseJSON(manifest_));
  }

  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE {
    bool handled = true;
    IPC_BEGIN_MESSAGE_MAP(SafeManifestJSONParser, message)
      IPC_MESSAGE_HANDLER(ChromeUtilityHostMsg_ParseJSON_Succeeded,
                          OnJSONParseSucceeded)
      IPC_MESSAGE_HANDLER(ChromeUtilityHostMsg_ParseJSON_Failed,
                          OnJSONParseFailed)
      IPC_MESSAGE_UNHANDLED(handled = false)
    IPC_END_MESSAGE_MAP()
    return handled;
  }

  void OnJSONParseSucceeded(const base::ListValue& wrapper) {
    CHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    const Value* value = NULL;
    CHECK(wrapper.Get(0, &value));
    if (value->IsType(Value::TYPE_DICTIONARY))
      parsed_manifest_.reset(
          static_cast<const base::DictionaryValue*>(value)->DeepCopy());
    else
      error_ = keys::kManifestParseError;

    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&SafeManifestJSONParser::ReportResultFromUIThread, this));
  }

  void OnJSONParseFailed(const std::string& error) {
    CHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
    error_ = error;
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&SafeManifestJSONParser::ReportResultFromUIThread, this));
  }

  void ReportResultFromUIThread() {
    CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
    if (error_.empty() && parsed_manifest_.get())
      client_->OnParseSuccess(parsed_manifest_.release());
    else
      client_->OnParseFailure(error_);
  }

 private:
  virtual ~SafeManifestJSONParser() {}

  // The client who we'll report results back to.
  ManagementGetPermissionWarningsByManifestFunction* client_;

  // Data to parse.
  std::string manifest_;

  // Results of parsing.
  scoped_ptr<base::DictionaryValue> parsed_manifest_;

  std::string error_;
};

}  // namespace

bool ManagementGetPermissionWarningsByManifestFunction::RunImpl() {
  scoped_ptr<management::GetPermissionWarningsByManifest::Params> params(
      management::GetPermissionWarningsByManifest::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  scoped_refptr<SafeManifestJSONParser> parser =
      new SafeManifestJSONParser(this, params->manifest_str);
  parser->Start();

  // Matched with a Release() in OnParseSuccess/Failure().
  AddRef();

  // Response is sent async in OnParseSuccess/Failure().
  return true;
}

void ManagementGetPermissionWarningsByManifestFunction::OnParseSuccess(
    base::DictionaryValue* parsed_manifest) {
  CHECK(parsed_manifest);

  scoped_refptr<Extension> extension = Extension::Create(
      base::FilePath(), Manifest::INVALID_LOCATION, *parsed_manifest,
      Extension::NO_FLAGS, &error_);
  if (!extension.get()) {
    OnParseFailure(keys::kExtensionCreateError);
    return;
  }

  std::vector<std::string> warnings = CreateWarningsList(extension.get());
  results_ =
      management::GetPermissionWarningsByManifest::Results::Create(warnings);
  SendResponse(true);

  // Matched with AddRef() in RunImpl().
  Release();
}

void ManagementGetPermissionWarningsByManifestFunction::OnParseFailure(
    const std::string& error) {
  error_ = error;
  SendResponse(false);

  // Matched with AddRef() in RunImpl().
  Release();
}

bool ManagementLaunchAppFunction::RunImpl() {
  scoped_ptr<management::LaunchApp::Params> params(
      management::LaunchApp::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  const Extension* extension = service()->GetExtensionById(params->id, true);
  if (!extension) {
    error_ = ErrorUtils::FormatErrorMessage(keys::kNoExtensionError,
                                                     params->id);
    return false;
  }
  if (!extension->is_app()) {
    error_ = ErrorUtils::FormatErrorMessage(keys::kNotAnAppError,
                                                     params->id);
    return false;
  }

  // Look at prefs to find the right launch container.
  // |default_pref_value| is set to LAUNCH_DEFAULT so that if
  // the user has not set a preference, we open the app in a tab.
  extension_misc::LaunchContainer launch_container =
      service()->extension_prefs()->GetLaunchContainer(
          extension, ExtensionPrefs::LAUNCH_DEFAULT);
  chrome::OpenApplication(chrome::AppLaunchParams(profile(), extension,
                                                  launch_container,
                                                  NEW_FOREGROUND_TAB));
#if !defined(OS_ANDROID)
  CoreAppLauncherHandler::RecordAppLaunchType(
      extension_misc::APP_LAUNCH_EXTENSION_API,
      extension->GetType());
#endif

  return true;
}

ManagementSetEnabledFunction::ManagementSetEnabledFunction() {
}

ManagementSetEnabledFunction::~ManagementSetEnabledFunction() {
}

bool ManagementSetEnabledFunction::RunImpl() {
  scoped_ptr<management::SetEnabled::Params> params(
      management::SetEnabled::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  extension_id_ = params->id;

  const Extension* extension = service()->GetInstalledExtension(extension_id_);
  if (!extension) {
    error_ = ErrorUtils::FormatErrorMessage(
        keys::kNoExtensionError, extension_id_);
    return false;
  }

  const ManagementPolicy* policy = ExtensionSystem::Get(profile())->
      management_policy();
  if (!policy->UserMayModifySettings(extension, NULL)) {
    error_ = ErrorUtils::FormatErrorMessage(
        keys::kUserCantModifyError, extension_id_);
    return false;
  }

  bool currently_enabled = service()->IsExtensionEnabled(extension_id_);

  if (!currently_enabled && params->enabled) {
    ExtensionPrefs* prefs = service()->extension_prefs();
    if (prefs->DidExtensionEscalatePermissions(extension_id_)) {
      if (!user_gesture()) {
        error_ = keys::kGestureNeededForEscalationError;
        return false;
      }
      AddRef(); // Matched in InstallUIProceed/InstallUIAbort
      install_prompt_.reset(
          new ExtensionInstallPrompt(GetAssociatedWebContents()));
      install_prompt_->ConfirmReEnable(this, extension);
      return true;
    }
    service()->EnableExtension(extension_id_);
  } else if (currently_enabled && !params->enabled) {
    service()->DisableExtension(extension_id_, Extension::DISABLE_USER_ACTION);
  }

  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&ManagementSetEnabledFunction::SendResponse, this, true));

  return true;
}

void ManagementSetEnabledFunction::InstallUIProceed() {
  service()->EnableExtension(extension_id_);
  SendResponse(true);
  Release();
}

void ManagementSetEnabledFunction::InstallUIAbort(bool user_initiated) {
  error_ = keys::kUserDidNotReEnableError;
  SendResponse(false);
  Release();
}

ManagementUninstallFunctionBase::ManagementUninstallFunctionBase() {
}

ManagementUninstallFunctionBase::~ManagementUninstallFunctionBase() {
}

bool ManagementUninstallFunctionBase::Uninstall(
    const std::string& extension_id,
    bool show_confirm_dialog) {
  extension_id_ = extension_id;
  const Extension* extension = service()->GetExtensionById(extension_id_, true);
  if (!extension) {
    error_ = ErrorUtils::FormatErrorMessage(
        keys::kNoExtensionError, extension_id_);
    return false;
  }

  if (!ExtensionSystem::Get(profile())->management_policy()->
      UserMayModifySettings(extension, NULL)) {
    error_ = ErrorUtils::FormatErrorMessage(
        keys::kUserCantModifyError, extension_id_);
    return false;
  }

  if (auto_confirm_for_test == DO_NOT_SKIP) {
    if (show_confirm_dialog) {
      AddRef(); // Balanced in ExtensionUninstallAccepted/Canceled
      extension_uninstall_dialog_.reset(ExtensionUninstallDialog::Create(
          profile(), GetCurrentBrowser(), this));
      extension_uninstall_dialog_->ConfirmUninstall(extension);
    } else {
      Finish(true);
    }
  } else {
    Finish(auto_confirm_for_test == PROCEED);
  }

  return true;
}

// static
void ManagementUninstallFunctionBase::SetAutoConfirmForTest(
    bool should_proceed) {
  auto_confirm_for_test = should_proceed ? PROCEED : ABORT;
}

void ManagementUninstallFunctionBase::Finish(bool should_uninstall) {
  if (should_uninstall) {
    bool success = service()->UninstallExtension(
        extension_id_,
        false, /* external uninstall */
        NULL);

    // TODO set error_ if !success
    SendResponse(success);
  } else {
    error_ = ErrorUtils::FormatErrorMessage(
        keys::kUninstallCanceledError, extension_id_);
    SendResponse(false);
  }

}

void ManagementUninstallFunctionBase::ExtensionUninstallAccepted() {
  Finish(true);
  Release();
}

void ManagementUninstallFunctionBase::ExtensionUninstallCanceled() {
  Finish(false);
  Release();
}

ManagementUninstallFunction::ManagementUninstallFunction() {
}

ManagementUninstallFunction::~ManagementUninstallFunction() {
}

bool ManagementUninstallFunction::RunImpl() {
  scoped_ptr<management::Uninstall::Params> params(
      management::Uninstall::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  bool show_confirm_dialog = false;
  if (params->options.get() && params->options->show_confirm_dialog.get())
    show_confirm_dialog = *params->options->show_confirm_dialog;

  return Uninstall(params->id, show_confirm_dialog);
}

ManagementUninstallSelfFunction::ManagementUninstallSelfFunction() {
}

ManagementUninstallSelfFunction::~ManagementUninstallSelfFunction() {
}

bool ManagementUninstallSelfFunction::RunImpl() {
  scoped_ptr<management::UninstallSelf::Params> params(
      management::UninstallSelf::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  bool show_confirm_dialog = false;
  if (params->options.get() && params->options->show_confirm_dialog.get())
    show_confirm_dialog = *params->options->show_confirm_dialog;
  return Uninstall(extension_->id(), show_confirm_dialog);
}

ManagementEventRouter::ManagementEventRouter(Profile* profile)
    : profile_(profile) {
  int types[] = {
    chrome::NOTIFICATION_EXTENSION_INSTALLED,
    chrome::NOTIFICATION_EXTENSION_UNINSTALLED,
    chrome::NOTIFICATION_EXTENSION_LOADED,
    chrome::NOTIFICATION_EXTENSION_UNLOADED
  };

  CHECK(registrar_.IsEmpty());
  for (size_t i = 0; i < arraysize(types); i++) {
    registrar_.Add(this,
                   types[i],
                   content::Source<Profile>(profile_));
  }
}

ManagementEventRouter::~ManagementEventRouter() {}

void ManagementEventRouter::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  const char* event_name = NULL;
  const Extension* extension = NULL;
  Profile* profile = content::Source<Profile>(source).ptr();
  CHECK(profile);
  CHECK(profile_->IsSameProfile(profile));

  switch (type) {
    case chrome::NOTIFICATION_EXTENSION_INSTALLED:
      event_name = events::kOnExtensionInstalled;
      extension =
          content::Details<const InstalledExtensionInfo>(details)->extension;
      break;
    case chrome::NOTIFICATION_EXTENSION_UNINSTALLED:
      event_name = events::kOnExtensionUninstalled;
      extension = content::Details<const Extension>(details).ptr();
      break;
    case chrome::NOTIFICATION_EXTENSION_LOADED:
      event_name = events::kOnExtensionEnabled;
      extension = content::Details<const Extension>(details).ptr();
      break;
    case chrome::NOTIFICATION_EXTENSION_UNLOADED:
      event_name = events::kOnExtensionDisabled;
      extension =
          content::Details<const UnloadedExtensionInfo>(details)->extension;
      break;
    default:
      NOTREACHED();
      return;
  }
  DCHECK(event_name);
  DCHECK(extension);

  scoped_ptr<base::ListValue> args(new base::ListValue());
  if (event_name == events::kOnExtensionUninstalled) {
    args->Append(Value::CreateStringValue(extension->id()));
  } else {
    scoped_ptr<management::ExtensionInfo> info = CreateExtensionInfo(
        *extension, ExtensionSystem::Get(profile));
    args->Append(info->ToValue().release());
  }

  scoped_ptr<Event> event(new Event(event_name, args.Pass()));
  ExtensionSystem::Get(profile)->event_router()->BroadcastEvent(event.Pass());
}

ManagementAPI::ManagementAPI(Profile* profile)
    : profile_(profile) {
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, events::kOnExtensionInstalled);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, events::kOnExtensionUninstalled);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, events::kOnExtensionEnabled);
  ExtensionSystem::Get(profile_)->event_router()->RegisterObserver(
      this, events::kOnExtensionDisabled);
}

ManagementAPI::~ManagementAPI() {
}

void ManagementAPI::Shutdown() {
  ExtensionSystem::Get(profile_)->event_router()->UnregisterObserver(this);
}

static base::LazyInstance<ProfileKeyedAPIFactory<ManagementAPI> >
g_factory = LAZY_INSTANCE_INITIALIZER;

// static
ProfileKeyedAPIFactory<ManagementAPI>* ManagementAPI::GetFactoryInstance() {
  return &g_factory.Get();
}

void ManagementAPI::OnListenerAdded(const EventListenerInfo& details) {
  management_event_router_.reset(new ManagementEventRouter(profile_));
  ExtensionSystem::Get(profile_)->event_router()->UnregisterObserver(this);
}

}  // namespace extensions
