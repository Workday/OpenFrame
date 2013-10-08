// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/help/help_handler.h"

#include <string>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/send_feedback_experiment.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/content_client.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/google_chrome_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "v8/include/v8.h"
#include "webkit/common/user_agent/user_agent_util.h"

#if defined(OS_CHROMEOS)
#include "base/files/file_util_proxy.h"
#include "base/i18n/time_formatting.h"
#include "base/prefs/pref_service.h"
#include "base/sys_info.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/help/help_utils_chromeos.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/power_manager_client.h"
#include "content/public/browser/browser_thread.h"
#endif

using base::ListValue;
using content::BrowserThread;

namespace {

const char kResourceReportIssue[] = "reportAnIssue";

// Returns the browser version as a string.
string16 BuildBrowserVersionString() {
  chrome::VersionInfo version_info;
  DCHECK(version_info.is_valid());

  std::string browser_version = version_info.Version();
  std::string version_modifier =
      chrome::VersionInfo::GetVersionStringModifier();
  if (!version_modifier.empty())
    browser_version += " " + version_modifier;

#if !defined(GOOGLE_CHROME_BUILD)
  browser_version += " (";
  browser_version += version_info.LastChange();
  browser_version += ")";
#endif

  return UTF8ToUTF16(browser_version);
}

#if defined(OS_CHROMEOS)

// Returns message that informs user that for update it's better to
// connect to a network of one of the allowed types.
string16 GetAllowedConnectionTypesMessage() {
  if (help_utils_chromeos::IsUpdateOverCellularAllowed()) {
    return l10n_util::GetStringUTF16(IDS_UPGRADE_NETWORK_LIST_CELLULAR_ALLOWED);
  } else {
    return l10n_util::GetStringUTF16(
        IDS_UPGRADE_NETWORK_LIST_CELLULAR_DISALLOWED);
  }
}

// Returns true if the device is enterprise managed, false otherwise.
bool IsEnterpriseManaged() {
  return g_browser_process->browser_policy_connector()->IsEnterpriseManaged();
}

// Returns true if current user can change channel, false otherwise.
bool CanChangeChannel() {
  bool value = false;
  chromeos::CrosSettings::Get()->GetBoolean(chromeos::kReleaseChannelDelegated,
                                            &value);

  // On a managed machine we delegate this setting to the users of the same
  // domain only if the policy value is "domain".
  if (IsEnterpriseManaged()) {
    if (!value)
      return false;
    // Get the currently logged in user and strip the domain part only.
    std::string domain = "";
    std::string user = chromeos::UserManager::Get()->GetLoggedInUser()->email();
    size_t at_pos = user.find('@');
    if (at_pos != std::string::npos && at_pos + 1 < user.length())
      domain = user.substr(user.find('@') + 1);
    return domain == g_browser_process->browser_policy_connector()->
        GetEnterpriseDomain();
  } else if (chromeos::UserManager::Get()->IsCurrentUserOwner()) {
    // On non managed machines we have local owner who is the only one to change
    // anything. Ensure that ReleaseChannelDelegated is false.
    return !value;
  }
  return false;
}

// Pointer to a |StringValue| holding the date of the build date to Chromium
// OS. Because this value is obtained by reading a file, it is cached here to
// prevent the need to read from the file system multiple times unnecessarily.
Value* g_build_date_string = NULL;

#endif  // defined(OS_CHROMEOS)

}  // namespace

HelpHandler::HelpHandler()
    : version_updater_(VersionUpdater::Create()),
      weak_factory_(this) {
}

HelpHandler::~HelpHandler() {
}

