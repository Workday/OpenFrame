// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/activity_log/activity_log.h"

#include <set>
#include <vector>

#include "base/command_line.h"
#include "base/json/json_string_value_serializer.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_checker.h"
#include "chrome/browser/extensions/activity_log/activity_action_constants.h"
#include "chrome/browser/extensions/activity_log/counting_policy.h"
#include "chrome/browser/extensions/activity_log/fullstream_ui_policy.h"
#include "chrome/browser/extensions/api/activity_log_private/activity_log_private_api.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_system_factory.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/install_tracker_factory.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"
#include "content/public/browser/web_contents.h"
#include "third_party/re2/re2/re2.h"
#include "url/gurl.h"

namespace constants = activity_log_constants;

namespace {

// Concatenate arguments.
std::string MakeArgList(const base::ListValue* args) {
  std::string call_signature;
  base::ListValue::const_iterator it = args->begin();
  for (; it != args->end(); ++it) {
    std::string arg;
    JSONStringValueSerializer serializer(&arg);
    if (serializer.SerializeAndOmitBinaryValues(**it)) {
      if (it != args->begin())
        call_signature += ", ";
      call_signature += arg;
    }
  }
  return call_signature;
}

// This is a hack for AL callers who don't have access to a profile object
// when deciding whether or not to do the work required for logging. The state
// is accessed through the static ActivityLog::IsLogEnabledOnAnyProfile()
// method. It returns true if --enable-extension-activity-logging is set on the
// command line OR *ANY* profile has the activity log whitelisted extension
// installed.
class LogIsEnabled {
 public:
  LogIsEnabled() : any_profile_enabled_(false) {
    ComputeIsFlagEnabled();
  }

  void ComputeIsFlagEnabled() {
    base::AutoLock auto_lock(lock_);
    cmd_line_enabled_ = CommandLine::ForCurrentProcess()->
        HasSwitch(switches::kEnableExtensionActivityLogging);
  }

  static LogIsEnabled* GetInstance() {
    return Singleton<LogIsEnabled>::get();
  }

  bool IsEnabled() {
    base::AutoLock auto_lock(lock_);
    return cmd_line_enabled_ || any_profile_enabled_;
  }

  void SetProfileEnabled(bool any_profile_enabled) {
    base::AutoLock auto_lock(lock_);
    any_profile_enabled_ = any_profile_enabled;
  }

 private:
  base::Lock lock_;
  bool any_profile_enabled_;
  bool cmd_line_enabled_;
};

// Gets the URL for a given tab ID.  Helper method for LookupTabId.  Returns
// true if able to perform the lookup.  The URL is stored to *url, and
// *is_incognito is set to indicate whether the URL is for an incognito tab.
bool GetUrlForTabId(int tab_id,
                    Profile* profile,
                    GURL* url,
                    bool* is_incognito) {
  content::WebContents* contents = NULL;
  Browser* browser = NULL;
  bool found = ExtensionTabUtil::GetTabById(tab_id,
                                            profile,
                                            true,  // search incognito tabs too
                                            &browser,
                                            NULL,
                                            &contents,
                                            NULL);
  if (found) {
    *url = contents->GetURL();
    *is_incognito = browser->profile()->IsOffTheRecord();
    return true;
  } else {
    return false;
  }
}

// Translate tab IDs to URLs in tabs API calls.  Mutates the Action object in
// place.  There is a small chance that the URL translation could be wrong, if
// the tab has already been navigated by the time of invocation.
//
// If a single tab ID is translated to a URL, the URL is stored into arg_url
// where it can more easily be searched for in the database.  For APIs that
// take a list of tab IDs, replace each tab ID with the URL in the argument
// list; we can only extract a single URL into arg_url so arbitrarily pull out
// the first one.
void LookupTabIds(scoped_refptr<extensions::Action> action, Profile* profile) {
  const std::string& api_call = action->api_name();
  if (api_call == "tabs.get" ||                 // api calls, ID as int
      api_call == "tabs.connect" ||
      api_call == "tabs.sendMessage" ||
      api_call == "tabs.duplicate" ||
      api_call == "tabs.update" ||
      api_call == "tabs.reload" ||
      api_call == "tabs.detectLanguage" ||
      api_call == "tabs.executeScript" ||
      api_call == "tabs.insertCSS" ||
      api_call == "tabs.move" ||                // api calls, IDs in array
      api_call == "tabs.remove" ||
      api_call == "tabs.onUpdated" ||           // events, ID as int
      api_call == "tabs.onMoved" ||
      api_call == "tabs.onDetached" ||
      api_call == "tabs.onAttached" ||
      api_call == "tabs.onRemoved" ||
      api_call == "tabs.onReplaced") {
    int tab_id;
    base::ListValue* id_list;
    base::ListValue* args = action->mutable_args();
    if (args->GetInteger(0, &tab_id)) {
      // Single tab ID to translate.
      GURL url;
      bool is_incognito;
      if (GetUrlForTabId(tab_id, profile, &url, &is_incognito)) {
        action->set_arg_url(url);
        action->set_arg_incognito(is_incognito);
      }
    } else if ((api_call == "tabs.move" || api_call == "tabs.remove") &&
               args->GetList(0, &id_list)) {
      // Array of tab IDs to translate.
      for (int i = 0; i < static_cast<int>(id_list->GetSize()); ++i) {
        if (id_list->GetInteger(i, &tab_id)) {
          GURL url;
          bool is_incognito;
          if (GetUrlForTabId(tab_id, profile, &url, &is_incognito) &&
              !is_incognito) {
            id_list->Set(i, new base::StringValue(url.spec()));
            if (i == 0) {
              action->set_arg_url(url);
              action->set_arg_incognito(is_incognito);
            }
          }
        } else {
          LOG(ERROR) << "The tab ID array is malformed at index " << i;
        }
      }
    }
  }
}

}  // namespace

