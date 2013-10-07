// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/first_run/first_run.h"

#include <algorithm>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/prefs/pref_service.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/updater/extension_updater.h"
#include "chrome/browser/first_run/first_run_internal.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/importer/importer_list.h"
#include "chrome/browser/importer/importer_progress_observer.h"
#include "chrome/browser/importer/importer_uma.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/signin/signin_promo.h"
#include "chrome/browser/signin/signin_tracker.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/global_error/global_error_service.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/ntp/new_tab_ui.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/installer/util/master_preferences.h"
#include "chrome/installer/util/master_preferences_constants.h"
#include "chrome/installer/util/util_constants.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "url/gurl.h"

using content::UserMetricsAction;

namespace {

// A bitfield formed from values in AutoImportState to record the state of
// AutoImport. This is used in testing to verify import startup actions that
// occur before an observer can be registered in the test.
uint16 g_auto_import_state = first_run::AUTO_IMPORT_NONE;

// Flags for functions of similar name.
bool g_should_show_welcome_page = false;
bool g_should_do_autofill_personal_data_manager_first_run = false;

// This class acts as an observer for the ImporterProgressObserver::ImportEnded
// callback. When the import process is started, certain errors may cause
// ImportEnded() to be called synchronously, but the typical case is that
// ImportEnded() is called asynchronously. Thus we have to handle both cases.
class ImportEndedObserver : public importer::ImporterProgressObserver {
 public:
  ImportEndedObserver() : ended_(false),
                          should_quit_message_loop_(false) {}
  virtual ~ImportEndedObserver() {}

  // importer::ImporterProgressObserver:
  virtual void ImportStarted() OVERRIDE {}
  virtual void ImportItemStarted(importer::ImportItem item) OVERRIDE {}
  virtual void ImportItemEnded(importer::ImportItem item) OVERRIDE {}
  virtual void ImportEnded() OVERRIDE {
    ended_ = true;
    if (should_quit_message_loop_)
      base::MessageLoop::current()->Quit();
  }

  void set_should_quit_message_loop() {
    should_quit_message_loop_ = true;
  }

  bool ended() const {
    return ended_;
  }

 private:
  // Set if the import has ended.
  bool ended_;

  bool should_quit_message_loop_;
};

// Helper class that performs delayed first-run tasks that need more of the
// chrome infrastructure to be up and running before they can be attempted.
class FirstRunDelayedTasks : public content::NotificationObserver {
 public:
  enum Tasks {
    NO_TASK,
    INSTALL_EXTENSIONS
  };

  explicit FirstRunDelayedTasks(Tasks task) {
    if (task == INSTALL_EXTENSIONS) {
      registrar_.Add(this, chrome::NOTIFICATION_EXTENSIONS_READY,
                     content::NotificationService::AllSources());
    }
    registrar_.Add(this, chrome::NOTIFICATION_BROWSER_CLOSED,
                   content::NotificationService::AllSources());
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    // After processing the notification we always delete ourselves.
    if (type == chrome::NOTIFICATION_EXTENSIONS_READY) {
      DoExtensionWork(
          content::Source<Profile>(source).ptr()->GetExtensionService());
    }
    delete this;
  }

 private:
  // Private ctor forces it to be created only in the heap.
  virtual ~FirstRunDelayedTasks() {}

  // The extension work is to basically trigger an extension update check.
  // If the extension specified in the master pref is older than the live
  // extension it will get updated which is the same as get it installed.
  void DoExtensionWork(ExtensionService* service) {
    if (service)
      service->updater()->CheckNow(extensions::ExtensionUpdater::CheckParams());
  }