void HelpHandler::GetLocalizedValues(content::WebUIDataSource* source) {
  struct L10nResources {
    const char* name;
    int ids;
  };

  static L10nResources resources[] = {
    { "helpTitle", IDS_HELP_TITLE },
    { "aboutTitle", IDS_ABOUT_TAB_TITLE },
#if defined(OS_CHROMEOS)
    { "aboutProductTitle", IDS_PRODUCT_OS_NAME },
#else
    { "aboutProductTitle", IDS_PRODUCT_NAME },
#endif
    { "aboutProductDescription", IDS_ABOUT_PRODUCT_DESCRIPTION },
    { "relaunch", IDS_RELAUNCH_BUTTON },
    { "relaunch", IDS_RELAUNCH_BUTTON },
#if defined(OS_CHROMEOS)
    { "relaunchAndPowerwash", IDS_RELAUNCH_AND_POWERWASH_BUTTON },
#endif
    { "productName", IDS_PRODUCT_NAME },
    { "productCopyright", IDS_ABOUT_VERSION_COPYRIGHT },
    { "updateCheckStarted", IDS_UPGRADE_CHECK_STARTED },
    { "upToDate", IDS_UPGRADE_UP_TO_DATE },
    { "updating", IDS_UPGRADE_UPDATING },
#if defined(OS_CHROMEOS)
    { "updatingChannelSwitch", IDS_UPGRADE_UPDATING_CHANNEL_SWITCH },
#endif
    { "updateAlmostDone", IDS_UPGRADE_SUCCESSFUL_RELAUNCH },
#if defined(OS_CHROMEOS)
    { "successfulChannelSwitch", IDS_UPGRADE_SUCCESSFUL_CHANNEL_SWITCH },
#endif
    { "getHelpWithChrome", IDS_GET_HELP_USING_CHROME },
    { kResourceReportIssue, IDS_REPORT_AN_ISSUE },
#if defined(OS_CHROMEOS)
    { "platform", IDS_PLATFORM_LABEL },
    { "firmware", IDS_ABOUT_PAGE_FIRMWARE },
    { "showMoreInfo", IDS_SHOW_MORE_INFO },
    { "hideMoreInfo", IDS_HIDE_MORE_INFO },
    { "channel", IDS_ABOUT_PAGE_CHANNEL },
    { "stable", IDS_ABOUT_PAGE_CHANNEL_STABLE },
    { "beta", IDS_ABOUT_PAGE_CHANNEL_BETA },
    { "dev", IDS_ABOUT_PAGE_CHANNEL_DEVELOPMENT },
    { "channel-changed", IDS_ABOUT_PAGE_CHANNEL_CHANGED },
    { "currentChannelStable", IDS_ABOUT_PAGE_CURRENT_CHANNEL_STABLE },
    { "currentChannelBeta", IDS_ABOUT_PAGE_CURRENT_CHANNEL_BETA },
    { "currentChannelDev", IDS_ABOUT_PAGE_CURRENT_CHANNEL_DEV },
    { "currentChannel", IDS_ABOUT_PAGE_CURRENT_CHANNEL },
    { "channelChangeButton", IDS_ABOUT_PAGE_CHANNEL_CHANGE_BUTTON },
    { "channelChangeDisallowedMessage",
      IDS_ABOUT_PAGE_CHANNEL_CHANGE_DISALLOWED_MESSAGE },
    { "channelChangePageTitle", IDS_ABOUT_PAGE_CHANNEL_CHANGE_PAGE_TITLE },
    { "channelChangePagePowerwashTitle",
      IDS_ABOUT_PAGE_CHANNEL_CHANGE_PAGE_POWERWASH_TITLE },
    { "channelChangePagePowerwashMessage",
      IDS_ABOUT_PAGE_CHANNEL_CHANGE_PAGE_POWERWASH_MESSAGE },
    { "channelChangePageDelayedChangeTitle",
      IDS_ABOUT_PAGE_CHANNEL_CHANGE_PAGE_DELAYED_CHANGE_TITLE },
    { "channelChangePageUnstableTitle",
      IDS_ABOUT_PAGE_CHANNEL_CHANGE_PAGE_UNSTABLE_TITLE },
    { "channelChangePagePowerwashButton",
      IDS_ABOUT_PAGE_CHANNEL_CHANGE_PAGE_POWERWASH_BUTTON },
    { "channelChangePageChangeButton",
      IDS_ABOUT_PAGE_CHANNEL_CHANGE_PAGE_CHANGE_BUTTON },
    { "channelChangePageCancelButton",
      IDS_ABOUT_PAGE_CHANNEL_CHANGE_PAGE_CANCEL_BUTTON },
    { "webkit", IDS_WEBKIT },
    { "userAgent", IDS_ABOUT_VERSION_USER_AGENT },
    { "commandLine", IDS_ABOUT_VERSION_COMMAND_LINE },
    { "buildDate", IDS_ABOUT_VERSION_BUILD_DATE },
#endif
#if defined(OS_MACOSX)
    { "promote", IDS_ABOUT_CHROME_PROMOTE_UPDATER },
    { "learnMore", IDS_LEARN_MORE },
#endif
  };

  if (chrome::UseAlternateSendFeedbackText()) {
    // Field trial to substitute "Report an Issue" with "Send Feedback".
    // (crbug.com/169339)
    std::string report_issue_key(kResourceReportIssue);
    for (size_t i = 0; i < ARRAYSIZE_UNSAFE(resources); ++i) {
      if (report_issue_key == resources[i].name)
        resources[i].ids = IDS_SEND_FEEDBACK;
    }
  }

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(resources); ++i) {
    source->AddString(resources[i].name,
                      l10n_util::GetStringUTF16(resources[i].ids));
  }

  source->AddString(
      "browserVersion",
      l10n_util::GetStringFUTF16(IDS_ABOUT_PRODUCT_VERSION,
                                 BuildBrowserVersionString()));

  string16 license = l10n_util::GetStringFUTF16(
      IDS_ABOUT_VERSION_LICENSE,
      ASCIIToUTF16(chrome::kChromiumProjectURL),
      ASCIIToUTF16(chrome::kChromeUICreditsURL));
  source->AddString("productLicense", license);