namespace extensions {

// static
bool ActivityLog::IsLogEnabledOnAnyProfile() {
  return LogIsEnabled::GetInstance()->IsEnabled();
}

// static
void ActivityLog::RecomputeLoggingIsEnabled(bool profile_enabled) {
  LogIsEnabled::GetInstance()->ComputeIsFlagEnabled();
  LogIsEnabled::GetInstance()->SetProfileEnabled(profile_enabled);
}

// ActivityLogFactory

ActivityLogFactory* ActivityLogFactory::GetInstance() {
  return Singleton<ActivityLogFactory>::get();
}

BrowserContextKeyedService* ActivityLogFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  return new ActivityLog(static_cast<Profile*>(profile));
}

content::BrowserContext* ActivityLogFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

ActivityLogFactory::ActivityLogFactory()
    : BrowserContextKeyedServiceFactory(
        "ActivityLog",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionSystemFactory::GetInstance());
  DependsOn(InstallTrackerFactory::GetInstance());
}

ActivityLogFactory::~ActivityLogFactory() {
}

// ActivityLog

void ActivityLog::SetDefaultPolicy(ActivityLogPolicy::PolicyType policy_type) {
  // Can't use IsLogEnabled() here because this is called from inside Init.
  if (policy_type != policy_type_ && enabled_) {
    // Deleting the old policy takes place asynchronously, on the database
    // thread.  Initializing a new policy below similarly happens
    // asynchronously.  Since the two operations are both queued for the
    // database, the queue ordering should ensure that the deletion completes
    // before database initialization occurs.
    //
    // However, changing policies at runtime is still not recommended, and
    // likely only should be done for unit tests.
    if (policy_)
      policy_->Close();

    switch (policy_type) {
      case ActivityLogPolicy::POLICY_FULLSTREAM:
        policy_ = new FullStreamUIPolicy(profile_);
        break;
      case ActivityLogPolicy::POLICY_COUNTS:
        policy_ = new CountingPolicy(profile_);
        break;
      default:
        NOTREACHED();
    }
    policy_type_ = policy_type;
  }
}

// Use GetInstance instead of directly creating an ActivityLog.
ActivityLog::ActivityLog(Profile* profile)
    : policy_(NULL),
      policy_type_(ActivityLogPolicy::POLICY_INVALID),
      profile_(profile),
      enabled_(false),
      initialized_(false),
      policy_chosen_(false),
      testing_mode_(false),
      has_threads_(true),
      tracker_(NULL) {
  // This controls whether logging statements are printed, which policy is set,
  // etc.
  testing_mode_ = CommandLine::ForCurrentProcess()->HasSwitch(
    switches::kEnableExtensionActivityLogTesting);

  // Check that the right threads exist. If not, we shouldn't try to do things
  // that require them.
  if (!BrowserThread::IsMessageLoopValid(BrowserThread::DB) ||
      !BrowserThread::IsMessageLoopValid(BrowserThread::FILE) ||
      !BrowserThread::IsMessageLoopValid(BrowserThread::IO)) {
    LOG(ERROR) << "Missing threads, disabling Activity Logging!";
    has_threads_ = false;
  } else {
    enabled_ = IsLogEnabledOnAnyProfile();
    ExtensionSystem::Get(profile_)->ready().Post(
      FROM_HERE, base::Bind(&ActivityLog::Init, base::Unretained(this)));
  }

  observers_ = new ObserverListThreadSafe<Observer>;
}

void ActivityLog::Init() {
  DCHECK(has_threads_);
  DCHECK(!initialized_);
  const Extension* whitelisted_extension = ExtensionSystem::Get(profile_)->
      extension_service()->GetExtensionById(kActivityLogExtensionId, false);
  if (whitelisted_extension) {
    enabled_ = true;
    LogIsEnabled::GetInstance()->SetProfileEnabled(true);
  }
  tracker_ = InstallTrackerFactory::GetForProfile(profile_);
  tracker_->AddObserver(this);
  ChooseDefaultPolicy();
  initialized_ = true;
}