  content::NotificationRegistrar registrar_;
};

// Installs a task to do an extensions update check once the extensions system
// is running.
void DoDelayedInstallExtensions() {
  new FirstRunDelayedTasks(FirstRunDelayedTasks::INSTALL_EXTENSIONS);
}

void DoDelayedInstallExtensionsIfNeeded(
    installer::MasterPreferences* install_prefs) {
  DictionaryValue* extensions = 0;
  if (install_prefs->GetExtensionsBlock(&extensions)) {
    VLOG(1) << "Extensions block found in master preferences";
    DoDelayedInstallExtensions();
  }
}

base::FilePath GetDefaultPrefFilePath(bool create_profile_dir,
                                      const base::FilePath& user_data_dir) {
  base::FilePath default_pref_dir =
      profiles::GetDefaultProfileDir(user_data_dir);
  if (create_profile_dir) {
    if (!base::PathExists(default_pref_dir)) {
      if (!file_util::CreateDirectory(default_pref_dir))
        return base::FilePath();
    }
  }
  return profiles::GetProfilePrefsPath(default_pref_dir);
}

// Sets the |items| bitfield according to whether the import data specified by
// |import_type| should be be auto imported or not.
void SetImportItem(PrefService* user_prefs,
                   const char* pref_path,
                   int import_items,
                   int dont_import_items,
                   importer::ImportItem import_type,
                   int* items) {
  // Work out whether an item is to be imported according to what is specified
  // in master preferences.
  bool should_import = false;
  bool master_pref_set =
      ((import_items | dont_import_items) & import_type) != 0;
  bool master_pref = ((import_items & ~dont_import_items) & import_type) != 0;

  if (import_type == importer::HISTORY ||
      (import_type != importer::FAVORITES &&
       first_run::internal::IsOrganicFirstRun())) {
    // History is always imported unless turned off in master_preferences.
    // Search engines and home page are imported in organic builds only
    // unless turned off in master_preferences.
    should_import = !master_pref_set || master_pref;
  } else {
    // Bookmarks are never imported, unless turned on in master_preferences.
    // Search engine and home page import behaviour is similar in non organic
    // builds.
    should_import = master_pref_set && master_pref;
  }

  // If an import policy is set, import items according to policy. If no master
  // preference is set, but a corresponding recommended policy is set, import
  // item according to recommended policy. If both a master preference and a
  // recommended policy is set, the master preference wins. If neither
  // recommended nor managed policies are set, import item according to what we
  // worked out above.
  if (master_pref_set)
    user_prefs->SetBoolean(pref_path, should_import);

  if (!user_prefs->FindPreference(pref_path)->IsDefaultValue()) {
    if (user_prefs->GetBoolean(pref_path))
      *items |= import_type;
  } else { // no policy (recommended or managed) is set
    if (should_import)
      *items |= import_type;
  }

  user_prefs->ClearPref(pref_path);
}

// Launches the import, via |importer_host|, from |source_profile| into
// |target_profile| for the items specified in the |items_to_import| bitfield.
// This may be done in a separate process depending on the platform, but it will
// always block until done.
void ImportFromSourceProfile(ExternalProcessImporterHost* importer_host,
                             const importer::SourceProfile& source_profile,
                             Profile* target_profile,
                             uint16 items_to_import) {
  ImportEndedObserver observer;
  importer_host->set_observer(&observer);
  importer_host->StartImportSettings(source_profile,
                                     target_profile,
                                     items_to_import,
                                     new ProfileWriter(target_profile));
  // If the import process has not errored out, block on it.
  if (!observer.ended()) {
    observer.set_should_quit_message_loop();
    base::MessageLoop::current()->Run();
  }
}

// Imports bookmarks from an html file whose path is provided by
// |import_bookmarks_path|.
void ImportFromFile(Profile* profile,
                    ExternalProcessImporterHost* file_importer_host,
                    const std::string& import_bookmarks_path) {
  importer::SourceProfile source_profile;
  source_profile.importer_type = importer::TYPE_BOOKMARKS_FILE;

  const base::FilePath::StringType& import_bookmarks_path_str =
#if defined(OS_WIN)
      UTF8ToUTF16(import_bookmarks_path);
#else
      import_bookmarks_path;
#endif
  source_profile.source_path = base::FilePath(import_bookmarks_path_str);

  ImportFromSourceProfile(file_importer_host, source_profile, profile,
                          importer::FAVORITES);
  g_auto_import_state |= first_run::AUTO_IMPORT_BOOKMARKS_FILE_IMPORTED;
}

// Imports settings from the first profile in |importer_list|.
void ImportSettings(Profile* profile,
                    ExternalProcessImporterHost* importer_host,
                    scoped_refptr<ImporterList> importer_list,
                    int items_to_import) {
  const importer::SourceProfile& source_profile =
      importer_list->GetSourceProfileAt(0);

  // Ensure that importers aren't requested to import items that they do not
  // support. If there is no overlap, skip.
  items_to_import &= source_profile.services_supported;
  if (items_to_import == 0)
    return;

  ImportFromSourceProfile(importer_host, source_profile, profile,
                          items_to_import);
  g_auto_import_state |= first_run::AUTO_IMPORT_PROFILE_IMPORTED;
}

GURL UrlFromString(const std::string& in) {
  return GURL(in);
}

void ConvertStringVectorToGURLVector(
    const std::vector<std::string>& src,
    std::vector<GURL>* ret) {
  ret->resize(src.size());
  std::transform(src.begin(), src.end(), ret->begin(), &UrlFromString);
}

// Show the first run search engine bubble at the first appropriate opportunity.
// This bubble may be delayed by other UI, like global errors and sync promos.
class FirstRunBubbleLauncher : public content::NotificationObserver {
 public:
  // Show the bubble at the first appropriate opportunity. This function
  // instantiates a FirstRunBubbleLauncher, which manages its own lifetime.
  static void ShowFirstRunBubbleSoon();