#if defined(OS_CHROMEOS)
  string16 os_license = l10n_util::GetStringFUTF16(
      IDS_ABOUT_CROS_VERSION_LICENSE,
      ASCIIToUTF16(chrome::kChromeUIOSCreditsURL));
  source->AddString("productOsLicense", os_license);

  string16 product_name = l10n_util::GetStringUTF16(IDS_PRODUCT_OS_NAME);
  source->AddString(
      "channelChangePageDelayedChangeMessage",
      l10n_util::GetStringFUTF16(
          IDS_ABOUT_PAGE_CHANNEL_CHANGE_PAGE_DELAYED_CHANGE_MESSAGE,
          product_name));
  source->AddString(
      "channelChangePageUnstableMessage",
      l10n_util::GetStringFUTF16(
          IDS_ABOUT_PAGE_CHANNEL_CHANGE_PAGE_UNSTABLE_MESSAGE,
          product_name));

  if (CommandLine::ForCurrentProcess()->
      HasSwitch(chromeos::switches::kDisableNewChannelSwitcherUI)) {
    source->AddBoolean("disableNewChannelSwitcherUI", true);
  }
#endif

  string16 tos = l10n_util::GetStringFUTF16(
      IDS_ABOUT_TERMS_OF_SERVICE, UTF8ToUTF16(chrome::kChromeUITermsURL));
  source->AddString("productTOS", tos);

  source->AddString("webkitVersion", webkit_glue::GetWebKitVersion());

  source->AddString("jsEngine", "V8");
  source->AddString("jsEngineVersion", v8::V8::GetVersion());

  source->AddString("userAgentInfo", content::GetUserAgent(GURL()));

  CommandLine::StringType command_line =
      CommandLine::ForCurrentProcess()->GetCommandLineString();
  source->AddString("commandLineInfo", command_line);
}

void HelpHandler::RegisterMessages() {
  registrar_.Add(this, chrome::NOTIFICATION_UPGRADE_RECOMMENDED,
                 content::NotificationService::AllSources());

  web_ui()->RegisterMessageCallback("onPageLoaded",
      base::Bind(&HelpHandler::OnPageLoaded, base::Unretained(this)));
  web_ui()->RegisterMessageCallback("relaunchNow",
      base::Bind(&HelpHandler::RelaunchNow, base::Unretained(this)));
  web_ui()->RegisterMessageCallback("openFeedbackDialog",
      base::Bind(&HelpHandler::OpenFeedbackDialog, base::Unretained(this)));
  web_ui()->RegisterMessageCallback("openHelpPage",
      base::Bind(&HelpHandler::OpenHelpPage, base::Unretained(this)));
#if defined(OS_CHROMEOS)
  web_ui()->RegisterMessageCallback("setChannel",
      base::Bind(&HelpHandler::SetChannel, base::Unretained(this)));
  web_ui()->RegisterMessageCallback("relaunchAndPowerwash",
      base::Bind(&HelpHandler::RelaunchAndPowerwash, base::Unretained(this)));
#endif
#if defined(OS_MACOSX)
  web_ui()->RegisterMessageCallback("promoteUpdater",
      base::Bind(&HelpHandler::PromoteUpdater, base::Unretained(this)));
#endif
}

void HelpHandler::Observe(int type, const content::NotificationSource& source,
                          const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_UPGRADE_RECOMMENDED: {
      // A version update is installed and ready to go. Refresh the UI so the
      // correct state will be shown.
      version_updater_->CheckForUpdate(
          base::Bind(&HelpHandler::SetUpdateStatus, base::Unretained(this))
#if defined(OS_MACOSX)
          , base::Bind(&HelpHandler::SetPromotionState, base::Unretained(this))
#endif
          );
      break;
    }
    default:
      NOTREACHED();
  }
}