void ActivityLog::ChooseDefaultPolicy() {
  if (policy_chosen_ || !enabled_) return;
  if (testing_mode_)
    SetDefaultPolicy(ActivityLogPolicy::POLICY_FULLSTREAM);
  else
    SetDefaultPolicy(ActivityLogPolicy::POLICY_COUNTS);
}

void ActivityLog::Shutdown() {
  if (tracker_) tracker_->RemoveObserver(this);
}

ActivityLog::~ActivityLog() {
  if (policy_)
    policy_->Close();
}

bool ActivityLog::IsLogEnabled() {
  if (!has_threads_ || !initialized_) return false;
  return enabled_;
}

void ActivityLog::OnExtensionLoaded(const Extension* extension) {
  if (extension->id() != kActivityLogExtensionId) return;
  enabled_ = true;
  LogIsEnabled::GetInstance()->SetProfileEnabled(true);
  ChooseDefaultPolicy();
}

void ActivityLog::OnExtensionUnloaded(const Extension* extension) {
  if (extension->id() != kActivityLogExtensionId) return;
  if (!CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableExtensionActivityLogging))
    enabled_ = false;
}

// static
ActivityLog* ActivityLog::GetInstance(Profile* profile) {
  return ActivityLogFactory::GetForProfile(profile);
}

void ActivityLog::AddObserver(ActivityLog::Observer* observer) {
  observers_->AddObserver(observer);
}

void ActivityLog::RemoveObserver(ActivityLog::Observer* observer) {
  observers_->RemoveObserver(observer);
}

void ActivityLog::LogAction(scoped_refptr<Action> action) {
  if (!IsLogEnabled() ||
      ActivityLogAPI::IsExtensionWhitelisted(action->extension_id()))
    return;

  // Perform some preprocessing of the Action data: convert tab IDs to URLs and
  // mask out incognito URLs if appropriate.
  if ((action->action_type() == Action::ACTION_API_CALL ||
       action->action_type() == Action::ACTION_API_EVENT) &&
      StartsWithASCII(action->api_name(), "tabs.", true)) {
    LookupTabIds(action, profile_);
  }

  // TODO(mvrable): Add any necessary processing of incognito URLs here, for
  // crbug.com/253368

  if (policy_)
    policy_->ProcessAction(action);
  observers_->Notify(&Observer::OnExtensionActivity, action);
  if (testing_mode_)
    LOG(INFO) << action->PrintForDebug();
}

void ActivityLog::GetActions(
    const std::string& extension_id,
    const int day,
    const base::Callback
        <void(scoped_ptr<std::vector<scoped_refptr<Action> > >)>& callback) {
  if (policy_) {
    policy_->ReadData(extension_id, day, callback);
  }
}

void ActivityLog::OnScriptsExecuted(
    const content::WebContents* web_contents,
    const ExecutingScriptsMap& extension_ids,
    int32 on_page_id,
    const GURL& on_url) {
  if (!IsLogEnabled()) return;
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  const ExtensionService* extension_service =
      ExtensionSystem::Get(profile)->extension_service();
  const ExtensionSet* extensions = extension_service->extensions();
  const prerender::PrerenderManager* prerender_manager =
      prerender::PrerenderManagerFactory::GetForProfile(
          Profile::FromBrowserContext(web_contents->GetBrowserContext()));

  for (ExecutingScriptsMap::const_iterator it = extension_ids.begin();
       it != extension_ids.end(); ++it) {
    const Extension* extension = extensions->GetByID(it->first);
    if (!extension || ActivityLogAPI::IsExtensionWhitelisted(extension->id()))
      continue;

    // If OnScriptsExecuted is fired because of tabs.executeScript, the list
    // of content scripts will be empty.  We don't want to log it because
    // the call to tabs.executeScript will have already been logged anyway.
    if (!it->second.empty()) {
      scoped_refptr<Action> action;
      action = new Action(extension->id(),
                          base::Time::Now(),
                          Action::ACTION_CONTENT_SCRIPT,
                          "");  // no API call here
      action->set_page_url(on_url);
      action->set_page_title(base::UTF16ToUTF8(web_contents->GetTitle()));
      action->set_page_incognito(
          web_contents->GetBrowserContext()->IsOffTheRecord());
      if (prerender_manager &&
          prerender_manager->IsWebContentsPrerendering(web_contents, NULL))
        action->mutable_other()->SetBoolean(constants::kActionPrerender, true);
      for (std::set<std::string>::const_iterator it2 = it->second.begin();
           it2 != it->second.end();
           ++it2) {
        action->mutable_args()->AppendString(*it2);
      }
      LogAction(action);
    }
  }
}

}  // namespace extensions