 private:
  FirstRunBubbleLauncher();
  virtual ~FirstRunBubbleLauncher();

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(FirstRunBubbleLauncher);
};

// static
void FirstRunBubbleLauncher::ShowFirstRunBubbleSoon() {
  SetShowFirstRunBubblePref(first_run::FIRST_RUN_BUBBLE_SHOW);
  // This FirstRunBubbleLauncher instance will manage its own lifetime.
  new FirstRunBubbleLauncher();
}

FirstRunBubbleLauncher::FirstRunBubbleLauncher() {
  registrar_.Add(this, content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
                 content::NotificationService::AllSources());

  // This notification is required to observe the switch between the sync setup
  // page and the general settings page.
  registrar_.Add(this, chrome::NOTIFICATION_WEB_CONTENT_SETTINGS_CHANGED,
                 content::NotificationService::AllSources());
}

FirstRunBubbleLauncher::~FirstRunBubbleLauncher() {}

void FirstRunBubbleLauncher::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(type == content::NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME ||
         type == chrome::NOTIFICATION_WEB_CONTENT_SETTINGS_CHANGED);

  Browser* browser = chrome::FindBrowserWithWebContents(
      content::Source<content::WebContents>(source).ptr());
  if (!browser || !browser->is_type_tabbed())
    return;

  // Check the preference to determine if the bubble should be shown.
  PrefService* prefs = g_browser_process->local_state();
  if (!prefs || prefs->GetInteger(prefs::kShowFirstRunBubbleOption) !=
      first_run::FIRST_RUN_BUBBLE_SHOW) {
    delete this;
    return;
  }

  content::WebContents* contents =
      browser->tab_strip_model()->GetActiveWebContents();

  // Suppress the first run bubble if a Gaia sign in page, the continue
  // URL for the sign in page or the sync setup page is showing.
  if (contents &&
      (gaia::IsGaiaSignonRealm(contents->GetURL().GetOrigin()) ||
       signin::IsContinueUrlForWebBasedSigninFlow(contents->GetURL()) ||
       contents->GetURL() == GURL(std::string(chrome::kChromeUISettingsURL) +
                                  chrome::kSyncSetupSubPage))) {
    return;
  }