void HelpHandler::OnPageLoaded(const ListValue* args) {
#if defined(OS_CHROMEOS)
  // Version information is loaded from a callback
  loader_.GetVersion(
      chromeos::VersionLoader::VERSION_FULL,
      base::Bind(&HelpHandler::OnOSVersion, base::Unretained(this)),
      &tracker_);
  loader_.GetFirmware(
      base::Bind(&HelpHandler::OnOSFirmware, base::Unretained(this)),
      &tracker_);

  web_ui()->CallJavascriptFunction(
      "help.HelpPage.updateEnableReleaseChannel",
      base::FundamentalValue(CanChangeChannel()));

  if (g_build_date_string == NULL) {
    // If |g_build_date_string| is |NULL|, the date has not yet been assigned.
    // Get the date of the last lsb-release file modification.
    base::FileUtilProxy::GetFileInfo(
        BrowserThread::GetMessageLoopProxyForThread(BrowserThread::FILE).get(),
        base::SysInfo::GetLsbReleaseFilePath(),
        base::Bind(&HelpHandler::ProcessLsbFileInfo,
                   weak_factory_.GetWeakPtr()));
  } else {
    web_ui()->CallJavascriptFunction("help.HelpPage.setBuildDate",
                                     *g_build_date_string);
  }
#endif  // defined(OS_CHROMEOS)

  version_updater_->CheckForUpdate(
      base::Bind(&HelpHandler::SetUpdateStatus, base::Unretained(this))
#if defined(OS_MACOSX)
      , base::Bind(&HelpHandler::SetPromotionState, base::Unretained(this))
#endif
      );

#if defined(OS_CHROMEOS)
  web_ui()->CallJavascriptFunction(
      "help.HelpPage.updateIsEnterpriseManaged",
      base::FundamentalValue(IsEnterpriseManaged()));
  // First argument to GetChannel() is a flag that indicates whether
  // current channel should be returned (if true) or target channel
  // (otherwise).
  version_updater_->GetChannel(true,
      base::Bind(&HelpHandler::OnCurrentChannel, weak_factory_.GetWeakPtr()));
  version_updater_->GetChannel(false,
      base::Bind(&HelpHandler::OnTargetChannel, weak_factory_.GetWeakPtr()));
#endif
}

#if defined(OS_MACOSX)
void HelpHandler::PromoteUpdater(const ListValue* args) {
  version_updater_->PromoteUpdater();
}
#endif

void HelpHandler::RelaunchNow(const ListValue* args) {
  DCHECK(args->empty());
  version_updater_->RelaunchBrowser();
}

void HelpHandler::OpenFeedbackDialog(const ListValue* args) {
  DCHECK(args->empty());
  Browser* browser = chrome::FindBrowserWithWebContents(
      web_ui()->GetWebContents());
  chrome::OpenFeedbackDialog(browser);
}

void HelpHandler::OpenHelpPage(const base::ListValue* args) {
  DCHECK(args->empty());
  Browser* browser = chrome::FindBrowserWithWebContents(
      web_ui()->GetWebContents());
  chrome::ShowHelp(browser, chrome::HELP_SOURCE_WEBUI);
}

#if defined(OS_CHROMEOS)

void HelpHandler::SetChannel(const ListValue* args) {
  DCHECK(args->GetSize() == 2);

  if (!CanChangeChannel()) {
    LOG(WARNING) << "Non-owner tried to change release track.";
    return;
  }

  base::string16 channel;
  bool is_powerwash_allowed;
  if (!args->GetString(0, &channel) ||
      !args->GetBoolean(1, &is_powerwash_allowed)) {
    LOG(ERROR) << "Can't parse SetChannel() args";
    return;
  }

  version_updater_->SetChannel(UTF16ToUTF8(channel), is_powerwash_allowed);
  if (chromeos::UserManager::Get()->IsCurrentUserOwner()) {
    // Check for update after switching release channel.
    version_updater_->CheckForUpdate(base::Bind(&HelpHandler::SetUpdateStatus,
                                                base::Unretained(this)));
  }
}

void HelpHandler::RelaunchAndPowerwash(const ListValue* args) {
  DCHECK(args->empty());

  if (IsEnterpriseManaged())
    return;

  PrefService* prefs = g_browser_process->local_state();
  prefs->SetBoolean(prefs::kFactoryResetRequested, true);
  prefs->CommitPendingWrite();

  // Perform sign out. Current chrome process will then terminate, new one will
  // be launched (as if it was a restart).
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->RequestRestart();
}

#endif  // defined(OS_CHROMEOS)