  if (contents && contents->GetURL().SchemeIs(chrome::kChromeUIScheme)) {
    // Suppress the first run bubble if 'make chrome metro' flow is showing.
    if (contents->GetURL().host() == chrome::kChromeUIMetroFlowHost)
      return;

    // Suppress the first run bubble if the NTP sync promo bubble is showing
    // or if sign in is in progress.
    if (contents->GetURL().host() == chrome::kChromeUINewTabHost) {
      Profile* profile =
          Profile::FromBrowserContext(contents->GetBrowserContext());
      SigninManagerBase* manager =
          SigninManagerFactory::GetForProfile(profile);
      bool signin_in_progress = manager &&
          (!manager->GetAuthenticatedUsername().empty() &&
              SigninTracker::GetSigninState(profile, NULL) !=
                  SigninTracker::SIGNIN_COMPLETE);
      bool is_promo_bubble_visible =
          profile->GetPrefs()->GetBoolean(prefs::kSignInPromoShowNTPBubble);

      if (is_promo_bubble_visible || signin_in_progress)
        return;
    }
  }

  // Suppress the first run bubble if a global error bubble is pending.
  GlobalErrorService* global_error_service =
      GlobalErrorServiceFactory::GetForProfile(browser->profile());
  if (global_error_service->GetFirstGlobalErrorWithBubbleView() != NULL)
    return;

  // Reset the preference and notifications to avoid showing the bubble again.
  prefs->SetInteger(prefs::kShowFirstRunBubbleOption,
                    first_run::FIRST_RUN_BUBBLE_DONT_SHOW);

  // Show the bubble now and destroy this bubble launcher.
  browser->ShowFirstRunBubble();
  delete this;
}

}  // namespace

namespace first_run {
namespace internal {

FirstRunState first_run_ = FIRST_RUN_UNKNOWN;

static base::LazyInstance<base::FilePath> master_prefs_path_for_testing
    = LAZY_INSTANCE_INITIALIZER;

installer::MasterPreferences*
    LoadMasterPrefs(base::FilePath* master_prefs_path) {
  if (!master_prefs_path_for_testing.Get().empty())
    *master_prefs_path = master_prefs_path_for_testing.Get();
  else
    *master_prefs_path = base::FilePath(MasterPrefsPath());
  if (master_prefs_path->empty())
    return NULL;
  installer::MasterPreferences* install_prefs =
      new installer::MasterPreferences(*master_prefs_path);
  if (!install_prefs->read_from_file()) {
    delete install_prefs;
    return NULL;
  }

  return install_prefs;
}

bool CopyPrefFile(const base::FilePath& user_data_dir,
                  const base::FilePath& master_prefs_path) {
  base::FilePath user_prefs = GetDefaultPrefFilePath(true, user_data_dir);
  if (user_prefs.empty())
    return false;

  // The master prefs are regular prefs so we can just copy the file
  // to the default place and they just work.
  return base::CopyFile(master_prefs_path, user_prefs);
}

void SetupMasterPrefsFromInstallPrefs(
    const installer::MasterPreferences& install_prefs,
    MasterPrefs* out_prefs) {
  ConvertStringVectorToGURLVector(
      install_prefs.GetFirstRunTabs(), &out_prefs->new_tabs);

  install_prefs.GetInt(installer::master_preferences::kDistroPingDelay,
                       &out_prefs->ping_delay);

  bool value = false;
  if (install_prefs.GetBool(
          installer::master_preferences::kDistroImportSearchPref, &value)) {
    if (value) {
      out_prefs->do_import_items |= importer::SEARCH_ENGINES;
    } else {
      out_prefs->dont_import_items |= importer::SEARCH_ENGINES;
    }
  }

  // If we're suppressing the first-run bubble, set that preference now.
  // Otherwise, wait until the user has completed first run to set it, so the
  // user is guaranteed to see the bubble iff he or she has completed the first
  // run process.
  if (install_prefs.GetBool(
          installer::master_preferences::kDistroSuppressFirstRunBubble,
          &value) && value)
    SetShowFirstRunBubblePref(FIRST_RUN_BUBBLE_SUPPRESS);

  if (install_prefs.GetBool(
          installer::master_preferences::kDistroImportHistoryPref,
          &value)) {
    if (value) {
      out_prefs->do_import_items |= importer::HISTORY;
    } else {
      out_prefs->dont_import_items |= importer::HISTORY;
    }
  }

  std::string not_used;
  out_prefs->homepage_defined = install_prefs.GetString(
      prefs::kHomePage, &not_used);

  if (install_prefs.GetBool(
          installer::master_preferences::kDistroImportHomePagePref,
          &value)) {
    if (value) {
      out_prefs->do_import_items |= importer::HOME_PAGE;
    } else {
      out_prefs->dont_import_items |= importer::HOME_PAGE;
    }
  }

  // Bookmarks are never imported unless specifically turned on.
  if (install_prefs.GetBool(
          installer::master_preferences::kDistroImportBookmarksPref,
          &value)) {
    if (value)
      out_prefs->do_import_items |= importer::FAVORITES;
    else
      out_prefs->dont_import_items |= importer::FAVORITES;
  }

  if (install_prefs.GetBool(
          installer::master_preferences::kMakeChromeDefaultForUser,
          &value) && value) {
    out_prefs->make_chrome_default = true;
  }

  if (install_prefs.GetBool(
          installer::master_preferences::kSuppressFirstRunDefaultBrowserPrompt,
          &value) && value) {
    out_prefs->suppress_first_run_default_browser_prompt = true;
  }

  install_prefs.GetString(
      installer::master_preferences::kDistroImportBookmarksFromFilePref,
      &out_prefs->import_bookmarks_path);

  out_prefs->variations_seed = install_prefs.GetVariationsSeed();

  install_prefs.GetString(
      installer::master_preferences::kDistroSuppressDefaultBrowserPromptPref,
      &out_prefs->suppress_default_browser_prompt_for_version);
}

void SetDefaultBrowser(installer::MasterPreferences* install_prefs){
  // Even on the first run we only allow for the user choice to take effect if
  // no policy has been set by the admin.
  if (!g_browser_process->local_state()->IsManagedPreference(
          prefs::kDefaultBrowserSettingEnabled)) {
    bool value = false;
    if (install_prefs->GetBool(
            installer::master_preferences::kMakeChromeDefaultForUser,
            &value) && value) {
      ShellIntegration::SetAsDefaultBrowser();
    }
  } else {
    if (g_browser_process->local_state()->GetBoolean(
            prefs::kDefaultBrowserSettingEnabled)) {
      ShellIntegration::SetAsDefaultBrowser();
    }
  }
}

bool CreateSentinel() {
  base::FilePath first_run_sentinel;
  if (!internal::GetFirstRunSentinelFilePath(&first_run_sentinel))
    return false;
  return file_util::WriteFile(first_run_sentinel, "", 0) != -1;
}

// -- Platform-specific functions --

#if !defined(OS_LINUX) && !defined(OS_BSD)
bool IsOrganicFirstRun() {
  std::string brand;
  google_util::GetBrand(&brand);
  return google_util::IsOrganicFirstRun(brand);
}
#endif

}  // namespace internal

MasterPrefs::MasterPrefs()
    : ping_delay(0),
      homepage_defined(false),
      do_import_items(0),
      dont_import_items(0),
      make_chrome_default(false),
      suppress_first_run_default_browser_prompt(false) {
}

MasterPrefs::~MasterPrefs() {}

bool IsChromeFirstRun() {
  if (internal::first_run_ != internal::FIRST_RUN_UNKNOWN)
    return internal::first_run_ == internal::FIRST_RUN_TRUE;

  internal::first_run_ = internal::FIRST_RUN_FALSE;

  base::FilePath first_run_sentinel;
  const CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kForceFirstRun)) {
    internal::first_run_ = internal::FIRST_RUN_TRUE;
  } else if (command_line->HasSwitch(switches::kCancelFirstRun)) {
    internal::first_run_ = internal::FIRST_RUN_CANCEL;
  } else if (!command_line->HasSwitch(switches::kNoFirstRun) &&
             internal::GetFirstRunSentinelFilePath(&first_run_sentinel) &&
             !base::PathExists(first_run_sentinel)) {
    internal::first_run_ = internal::FIRST_RUN_TRUE;
  }

  return internal::first_run_ == internal::FIRST_RUN_TRUE;
}