void HelpHandler::SetUpdateStatus(VersionUpdater::Status status,
                                  int progress, const string16& message) {
  // Only UPDATING state should have progress set.
  DCHECK(status == VersionUpdater::UPDATING || progress == 0);

  std::string status_str;
  switch (status) {
  case VersionUpdater::CHECKING:
    status_str = "checking";
    break;
  case VersionUpdater::UPDATING:
    status_str = "updating";
    break;
  case VersionUpdater::NEARLY_UPDATED:
    status_str = "nearly_updated";
    break;
  case VersionUpdater::UPDATED:
    status_str = "updated";
    break;
  case VersionUpdater::FAILED:
  case VersionUpdater::FAILED_OFFLINE:
  case VersionUpdater::FAILED_CONNECTION_TYPE_DISALLOWED:
    status_str = "failed";
    break;
  case VersionUpdater::DISABLED:
    status_str = "disabled";
    break;
  }

  web_ui()->CallJavascriptFunction("help.HelpPage.setUpdateStatus",
                                   base::StringValue(status_str),
                                   base::StringValue(message));

  if (status == VersionUpdater::UPDATING) {
    web_ui()->CallJavascriptFunction("help.HelpPage.setProgress",
                                     base::FundamentalValue(progress));
  }

#if defined(OS_CHROMEOS)
  if (status == VersionUpdater::FAILED_OFFLINE ||
      status == VersionUpdater::FAILED_CONNECTION_TYPE_DISALLOWED) {
    string16 types_msg = GetAllowedConnectionTypesMessage();
    if (!types_msg.empty()) {
      web_ui()->CallJavascriptFunction(
          "help.HelpPage.setAndShowAllowedConnectionTypesMsg",
          base::StringValue(types_msg));
    } else {
      web_ui()->CallJavascriptFunction(
          "help.HelpPage.showAllowedConnectionTypesMsg",
          base::FundamentalValue(false));
    }
  } else {
    web_ui()->CallJavascriptFunction(
        "help.HelpPage.showAllowedConnectionTypesMsg",
        base::FundamentalValue(false));
  }
#endif  // defined(OS_CHROMEOS)
}

#if defined(OS_MACOSX)
void HelpHandler::SetPromotionState(VersionUpdater::PromotionState state) {
  std::string state_str;
  switch (state) {
  case VersionUpdater::PROMOTE_HIDDEN:
    state_str = "hidden";
    break;
  case VersionUpdater::PROMOTE_ENABLED:
    state_str = "enabled";
    break;
  case VersionUpdater::PROMOTE_DISABLED:
    state_str = "disabled";
    break;
  }

  web_ui()->CallJavascriptFunction("help.HelpPage.setPromotionState",
                                   base::StringValue(state_str));
}
#endif  // defined(OS_MACOSX)

#if defined(OS_CHROMEOS)
void HelpHandler::OnOSVersion(const std::string& version) {
  web_ui()->CallJavascriptFunction("help.HelpPage.setOSVersion",
                                   base::StringValue(version));
}

void HelpHandler::OnOSFirmware(const std::string& firmware) {
  web_ui()->CallJavascriptFunction("help.HelpPage.setOSFirmware",
                                   base::StringValue(firmware));
}

void HelpHandler::OnCurrentChannel(const std::string& channel) {
  web_ui()->CallJavascriptFunction(
      "help.HelpPage.updateCurrentChannel", base::StringValue(channel));
}

void HelpHandler::OnTargetChannel(const std::string& channel) {
  web_ui()->CallJavascriptFunction(
      "help.HelpPage.updateTargetChannel", base::StringValue(channel));
}

void HelpHandler::ProcessLsbFileInfo(
    base::PlatformFileError error, const base::PlatformFileInfo& file_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // If |g_build_date_string| is not |NULL|, then the file's information has
  // already been retrieved by another tab.
  if (g_build_date_string == NULL) {
    base::Time time;
    if (error == base::PLATFORM_FILE_OK) {
      // Retrieves the time at which the Chrome OS build was created.
      // Each time a new build is created, /etc/lsb-release is modified with the
      // new version numbers of the release.
      time = file_info.last_modified;
    } else {
      // If the time of the build cannot be retrieved, return and do not
      // display the "Build Date" section.
      return;
    }

    // Note that this string will be internationalized.
    string16 build_date = base::TimeFormatFriendlyDate(time);
    g_build_date_string = Value::CreateStringValue(build_date);
  }

  web_ui()->CallJavascriptFunction("help.HelpPage.setBuildDate",
                                   *g_build_date_string);
}
#endif // defined(OS_CHROMEOS)