bool IsFirstRunSuppressed(const CommandLine& command_line) {
  return command_line.HasSwitch(switches::kCancelFirstRun) ||
      command_line.HasSwitch(switches::kNoFirstRun);
}

void CreateSentinelIfNeeded() {
  if (IsChromeFirstRun() ||
      internal::first_run_ == internal::FIRST_RUN_CANCEL) {
    internal::CreateSentinel();
  }
}

std::string GetPingDelayPrefName() {
  return base::StringPrintf("%s.%s",
                            installer::master_preferences::kDistroDict,
                            installer::master_preferences::kDistroPingDelay);
}

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(
      GetPingDelayPrefName().c_str(),
      0,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

bool RemoveSentinel() {
  base::FilePath first_run_sentinel;
  if (!internal::GetFirstRunSentinelFilePath(&first_run_sentinel))
    return false;
  return base::DeleteFile(first_run_sentinel, false);
}

bool SetShowFirstRunBubblePref(FirstRunBubbleOptions show_bubble_option) {
  PrefService* local_state = g_browser_process->local_state();
  if (!local_state)
    return false;
  if (local_state->GetInteger(
          prefs::kShowFirstRunBubbleOption) != FIRST_RUN_BUBBLE_SUPPRESS) {
    // Set the new state as long as the bubble wasn't explicitly suppressed
    // already.
    local_state->SetInteger(prefs::kShowFirstRunBubbleOption,
                            show_bubble_option);
  }
  return true;
}

void SetShouldShowWelcomePage() {
  g_should_show_welcome_page = true;
}

bool ShouldShowWelcomePage() {
  bool retval = g_should_show_welcome_page;
  g_should_show_welcome_page = false;
  return retval;
}

void SetShouldDoPersonalDataManagerFirstRun() {
  g_should_do_autofill_personal_data_manager_first_run = true;
}

bool ShouldDoPersonalDataManagerFirstRun() {
  bool retval = g_should_do_autofill_personal_data_manager_first_run;
  g_should_do_autofill_personal_data_manager_first_run = false;
  return retval;
}

void LogFirstRunMetric(FirstRunBubbleMetric metric) {
  UMA_HISTOGRAM_ENUMERATION("FirstRun.SearchEngineBubble", metric,
                            NUM_FIRST_RUN_BUBBLE_METRICS);
}

void SetMasterPrefsPathForTesting(const base::FilePath& master_prefs) {
  internal::master_prefs_path_for_testing.Get() = master_prefs;
}

ProcessMasterPreferencesResult ProcessMasterPreferences(
    const base::FilePath& user_data_dir,
    MasterPrefs* out_prefs) {
  DCHECK(!user_data_dir.empty());

  base::FilePath master_prefs_path;
  scoped_ptr<installer::MasterPreferences>
      install_prefs(internal::LoadMasterPrefs(&master_prefs_path));

  // Default value in case master preferences is missing or corrupt, or
  // ping_delay is missing.
  out_prefs->ping_delay = 90;
  if (install_prefs.get()) {
    if (!internal::ShowPostInstallEULAIfNeeded(install_prefs.get()))
      return EULA_EXIT_NOW;

    if (!internal::CopyPrefFile(user_data_dir, master_prefs_path))
      DLOG(ERROR) << "Failed to copy master_preferences to user data dir.";

    DoDelayedInstallExtensionsIfNeeded(install_prefs.get());

    internal::SetupMasterPrefsFromInstallPrefs(*install_prefs, out_prefs);

    internal::SetDefaultBrowser(install_prefs.get());
  }

  return FIRST_RUN_PROCEED;
}

void AutoImport(
    Profile* profile,
    bool homepage_defined,
    int import_items,
    int dont_import_items,
    const std::string& import_bookmarks_path) {
  // Deletes itself.
  ExternalProcessImporterHost* importer_host = new ExternalProcessImporterHost;

  base::FilePath local_state_path;
  PathService::Get(chrome::FILE_LOCAL_STATE, &local_state_path);
  bool local_state_file_exists = base::PathExists(local_state_path);

  scoped_refptr<ImporterList> importer_list(new ImporterList());
  importer_list->DetectSourceProfilesHack(
      g_browser_process->GetApplicationLocale());

  // Do import if there is an available profile for us to import.
  if (importer_list->count() > 0) {
    // Don't show the warning dialog if import fails.
    importer_host->set_headless();
    int items = 0;

    if (internal::IsOrganicFirstRun()) {
      // Home page is imported in organic builds only unless turned off or
      // defined in master_preferences.
      if (homepage_defined) {
        dont_import_items |= importer::HOME_PAGE;
        if (import_items & importer::HOME_PAGE)
          import_items &= ~importer::HOME_PAGE;
      }
      // Search engines are not imported automatically in organic builds if the
      // user already has a user preferences directory.
      if (local_state_file_exists) {
        dont_import_items |= importer::SEARCH_ENGINES;
        if (import_items & importer::SEARCH_ENGINES)
          import_items &= ~importer::SEARCH_ENGINES;
      }
    }

    PrefService* user_prefs = profile->GetPrefs();

    SetImportItem(user_prefs,
                  prefs::kImportHistory,
                  import_items,
                  dont_import_items,
                  importer::HISTORY,
                  &items);
    SetImportItem(user_prefs,
                  prefs::kImportHomepage,
                  import_items,
                  dont_import_items,
                  importer::HOME_PAGE,
                  &items);
    SetImportItem(user_prefs,
                  prefs::kImportSearchEngine,
                  import_items,
                  dont_import_items,
                  importer::SEARCH_ENGINES,
                  &items);
    SetImportItem(user_prefs,
                  prefs::kImportBookmarks,
                  import_items,
                  dont_import_items,
                  importer::FAVORITES,
                  &items);

    importer::LogImporterUseToMetrics(
        "AutoImport", importer_list->GetSourceProfileAt(0).importer_type);

    ImportSettings(profile, importer_host, importer_list, items);
  }

  if (!import_bookmarks_path.empty()) {
    // Deletes itself.
    ExternalProcessImporterHost* file_importer_host =
        new ExternalProcessImporterHost;
    file_importer_host->set_headless();

    ImportFromFile(profile, file_importer_host, import_bookmarks_path);
  }

  content::RecordAction(UserMetricsAction("FirstRunDef_Accept"));

  g_auto_import_state |= AUTO_IMPORT_CALLED;
}

void DoPostImportTasks(Profile* profile, bool make_chrome_default) {
  if (make_chrome_default &&
      ShellIntegration::CanSetAsDefaultBrowser() ==
          ShellIntegration::SET_DEFAULT_UNATTENDED) {
    ShellIntegration::SetAsDefaultBrowser();
  }

  // Display the first run bubble if there is a default search provider.
  TemplateURLService* template_url =
      TemplateURLServiceFactory::GetForProfile(profile);
  if (template_url && template_url->GetDefaultSearchProvider())
    FirstRunBubbleLauncher::ShowFirstRunBubbleSoon();
  SetShouldShowWelcomePage();
  SetShouldDoPersonalDataManagerFirstRun();

  internal::DoPostImportPlatformSpecificTasks(profile);
}

uint16 auto_import_state() {
  return g_auto_import_state;
}

}  // namespace first_run
