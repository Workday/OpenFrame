// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_service_unittest.h"

#include <algorithm>
#include <set>
#include <vector>

#include "base/at_exit.h"
#include "base/basictypes.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/files/file_enumerator.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_file_value_serializer.h"
#include "base/json/json_reader.h"
#include "base/json/json_string_value_serializer.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/stl_util.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/version.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/app_sync_data.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/extensions/default_apps.h"
#include "chrome/browser/extensions/extension_creator.h"
#include "chrome/browser/extensions/extension_error_reporter.h"
#include "chrome/browser/extensions/extension_error_ui.h"
#include "chrome/browser/extensions/extension_notification_observer.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_sorting.h"
#include "chrome/browser/extensions/extension_special_storage_policy.h"
#include "chrome/browser/extensions/extension_sync_data.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/external_install_ui.h"
#include "chrome/browser/extensions/external_policy_loader.h"
#include "chrome/browser/extensions/external_pref_loader.h"
#include "chrome/browser/extensions/external_provider_impl.h"
#include "chrome/browser/extensions/external_provider_interface.h"
#include "chrome/browser/extensions/install_observer.h"
#include "chrome/browser/extensions/install_tracker.h"
#include "chrome/browser/extensions/install_tracker_factory.h"
#include "chrome/browser/extensions/installed_loader.h"
#include "chrome/browser/extensions/management_policy.h"
#include "chrome/browser/extensions/pack_extension_job.h"
#include "chrome/browser/extensions/pending_extension_info.h"
#include "chrome/browser/extensions/pending_extension_manager.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/extensions/test_management_policy.h"
#include "chrome/browser/extensions/unpacked_installer.h"
#include "chrome/browser/extensions/updater/extension_updater.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/browser/prefs/pref_service_mock_builder.h"
#include "chrome/browser/prefs/pref_service_syncable.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/api/plugins/plugins_handler.h"
#include "chrome/common/extensions/background_info.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_builder.h"
#include "chrome/common/extensions/extension_l10n_util.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "chrome/common/extensions/manifest_handlers/content_scripts_handler.h"
#include "chrome/common/extensions/manifest_url_handler.h"
#include "chrome/common/extensions/permissions/permission_set.h"
#include "chrome/common/extensions/value_builder.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/testing_profile.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/dom_storage_context.h"
#include "content/public/browser/gpu_data_manager.h"
#include "content/public/browser/indexed_db_context.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_constants.h"
#include "content/public/test/test_utils.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension_resource.h"
#include "extensions/common/url_pattern.h"
#include "gpu/config/gpu_info.h"
#include "grit/browser_resources.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_monster.h"
#include "net/cookies/cookie_options.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "sync/api/string_ordinal.h"
#include "sync/api/sync_error_factory.h"
#include "sync/api/sync_error_factory_mock.h"
#include "sync/api/syncable_service.h"
#include "sync/protocol/app_specifics.pb.h"
#include "sync/protocol/extension_specifics.pb.h"
#include "sync/protocol/sync.pb.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"
#include "url/gurl.h"
#include "webkit/browser/database/database_tracker.h"
#include "webkit/browser/quota/quota_manager.h"
#include "webkit/common/database/database_identifier.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/extensions/install_limiter.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#endif

using base::DictionaryValue;
using base::ListValue;
using base::Value;
using content::BrowserContext;
using content::BrowserThread;
using content::DOMStorageContext;
using content::IndexedDBContext;
using content::PluginService;
using extensions::APIPermission;
using extensions::APIPermissionSet;
using extensions::CrxInstaller;
using extensions::Extension;
using extensions::ExtensionCreator;
using extensions::ExtensionPrefs;
using extensions::ExtensionResource;
using extensions::ExtensionSystem;
using extensions::FeatureSwitch;
using extensions::Manifest;
using extensions::PermissionSet;
using extensions::TestExtensionSystem;
using extensions::URLPatternSet;

namespace keys = extension_manifest_keys;

namespace {

// Extension ids used during testing.
const char* const all_zero = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* const zero_n_one = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab";
const char* const good0 = "behllobkkfkfnphdnhnkndlbkcpglgmj";
const char* const good1 = "hpiknbiabeeppbpihjehijgoemciehgk";
const char* const good2 = "bjafgdebaacbbbecmhlhpofkepfkgcpa";
const char* const good_crx = "ldnnhddmnhbkjipkidpdiheffobcpfmf";
const char* const hosted_app = "kbmnembihfiondgfjekmnmcbddelicoi";
const char* const page_action = "obcimlgaoabeegjmmpldobjndiealpln";
const char* const theme_crx = "iamefpfkojoapidjnbafmgkgncegbkad";
const char* const theme2_crx = "pjpgmfcmabopnnfonnhmdjglfpjjfkbf";
const char* const permissions_crx = "eagpmdpfmaekmmcejjbmjoecnejeiiin";
const char* const unpacked = "cbcdidchbppangcjoddlpdjlenngjldk";
const char* const updates_from_webstore = "akjooamlhcgeopfifcmlggaebeocgokj";

struct ExtensionsOrder {
  bool operator()(const scoped_refptr<const Extension>& a,
                  const scoped_refptr<const Extension>& b) {
    return a->name() < b->name();
  }
};

static std::vector<string16> GetErrors() {
  const std::vector<string16>* errors =
      ExtensionErrorReporter::GetInstance()->GetErrors();
  std::vector<string16> ret_val;

  for (std::vector<string16>::const_iterator iter = errors->begin();
       iter != errors->end(); ++iter) {
    std::string utf8_error = UTF16ToUTF8(*iter);
    if (utf8_error.find(".svn") == std::string::npos) {
      ret_val.push_back(*iter);
    }
  }

  // The tests rely on the errors being in a certain order, which can vary
  // depending on how filesystem iteration works.
  std::stable_sort(ret_val.begin(), ret_val.end());

  return ret_val;
}

static void AddPattern(URLPatternSet* extent, const std::string& pattern) {
  int schemes = URLPattern::SCHEME_ALL;
  extent->AddPattern(URLPattern(schemes, pattern));
}

base::FilePath GetTemporaryFile() {
  base::FilePath temp_file;
  CHECK(file_util::CreateTemporaryFile(&temp_file));
  return temp_file;
}


bool WaitForCountNotificationsCallback(int *count) {
  return --(*count) == 0;
}

}  // namespace

class MockExtensionProvider : public extensions::ExternalProviderInterface {
 public:
  MockExtensionProvider(
      VisitorInterface* visitor,
      Manifest::Location location)
    : location_(location), visitor_(visitor), visit_count_(0) {
  }

  virtual ~MockExtensionProvider() {}

  void UpdateOrAddExtension(const std::string& id,
                            const std::string& version,
                            const base::FilePath& path) {
    extension_map_[id] = std::make_pair(version, path);
  }

  void RemoveExtension(const std::string& id) {
    extension_map_.erase(id);
  }

  // ExternalProvider implementation:
  virtual void VisitRegisteredExtension() OVERRIDE {
    visit_count_++;
    for (DataMap::const_iterator i = extension_map_.begin();
         i != extension_map_.end(); ++i) {
      Version version(i->second.first);

      visitor_->OnExternalExtensionFileFound(
          i->first, &version, i->second.second, location_,
          Extension::NO_FLAGS, false);
    }
    visitor_->OnExternalProviderReady(this);
  }

  virtual bool HasExtension(const std::string& id) const OVERRIDE {
    return extension_map_.find(id) != extension_map_.end();
  }

  virtual bool GetExtensionDetails(
      const std::string& id,
      Manifest::Location* location,
      scoped_ptr<Version>* version) const OVERRIDE {
    DataMap::const_iterator it = extension_map_.find(id);
    if (it == extension_map_.end())
      return false;

    if (version)
      version->reset(new Version(it->second.first));

    if (location)
      *location = location_;

    return true;
  }

  virtual bool IsReady() const OVERRIDE {
    return true;
  }

  virtual void ServiceShutdown() OVERRIDE {
  }

  int visit_count() const { return visit_count_; }
  void set_visit_count(int visit_count) {
    visit_count_ = visit_count;
  }

 private:
  typedef std::map< std::string, std::pair<std::string, base::FilePath> >
      DataMap;
  DataMap extension_map_;
  Manifest::Location location_;
  VisitorInterface* visitor_;

  // visit_count_ tracks the number of calls to VisitRegisteredExtension().
  // Mutable because it must be incremented on each call to
  // VisitRegisteredExtension(), which must be a const method to inherit
  // from the class being mocked.
  mutable int visit_count_;

  DISALLOW_COPY_AND_ASSIGN(MockExtensionProvider);
};

class MockProviderVisitor
    : public extensions::ExternalProviderInterface::VisitorInterface {
 public:
  // The provider will return |fake_base_path| from
  // GetBaseCrxFilePath().  User can test the behavior with
  // and without an empty path using this parameter.
  explicit MockProviderVisitor(base::FilePath fake_base_path)
      : ids_found_(0),
        fake_base_path_(fake_base_path),
        expected_creation_flags_(Extension::NO_FLAGS) {
    profile_.reset(new TestingProfile);
  }

  MockProviderVisitor(base::FilePath fake_base_path,
                      int expected_creation_flags)
      : ids_found_(0),
        fake_base_path_(fake_base_path),
        expected_creation_flags_(expected_creation_flags) {
  }

  int Visit(const std::string& json_data) {
    // Give the test json file to the provider for parsing.
    provider_.reset(new extensions::ExternalProviderImpl(
        this,
        new extensions::ExternalTestingLoader(json_data, fake_base_path_),
        profile_.get(),
        Manifest::EXTERNAL_PREF,
        Manifest::EXTERNAL_PREF_DOWNLOAD,
        Extension::NO_FLAGS));

    // We also parse the file into a dictionary to compare what we get back
    // from the provider.
    JSONStringValueSerializer serializer(json_data);
    Value* json_value = serializer.Deserialize(NULL, NULL);

    if (!json_value || !json_value->IsType(Value::TYPE_DICTIONARY)) {
      NOTREACHED() << "Unable to deserialize json data";
      return -1;
    } else {
      DictionaryValue* external_extensions =
          static_cast<DictionaryValue*>(json_value);
      prefs_.reset(external_extensions);
    }

    // Reset our counter.
    ids_found_ = 0;
    // Ask the provider to look up all extensions and return them.
    provider_->VisitRegisteredExtension();

    return ids_found_;
  }

  virtual bool OnExternalExtensionFileFound(const std::string& id,
                                            const Version* version,
                                            const base::FilePath& path,
                                            Manifest::Location unused,
                                            int creation_flags,
                                            bool mark_acknowledged) OVERRIDE {
    EXPECT_EQ(expected_creation_flags_, creation_flags);

    ++ids_found_;
    DictionaryValue* pref;
    // This tests is to make sure that the provider only notifies us of the
    // values we gave it. So if the id we doesn't exist in our internal
    // dictionary then something is wrong.
    EXPECT_TRUE(prefs_->GetDictionary(id, &pref))
       << "Got back ID (" << id.c_str() << ") we weren't expecting";

    EXPECT_TRUE(path.IsAbsolute());
    if (!fake_base_path_.empty())
      EXPECT_TRUE(fake_base_path_.IsParent(path));

    if (pref) {
      EXPECT_TRUE(provider_->HasExtension(id));

      // Ask provider if the extension we got back is registered.
      Manifest::Location location = Manifest::INVALID_LOCATION;
      scoped_ptr<Version> v1;
      base::FilePath crx_path;

      EXPECT_TRUE(provider_->GetExtensionDetails(id, NULL, &v1));
      EXPECT_STREQ(version->GetString().c_str(), v1->GetString().c_str());

      scoped_ptr<Version> v2;
      EXPECT_TRUE(provider_->GetExtensionDetails(id, &location, &v2));
      EXPECT_STREQ(version->GetString().c_str(), v1->GetString().c_str());
      EXPECT_STREQ(version->GetString().c_str(), v2->GetString().c_str());
      EXPECT_EQ(Manifest::EXTERNAL_PREF, location);

      // Remove it so we won't count it ever again.
      prefs_->Remove(id, NULL);
    }
    return true;
  }

  virtual bool OnExternalExtensionUpdateUrlFound(
      const std::string& id, const GURL& update_url,
      Manifest::Location location) OVERRIDE {
    ++ids_found_;
    DictionaryValue* pref;
    // This tests is to make sure that the provider only notifies us of the
    // values we gave it. So if the id we doesn't exist in our internal
    // dictionary then something is wrong.
    EXPECT_TRUE(prefs_->GetDictionary(id, &pref))
       << L"Got back ID (" << id.c_str() << ") we weren't expecting";
    EXPECT_EQ(Manifest::EXTERNAL_PREF_DOWNLOAD, location);

    if (pref) {
      EXPECT_TRUE(provider_->HasExtension(id));

      // External extensions with update URLs do not have versions.
      scoped_ptr<Version> v1;
      Manifest::Location location1 = Manifest::INVALID_LOCATION;
      EXPECT_TRUE(provider_->GetExtensionDetails(id, &location1, &v1));
      EXPECT_FALSE(v1.get());
      EXPECT_EQ(Manifest::EXTERNAL_PREF_DOWNLOAD, location1);

      // Remove it so we won't count it again.
      prefs_->Remove(id, NULL);
    }
    return true;
  }

  virtual void OnExternalProviderReady(
      const extensions::ExternalProviderInterface* provider) OVERRIDE {
    EXPECT_EQ(provider, provider_.get());
    EXPECT_TRUE(provider->IsReady());
  }

 private:
  int ids_found_;
  base::FilePath fake_base_path_;
  int expected_creation_flags_;
  scoped_ptr<extensions::ExternalProviderImpl> provider_;
  scoped_ptr<DictionaryValue> prefs_;
  scoped_ptr<TestingProfile> profile_;

  DISALLOW_COPY_AND_ASSIGN(MockProviderVisitor);
};

ExtensionServiceTestBase::ExtensionServiceInitParams::
ExtensionServiceInitParams()
 : autoupdate_enabled(false), is_first_run(true) {
}

// Our message loop may be used in tests which require it to be an IO loop.
ExtensionServiceTestBase::ExtensionServiceTestBase()
    : thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP),
      service_(NULL),
      management_policy_(NULL),
      expected_extensions_count_(0) {
  base::FilePath test_data_dir;
  if (!PathService::Get(chrome::DIR_TEST_DATA, &test_data_dir)) {
    ADD_FAILURE();
    return;
  }
  data_dir_ = test_data_dir.AppendASCII("extensions");
}

ExtensionServiceTestBase::~ExtensionServiceTestBase() {
  service_ = NULL;
}

void ExtensionServiceTestBase::InitializeExtensionService(
    const ExtensionServiceTestBase::ExtensionServiceInitParams& params) {
  TestingProfile::Builder profile_builder;
  // Create a PrefService that only contains user defined preference values.
  PrefServiceMockBuilder builder;
  // If pref_file is empty, TestingProfile automatically creates
  // TestingPrefServiceSyncable instance.
  if (!params.pref_file.empty()) {
    builder.WithUserFilePrefs(params.pref_file,
                              base::MessageLoopProxy::current().get());
    scoped_refptr<user_prefs::PrefRegistrySyncable> registry(
        new user_prefs::PrefRegistrySyncable);
    scoped_ptr<PrefServiceSyncable> prefs(
        builder.CreateSyncable(registry.get()));
    chrome::RegisterUserProfilePrefs(registry.get());
    profile_builder.SetPrefService(prefs.Pass());
  }
  profile_builder.SetPath(params.profile_path);
  profile_ = profile_builder.Build();

  TestExtensionSystem* system = static_cast<TestExtensionSystem*>(
      ExtensionSystem::Get(profile_.get()));
  if (!params.is_first_run) {
    ExtensionPrefs* prefs = system->CreateExtensionPrefs(
        CommandLine::ForCurrentProcess(),
        params.extensions_install_dir);
    prefs->SetAlertSystemFirstRun();
  }

  service_ = system->CreateExtensionService(
      CommandLine::ForCurrentProcess(),
      params.extensions_install_dir,
      params.autoupdate_enabled);
  service_->SetFileTaskRunnerForTesting(
      base::MessageLoopProxy::current().get());
  service_->set_extensions_enabled(true);
  service_->set_show_extensions_prompts(false);
  service_->set_install_updates_when_idle_for_test(false);

  management_policy_ =
      ExtensionSystem::Get(profile_.get())->management_policy();

  // When we start up, we want to make sure there is no external provider,
  // since the ExtensionService on Windows will use the Registry as a default
  // provider and if there is something already registered there then it will
  // interfere with the tests. Those tests that need an external provider
  // will register one specifically.
  service_->ClearProvidersForTesting();

#if defined(OS_CHROMEOS)
  extensions::InstallLimiter::Get(profile_.get())->DisableForTest();
#endif

  expected_extensions_count_ = 0;
}

void ExtensionServiceTestBase::InitializeInstalledExtensionService(
    const base::FilePath& prefs_file,
    const base::FilePath& source_install_dir) {
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  base::FilePath path = temp_dir_.path();
  path = path.Append(FILE_PATH_LITERAL("TestingExtensionsPath"));
  base::DeleteFile(path, true);
  file_util::CreateDirectory(path);
  base::FilePath temp_prefs = path.Append(FILE_PATH_LITERAL("Preferences"));
  base::CopyFile(prefs_file, temp_prefs);

  extensions_install_dir_ = path.Append(FILE_PATH_LITERAL("Extensions"));
  base::DeleteFile(extensions_install_dir_, true);
  base::CopyDirectory(source_install_dir, extensions_install_dir_, true);

  ExtensionServiceInitParams params;
  params.profile_path = path;
  params.pref_file = temp_prefs;
  params.extensions_install_dir = extensions_install_dir_;
  InitializeExtensionService(params);
}

void ExtensionServiceTestBase::InitializeEmptyExtensionService() {
  InitializeExtensionServiceHelper(false, true);
}

void ExtensionServiceTestBase::InitializeExtensionProcessManager() {
  static_cast<extensions::TestExtensionSystem*>(
      ExtensionSystem::Get(profile_.get()))->
      CreateExtensionProcessManager();
}

void ExtensionServiceTestBase::InitializeExtensionServiceWithUpdater() {
  InitializeExtensionServiceHelper(true, true);
  service_->updater()->Start();
}

void ExtensionServiceTestBase::InitializeExtensionServiceHelper(
    bool autoupdate_enabled, bool is_first_run) {
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  base::FilePath path = temp_dir_.path();
  path = path.Append(FILE_PATH_LITERAL("TestingExtensionsPath"));
  base::DeleteFile(path, true);
  file_util::CreateDirectory(path);
  base::FilePath prefs_filename =
      path.Append(FILE_PATH_LITERAL("TestPreferences"));
  extensions_install_dir_ = path.Append(FILE_PATH_LITERAL("Extensions"));
  base::DeleteFile(extensions_install_dir_, true);
  file_util::CreateDirectory(extensions_install_dir_);

  ExtensionServiceInitParams params;
  params.profile_path = path;
  params.pref_file = prefs_filename;
  params.extensions_install_dir = extensions_install_dir_;
  params.autoupdate_enabled = autoupdate_enabled;
  params.is_first_run = is_first_run;
  InitializeExtensionService(params);
}

// static
void ExtensionServiceTestBase::SetUpTestCase() {
  ExtensionErrorReporter::Init(false);  // no noisy errors
}

void ExtensionServiceTestBase::SetUp() {
  ExtensionErrorReporter::GetInstance()->ClearErrors();
  content::RenderProcessHost::SetRunRendererInProcess(true);
}

void ExtensionServiceTestBase::TearDown() {
  content::RenderProcessHost::SetRunRendererInProcess(false);
}

class ExtensionServiceTest
  : public ExtensionServiceTestBase, public content::NotificationObserver {
 public:
  ExtensionServiceTest()
      : installed_(NULL),
        was_update_(false),
        override_external_install_prompt_(
            FeatureSwitch::prompt_for_external_extensions(), false) {
    registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_LOADED,
                   content::NotificationService::AllSources());
    registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNLOADED,
                   content::NotificationService::AllSources());
    registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_INSTALLED,
                   content::NotificationService::AllSources());
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    switch (type) {
      case chrome::NOTIFICATION_EXTENSION_LOADED: {
        const Extension* extension =
            content::Details<const Extension>(details).ptr();
        loaded_.push_back(make_scoped_refptr(extension));
        // The tests rely on the errors being in a certain order, which can vary
        // depending on how filesystem iteration works.
        std::stable_sort(loaded_.begin(), loaded_.end(), ExtensionsOrder());
        break;
      }

      case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
        const Extension* e =
            content::Details<extensions::UnloadedExtensionInfo>(
                details)->extension;
        unloaded_id_ = e->id();
        extensions::ExtensionList::iterator i =
            std::find(loaded_.begin(), loaded_.end(), e);
        // TODO(erikkay) fix so this can be an assert.  Right now the tests
        // are manually calling clear() on loaded_, so this isn't doable.
        if (i == loaded_.end())
          return;
        loaded_.erase(i);
        break;
      }
      case chrome::NOTIFICATION_EXTENSION_INSTALLED: {
        const extensions::InstalledExtensionInfo* installed_info =
            content::Details<const extensions::InstalledExtensionInfo>(details)
                .ptr();
        installed_ = installed_info->extension;
        was_update_ = installed_info->is_update;
        old_name_ = installed_info->old_name;
        break;
      }

      default:
        DCHECK(false);
    }
  }

  void AddMockExternalProvider(
      extensions::ExternalProviderInterface* provider) {
    service_->AddProviderForTesting(provider);
  }

  void MockSyncStartFlare(bool* was_called,
                          syncer::ModelType* model_type_passed_in,
                          syncer::ModelType model_type) {
    *was_called = true;
    *model_type_passed_in = model_type;
  }

 protected:
  void TestExternalProvider(MockExtensionProvider* provider,
                            Manifest::Location location);

  void PackCRX(const base::FilePath& dir_path,
               const base::FilePath& pem_path,
               const base::FilePath& crx_path) {
    // Use the existing pem key, if provided.
    base::FilePath pem_output_path;
    if (pem_path.value().empty()) {
      pem_output_path = crx_path.DirName().AppendASCII("temp.pem");
    } else {
      ASSERT_TRUE(base::PathExists(pem_path));
    }

    ASSERT_TRUE(base::DeleteFile(crx_path, false));

    scoped_ptr<ExtensionCreator> creator(new ExtensionCreator());
    ASSERT_TRUE(creator->Run(dir_path,
                             crx_path,
                             pem_path,
                             pem_output_path,
                             ExtensionCreator::kOverwriteCRX));

    ASSERT_TRUE(base::PathExists(crx_path));
  }

  // Create a CrxInstaller and start installation. To allow the install
  // to happen, use base::RunLoop().RunUntilIdle();. Most tests will not use
  // this method directly.  Instead, use InstallCrx(), which waits for
  // the crx to be installed and does extra error checking.
  void StartCRXInstall(const base::FilePath& crx_path) {
    StartCRXInstall(crx_path, Extension::NO_FLAGS);
  }

  void StartCRXInstall(const base::FilePath& crx_path, int creation_flags) {
    ASSERT_TRUE(base::PathExists(crx_path))
        << "Path does not exist: "<< crx_path.value().c_str();
    scoped_refptr<CrxInstaller> installer(CrxInstaller::CreateSilent(service_));
    installer->set_creation_flags(creation_flags);
    if (!(creation_flags & Extension::WAS_INSTALLED_BY_DEFAULT)) {
      installer->set_allow_silent_install(true);
    }

    content::WindowedNotificationObserver windowed_observer(
        chrome::NOTIFICATION_CRX_INSTALLER_DONE,
        content::Source<extensions::CrxInstaller>(installer));

    installer->InstallCrx(crx_path);
  }

  enum InstallState {
    INSTALL_FAILED,
    INSTALL_UPDATED,
    INSTALL_NEW,
    INSTALL_WITHOUT_LOAD,
  };

  const Extension* PackAndInstallCRX(const base::FilePath& dir_path,
                                     const base::FilePath& pem_path,
                                     InstallState install_state,
                                     int creation_flags) {
    base::FilePath crx_path;
    base::ScopedTempDir temp_dir;
    EXPECT_TRUE(temp_dir.CreateUniqueTempDir());
    crx_path = temp_dir.path().AppendASCII("temp.crx");

    PackCRX(dir_path, pem_path, crx_path);
    return InstallCRX(crx_path, install_state, creation_flags);
  }

  const Extension* PackAndInstallCRX(const base::FilePath& dir_path,
                                     const base::FilePath& pem_path,
                                     InstallState install_state) {
    return PackAndInstallCRX(dir_path, pem_path, install_state,
                             Extension::NO_FLAGS);
  }

  const Extension* PackAndInstallCRX(const base::FilePath& dir_path,
                                     InstallState install_state) {
    return PackAndInstallCRX(dir_path, base::FilePath(), install_state,
                             Extension::NO_FLAGS);
  }

  // Attempts to install an extension. Use INSTALL_FAILED if the installation
  // is expected to fail.
  // If |install_state| is INSTALL_UPDATED, and |expected_old_name| is
  // non-empty, expects that the existing extension's title was
  // |expected_old_name|.
  const Extension* InstallCRX(const base::FilePath& path,
                              InstallState install_state,
                              int creation_flags,
                              const std::string& expected_old_name) {
    StartCRXInstall(path, creation_flags);
    return WaitForCrxInstall(path, install_state, expected_old_name);
  }

  // Attempts to install an extension. Use INSTALL_FAILED if the installation
  // is expected to fail.
  const Extension* InstallCRX(const base::FilePath& path,
                              InstallState install_state,
                              int creation_flags) {
    return InstallCRX(path, install_state, creation_flags, "");
  }

  // Attempts to install an extension. Use INSTALL_FAILED if the installation
  // is expected to fail.
  const Extension* InstallCRX(const base::FilePath& path,
                              InstallState install_state) {
    return InstallCRX(path, install_state, Extension::NO_FLAGS);
  }

  const Extension* InstallCRXFromWebStore(const base::FilePath& path,
                                          InstallState install_state) {
    StartCRXInstall(path, Extension::FROM_WEBSTORE);
    return WaitForCrxInstall(path, install_state);
  }

  const Extension* InstallCRXWithLocation(const base::FilePath& crx_path,
                                          Manifest::Location install_location,
                                          InstallState install_state) {
    EXPECT_TRUE(base::PathExists(crx_path))
        << "Path does not exist: "<< crx_path.value().c_str();
    // no client (silent install)
    scoped_refptr<CrxInstaller> installer(CrxInstaller::CreateSilent(service_));
    installer->set_install_source(install_location);
    installer->InstallCrx(crx_path);

    return WaitForCrxInstall(crx_path, install_state);
  }

  // Wait for a CrxInstaller to finish. Used by InstallCRX. Set the
  // |install_state| to INSTALL_FAILED if the installation is expected to fail.
  // Returns an Extension pointer if the install succeeded, NULL otherwise.
  const Extension* WaitForCrxInstall(const base::FilePath& path,
                                     InstallState install_state) {
    return WaitForCrxInstall(path, install_state, "");
  }

  // Wait for a CrxInstaller to finish. Used by InstallCRX. Set the
  // |install_state| to INSTALL_FAILED if the installation is expected to fail.
  // If |install_state| is INSTALL_UPDATED, and |expected_old_name| is
  // non-empty, expects that the existing extension's title was
  // |expected_old_name|.
  // Returns an Extension pointer if the install succeeded, NULL otherwise.
  const Extension* WaitForCrxInstall(const base::FilePath& path,
                                     InstallState install_state,
                                     const std::string& expected_old_name) {
    content::WindowedNotificationObserver(
        chrome::NOTIFICATION_CRX_INSTALLER_DONE,
        content::NotificationService::AllSources()).Wait();

    std::vector<string16> errors = GetErrors();
    const Extension* extension = NULL;
    if (install_state != INSTALL_FAILED) {
      if (install_state == INSTALL_NEW)
        ++expected_extensions_count_;

      EXPECT_TRUE(installed_) << path.value();
      // If and only if INSTALL_UPDATED, it should have the is_update flag.
      EXPECT_EQ(install_state == INSTALL_UPDATED, was_update_)
          << path.value();
      // If INSTALL_UPDATED, old_name_ should match the given string.
      if (install_state == INSTALL_UPDATED && !expected_old_name.empty())
        EXPECT_EQ(expected_old_name, old_name_);
      EXPECT_EQ(0u, errors.size()) << path.value();

      if (install_state == INSTALL_WITHOUT_LOAD) {
        EXPECT_EQ(0u, loaded_.size()) << path.value();
      } else {
        EXPECT_EQ(1u, loaded_.size()) << path.value();
        EXPECT_EQ(expected_extensions_count_, service_->extensions()->size()) <<
            path.value();
        extension = loaded_[0].get();
        EXPECT_TRUE(service_->GetExtensionById(extension->id(), false))
            << path.value();
      }

      for (std::vector<string16>::iterator err = errors.begin();
        err != errors.end(); ++err) {
        LOG(ERROR) << *err;
      }
    } else {
      EXPECT_FALSE(installed_) << path.value();
      EXPECT_EQ(0u, loaded_.size()) << path.value();
      EXPECT_EQ(1u, errors.size()) << path.value();
    }

    installed_ = NULL;
    was_update_ = false;
    old_name_ = "";
    loaded_.clear();
    ExtensionErrorReporter::GetInstance()->ClearErrors();
    return extension;
  }

  enum UpdateState {
    FAILED_SILENTLY,
    FAILED,
    UPDATED,
    INSTALLED,
    ENABLED
  };

  void BlackListWebGL() {
    static const std::string json_blacklist =
      "{\n"
      "  \"name\": \"gpu blacklist\",\n"
      "  \"version\": \"1.0\",\n"
      "  \"entries\": [\n"
      "    {\n"
      "      \"id\": 1,\n"
      "      \"features\": [\"webgl\"]\n"
      "    }\n"
      "  ]\n"
      "}";
    gpu::GPUInfo gpu_info;
    content::GpuDataManager::GetInstance()->InitializeForTesting(
        json_blacklist, gpu_info);
  }

  void UpdateExtension(const std::string& id, const base::FilePath& in_path,
                       UpdateState expected_state) {
    ASSERT_TRUE(base::PathExists(in_path));

    // We need to copy this to a temporary location because Update() will delete
    // it.
    base::FilePath path = temp_dir_.path();
    path = path.Append(in_path.BaseName());
    ASSERT_TRUE(base::CopyFile(in_path, path));

    int previous_enabled_extension_count =
        service_->extensions()->size();
    int previous_installed_extension_count =
        previous_enabled_extension_count +
        service_->disabled_extensions()->size();

    extensions::CrxInstaller* installer = NULL;
    service_->UpdateExtension(id, path, GURL(), &installer);

    if (installer) {
      content::WindowedNotificationObserver(
          chrome::NOTIFICATION_CRX_INSTALLER_DONE,
          content::Source<extensions::CrxInstaller>(installer)).Wait();
    } else {
      base::RunLoop().RunUntilIdle();
    }

    std::vector<string16> errors = GetErrors();
    int error_count = errors.size();
    int enabled_extension_count =
        service_->extensions()->size();
    int installed_extension_count =
        enabled_extension_count + service_->disabled_extensions()->size();

    int expected_error_count = (expected_state == FAILED) ? 1 : 0;
    EXPECT_EQ(expected_error_count, error_count) << path.value();

    if (expected_state <= FAILED) {
      EXPECT_EQ(previous_enabled_extension_count,
                enabled_extension_count);
      EXPECT_EQ(previous_installed_extension_count,
                installed_extension_count);
    } else {
      int expected_installed_extension_count =
          (expected_state >= INSTALLED) ? 1 : 0;
      int expected_enabled_extension_count =
          (expected_state >= ENABLED) ? 1 : 0;
      EXPECT_EQ(expected_installed_extension_count,
                installed_extension_count);
      EXPECT_EQ(expected_enabled_extension_count,
                enabled_extension_count);
    }

    // Update() should the temporary input file.
    EXPECT_FALSE(base::PathExists(path));
  }

  void TerminateExtension(const std::string& id) {
    const Extension* extension = service_->GetInstalledExtension(id);
    if (!extension) {
      ADD_FAILURE();
      return;
    }
    service_->TrackTerminatedExtensionForTest(extension);
  }

  size_t GetPrefKeyCount() {
    const DictionaryValue* dict =
        profile_->GetPrefs()->GetDictionary("extensions.settings");
    if (!dict) {
      ADD_FAILURE();
      return 0;
    }
    return dict->size();
  }

  void UninstallExtension(const std::string& id, bool use_helper) {
    // Verify that the extension is installed.
    base::FilePath extension_path = extensions_install_dir_.AppendASCII(id);
    EXPECT_TRUE(base::PathExists(extension_path));
    size_t pref_key_count = GetPrefKeyCount();
    EXPECT_GT(pref_key_count, 0u);
    ValidateIntegerPref(id, "state", Extension::ENABLED);

    // Uninstall it.
    if (use_helper) {
      EXPECT_TRUE(ExtensionService::UninstallExtensionHelper(service_, id));
    } else {
      EXPECT_TRUE(service_->UninstallExtension(id, false, NULL));
    }
    --expected_extensions_count_;

    // We should get an unload notification.
    EXPECT_FALSE(unloaded_id_.empty());
    EXPECT_EQ(id, unloaded_id_);

    // Verify uninstalled state.
    size_t new_pref_key_count = GetPrefKeyCount();
    if (new_pref_key_count == pref_key_count) {
      ValidateIntegerPref(id, "location",
                          Extension::EXTERNAL_EXTENSION_UNINSTALLED);
    } else {
      EXPECT_EQ(new_pref_key_count, pref_key_count - 1);
    }

    // The extension should not be in the service anymore.
    EXPECT_FALSE(service_->GetInstalledExtension(id));
    base::RunLoop().RunUntilIdle();

    // The directory should be gone.
    EXPECT_FALSE(base::PathExists(extension_path));
  }

  void ValidatePrefKeyCount(size_t count) {
    EXPECT_EQ(count, GetPrefKeyCount());
  }

  void ValidateBooleanPref(const std::string& extension_id,
                           const std::string& pref_path,
                           bool expected_val) {
    std::string msg = " while checking: ";
    msg += extension_id;
    msg += " ";
    msg += pref_path;
    msg += " == ";
    msg += expected_val ? "true" : "false";

    PrefService* prefs = profile_->GetPrefs();
    const DictionaryValue* dict =
        prefs->GetDictionary("extensions.settings");
    ASSERT_TRUE(dict != NULL) << msg;
    const DictionaryValue* pref = NULL;
    ASSERT_TRUE(dict->GetDictionary(extension_id, &pref)) << msg;
    EXPECT_TRUE(pref != NULL) << msg;
    bool val;
    ASSERT_TRUE(pref->GetBoolean(pref_path, &val)) << msg;
    EXPECT_EQ(expected_val, val) << msg;
  }

  bool IsPrefExist(const std::string& extension_id,
                   const std::string& pref_path) {
    const DictionaryValue* dict =
        profile_->GetPrefs()->GetDictionary("extensions.settings");
    if (dict == NULL) return false;
    const DictionaryValue* pref = NULL;
    if (!dict->GetDictionary(extension_id, &pref)) {
      return false;
    }
    if (pref == NULL) {
      return false;
    }
    bool val;
    if (!pref->GetBoolean(pref_path, &val)) {
      return false;
    }
    return true;
  }

  void ValidateIntegerPref(const std::string& extension_id,
                           const std::string& pref_path,
                           int expected_val) {
    std::string msg = " while checking: ";
    msg += extension_id;
    msg += " ";
    msg += pref_path;
    msg += " == ";
    msg += base::IntToString(expected_val);

    PrefService* prefs = profile_->GetPrefs();
    const DictionaryValue* dict =
        prefs->GetDictionary("extensions.settings");
    ASSERT_TRUE(dict != NULL) << msg;
    const DictionaryValue* pref = NULL;
    ASSERT_TRUE(dict->GetDictionary(extension_id, &pref)) << msg;
    EXPECT_TRUE(pref != NULL) << msg;
    int val;
    ASSERT_TRUE(pref->GetInteger(pref_path, &val)) << msg;
    EXPECT_EQ(expected_val, val) << msg;
  }

  void ValidateStringPref(const std::string& extension_id,
                          const std::string& pref_path,
                          const std::string& expected_val) {
    std::string msg = " while checking: ";
    msg += extension_id;
    msg += ".manifest.";
    msg += pref_path;
    msg += " == ";
    msg += expected_val;

    const DictionaryValue* dict =
        profile_->GetPrefs()->GetDictionary("extensions.settings");
    ASSERT_TRUE(dict != NULL) << msg;
    const DictionaryValue* pref = NULL;
    std::string manifest_path = extension_id + ".manifest";
    ASSERT_TRUE(dict->GetDictionary(manifest_path, &pref)) << msg;
    EXPECT_TRUE(pref != NULL) << msg;
    std::string val;
    ASSERT_TRUE(pref->GetString(pref_path, &val)) << msg;
    EXPECT_EQ(expected_val, val) << msg;
  }

  void SetPref(const std::string& extension_id,
               const std::string& pref_path,
               Value* value,
               const std::string& msg) {
    DictionaryPrefUpdate update(profile_->GetPrefs(), "extensions.settings");
    DictionaryValue* dict = update.Get();
    ASSERT_TRUE(dict != NULL) << msg;
    DictionaryValue* pref = NULL;
    ASSERT_TRUE(dict->GetDictionary(extension_id, &pref)) << msg;
    EXPECT_TRUE(pref != NULL) << msg;
    pref->Set(pref_path, value);
  }

  void SetPrefInteg(const std::string& extension_id,
                    const std::string& pref_path,
                    int value) {
    std::string msg = " while setting: ";
    msg += extension_id;
    msg += " ";
    msg += pref_path;
    msg += " = ";
    msg += base::IntToString(value);

    SetPref(extension_id, pref_path, Value::CreateIntegerValue(value), msg);
  }

  void SetPrefBool(const std::string& extension_id,
                   const std::string& pref_path,
                   bool value) {
    std::string msg = " while setting: ";
    msg += extension_id + " " + pref_path;
    msg += " = ";
    msg += (value ? "true" : "false");

    SetPref(extension_id, pref_path, Value::CreateBooleanValue(value), msg);
  }

  void ClearPref(const std::string& extension_id,
                 const std::string& pref_path) {
    std::string msg = " while clearing: ";
    msg += extension_id + " " + pref_path;

    DictionaryPrefUpdate update(profile_->GetPrefs(), "extensions.settings");
    DictionaryValue* dict = update.Get();
    ASSERT_TRUE(dict != NULL) << msg;
    DictionaryValue* pref = NULL;
    ASSERT_TRUE(dict->GetDictionary(extension_id, &pref)) << msg;
    EXPECT_TRUE(pref != NULL) << msg;
    pref->Remove(pref_path, NULL);
  }

  void SetPrefStringSet(const std::string& extension_id,
                        const std::string& pref_path,
                        const std::set<std::string>& value) {
    std::string msg = " while setting: ";
    msg += extension_id + " " + pref_path;

    ListValue* list_value = new ListValue();
    for (std::set<std::string>::const_iterator iter = value.begin();
         iter != value.end(); ++iter)
      list_value->Append(Value::CreateStringValue(*iter));

    SetPref(extension_id, pref_path, list_value, msg);
  }

  void InitPluginService() {
#if defined(ENABLE_PLUGINS)
    PluginService::GetInstance()->Init();
#endif
  }

 protected:
  extensions::ExtensionList loaded_;
  std::string unloaded_id_;
  const Extension* installed_;
  bool was_update_;
  std::string old_name_;
  FeatureSwitch::ScopedOverride override_external_install_prompt_;

 private:
  content::NotificationRegistrar registrar_;
};

// Receives notifications from a PackExtensionJob, indicating either that
// packing succeeded or that there was some error.
class PackExtensionTestClient : public extensions::PackExtensionJob::Client {
 public:
  PackExtensionTestClient(const base::FilePath& expected_crx_path,
                          const base::FilePath& expected_private_key_path);
  virtual void OnPackSuccess(const base::FilePath& crx_path,
                             const base::FilePath& private_key_path) OVERRIDE;
  virtual void OnPackFailure(const std::string& error_message,
                             ExtensionCreator::ErrorType type) OVERRIDE;

 private:
  const base::FilePath expected_crx_path_;
  const base::FilePath expected_private_key_path_;
  DISALLOW_COPY_AND_ASSIGN(PackExtensionTestClient);
};

PackExtensionTestClient::PackExtensionTestClient(
    const base::FilePath& expected_crx_path,
    const base::FilePath& expected_private_key_path)
    : expected_crx_path_(expected_crx_path),
      expected_private_key_path_(expected_private_key_path) {}

// If packing succeeded, we make sure that the package names match our
// expectations.
void PackExtensionTestClient::OnPackSuccess(
    const base::FilePath& crx_path,
    const base::FilePath& private_key_path) {
  // We got the notification and processed it; we don't expect any further tasks
  // to be posted to the current thread, so we should stop blocking and continue
  // on with the rest of the test.
  // This call to |Quit()| matches the call to |Run()| in the
  // |PackPunctuatedExtension| test.
  base::MessageLoop::current()->Quit();
  EXPECT_EQ(expected_crx_path_.value(), crx_path.value());
  EXPECT_EQ(expected_private_key_path_.value(), private_key_path.value());
  ASSERT_TRUE(base::PathExists(private_key_path));
}

// The tests are designed so that we never expect to see a packing error.
void PackExtensionTestClient::OnPackFailure(const std::string& error_message,
                                            ExtensionCreator::ErrorType type) {
  if (type == ExtensionCreator::kCRXExists)
     FAIL() << "Packing should not fail.";
  else
     FAIL() << "Existing CRX should have been overwritten.";
}

// Test loading good extensions from the profile directory.
TEST_F(ExtensionServiceTest, LoadAllExtensionsFromDirectorySuccess) {
  InitPluginService();

  // Initialize the test dir with a good Preferences/extensions.
  base::FilePath source_install_dir = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions");
  base::FilePath pref_path = source_install_dir
      .DirName()
      .AppendASCII("Preferences");
  InitializeInstalledExtensionService(pref_path, source_install_dir);

  service_->Init();

  uint32 expected_num_extensions = 3u;
  ASSERT_EQ(expected_num_extensions, loaded_.size());

  EXPECT_EQ(std::string(good0), loaded_[0]->id());
  EXPECT_EQ(std::string("My extension 1"),
            loaded_[0]->name());
  EXPECT_EQ(std::string("The first extension that I made."),
            loaded_[0]->description());
  EXPECT_EQ(Manifest::INTERNAL, loaded_[0]->location());
  EXPECT_TRUE(service_->GetExtensionById(loaded_[0]->id(), false));
  EXPECT_EQ(expected_num_extensions, service_->extensions()->size());

  ValidatePrefKeyCount(3);
  ValidateIntegerPref(good0, "state", Extension::ENABLED);
  ValidateIntegerPref(good0, "location", Manifest::INTERNAL);
  ValidateIntegerPref(good1, "state", Extension::ENABLED);
  ValidateIntegerPref(good1, "location", Manifest::INTERNAL);
  ValidateIntegerPref(good2, "state", Extension::ENABLED);
  ValidateIntegerPref(good2, "location", Manifest::INTERNAL);

  URLPatternSet expected_patterns;
  AddPattern(&expected_patterns, "file:///*");
  AddPattern(&expected_patterns, "http://*.google.com/*");
  AddPattern(&expected_patterns, "https://*.google.com/*");
  const Extension* extension = loaded_[0].get();
  const extensions::UserScriptList& scripts =
      extensions::ContentScriptsInfo::GetContentScripts(extension);
  ASSERT_EQ(2u, scripts.size());
  EXPECT_EQ(expected_patterns, scripts[0].url_patterns());
  EXPECT_EQ(2u, scripts[0].js_scripts().size());
  ExtensionResource resource00(extension->id(),
                               scripts[0].js_scripts()[0].extension_root(),
                               scripts[0].js_scripts()[0].relative_path());
  base::FilePath expected_path =
      base::MakeAbsoluteFilePath(extension->path().AppendASCII("script1.js"));
  EXPECT_TRUE(resource00.ComparePathWithDefault(expected_path));
  ExtensionResource resource01(extension->id(),
                               scripts[0].js_scripts()[1].extension_root(),
                               scripts[0].js_scripts()[1].relative_path());
  expected_path =
      base::MakeAbsoluteFilePath(extension->path().AppendASCII("script2.js"));
  EXPECT_TRUE(resource01.ComparePathWithDefault(expected_path));
  EXPECT_TRUE(!extensions::PluginInfo::HasPlugins(extension));
  EXPECT_EQ(1u, scripts[1].url_patterns().patterns().size());
  EXPECT_EQ("http://*.news.com/*",
            scripts[1].url_patterns().begin()->GetAsString());
  ExtensionResource resource10(extension->id(),
                               scripts[1].js_scripts()[0].extension_root(),
                               scripts[1].js_scripts()[0].relative_path());
  expected_path =
      extension->path().AppendASCII("js_files").AppendASCII("script3.js");
  expected_path = base::MakeAbsoluteFilePath(expected_path);
  EXPECT_TRUE(resource10.ComparePathWithDefault(expected_path));

  expected_patterns.ClearPatterns();
  AddPattern(&expected_patterns, "http://*.google.com/*");
  AddPattern(&expected_patterns, "https://*.google.com/*");
  EXPECT_EQ(expected_patterns,
            extension->GetActivePermissions()->explicit_hosts());

  EXPECT_EQ(std::string(good1), loaded_[1]->id());
  EXPECT_EQ(std::string("My extension 2"), loaded_[1]->name());
  EXPECT_EQ(std::string(), loaded_[1]->description());
  EXPECT_EQ(loaded_[1]->GetResourceURL("background.html"),
            extensions::BackgroundInfo::GetBackgroundURL(loaded_[1].get()));
  EXPECT_EQ(0u,
            extensions::ContentScriptsInfo::GetContentScripts(loaded_[1].get())
                .size());

  // We don't parse the plugins section on Chrome OS.
#if defined(OS_CHROMEOS)
  EXPECT_TRUE(!extensions::PluginInfo::HasPlugins(loaded_[1].get()));
#else
  ASSERT_TRUE(extensions::PluginInfo::HasPlugins(loaded_[1].get()));
  const std::vector<extensions::PluginInfo>* plugins =
      extensions::PluginInfo::GetPlugins(loaded_[1].get());
  ASSERT_TRUE(plugins);
  ASSERT_EQ(2u, plugins->size());
  EXPECT_EQ(loaded_[1]->path().AppendASCII("content_plugin.dll").value(),
            plugins->at(0).path.value());
  EXPECT_TRUE(plugins->at(0).is_public);
  EXPECT_EQ(loaded_[1]->path().AppendASCII("extension_plugin.dll").value(),
            plugins->at(1).path.value());
  EXPECT_FALSE(plugins->at(1).is_public);
#endif

  EXPECT_EQ(Manifest::INTERNAL, loaded_[1]->location());

  int index = expected_num_extensions - 1;
  EXPECT_EQ(std::string(good2), loaded_[index]->id());
  EXPECT_EQ(std::string("My extension 3"), loaded_[index]->name());
  EXPECT_EQ(std::string(), loaded_[index]->description());
  EXPECT_EQ(0u,
            extensions::ContentScriptsInfo::GetContentScripts(
                loaded_[index].get()).size());
  EXPECT_EQ(Manifest::INTERNAL, loaded_[index]->location());
};

// Test loading bad extensions from the profile directory.
TEST_F(ExtensionServiceTest, LoadAllExtensionsFromDirectoryFail) {
  // Initialize the test dir with a bad Preferences/extensions.
  base::FilePath source_install_dir = data_dir_
      .AppendASCII("bad")
      .AppendASCII("Extensions");
  base::FilePath pref_path = source_install_dir
      .DirName()
      .AppendASCII("Preferences");

  InitializeInstalledExtensionService(pref_path, source_install_dir);

  service_->Init();

  ASSERT_EQ(4u, GetErrors().size());
  ASSERT_EQ(0u, loaded_.size());

  EXPECT_TRUE(MatchPattern(UTF16ToUTF8(GetErrors()[0]),
      std::string("Could not load extension from '*'. ") +
      extension_manifest_errors::kManifestUnreadable)) <<
      UTF16ToUTF8(GetErrors()[0]);

  EXPECT_TRUE(MatchPattern(UTF16ToUTF8(GetErrors()[1]),
      std::string("Could not load extension from '*'. ") +
      extension_manifest_errors::kManifestUnreadable)) <<
      UTF16ToUTF8(GetErrors()[1]);

  EXPECT_TRUE(MatchPattern(UTF16ToUTF8(GetErrors()[2]),
      std::string("Could not load extension from '*'. ") +
      extension_manifest_errors::kMissingFile)) <<
      UTF16ToUTF8(GetErrors()[2]);

  EXPECT_TRUE(MatchPattern(UTF16ToUTF8(GetErrors()[3]),
      std::string("Could not load extension from '*'. ") +
      extension_manifest_errors::kManifestUnreadable)) <<
      UTF16ToUTF8(GetErrors()[3]);
};

// Test that partially deleted extensions are cleaned up during startup
// Test loading bad extensions from the profile directory.
TEST_F(ExtensionServiceTest, CleanupOnStartup) {
  InitPluginService();

  base::FilePath source_install_dir = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions");
  base::FilePath pref_path = source_install_dir
      .DirName()
      .AppendASCII("Preferences");

  InitializeInstalledExtensionService(pref_path, source_install_dir);

  // Simulate that one of them got partially deleted by clearing its pref.
  {
    DictionaryPrefUpdate update(profile_->GetPrefs(), "extensions.settings");
    DictionaryValue* dict = update.Get();
    ASSERT_TRUE(dict != NULL);
    dict->Remove("behllobkkfkfnphdnhnkndlbkcpglgmj", NULL);
  }

  service_->Init();
  // A delayed task to call GarbageCollectExtensions is posted by
  // ExtensionService::Init. As the test won't wait for the delayed task to
  // be called, call it manually instead.
  service_->GarbageCollectExtensions();
  // Wait for GarbageCollectExtensions task to complete.
  base::RunLoop().RunUntilIdle();

  base::FileEnumerator dirs(extensions_install_dir_, false,
                            base::FileEnumerator::DIRECTORIES);
  size_t count = 0;
  while (!dirs.Next().empty())
    count++;

  // We should have only gotten two extensions now.
  EXPECT_EQ(2u, count);

  // And extension1 dir should now be toast.
  base::FilePath extension_dir = extensions_install_dir_
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj");
  ASSERT_FALSE(base::PathExists(extension_dir));
}

// Test that GarbageCollectExtensions deletes the right versions of an
// extension.
TEST_F(ExtensionServiceTest, GarbageCollectWithPendingUpdates) {
  InitPluginService();

  base::FilePath source_install_dir = data_dir_
      .AppendASCII("pending_updates")
      .AppendASCII("Extensions");
  base::FilePath pref_path = source_install_dir
      .DirName()
      .AppendASCII("Preferences");

  InitializeInstalledExtensionService(pref_path, source_install_dir);

  // This is the directory that is going to be deleted, so make sure it actually
  // is there before the garbage collection.
  ASSERT_TRUE(base::PathExists(extensions_install_dir_.AppendASCII(
      "hpiknbiabeeppbpihjehijgoemciehgk/3")));

  service_->GarbageCollectExtensions();
  // Wait for GarbageCollectExtensions task to complete.
  base::RunLoop().RunUntilIdle();

  // Verify that the pending update for the first extension didn't get
  // deleted.
  EXPECT_TRUE(base::PathExists(extensions_install_dir_.AppendASCII(
      "bjafgdebaacbbbecmhlhpofkepfkgcpa/1.0")));
  EXPECT_TRUE(base::PathExists(extensions_install_dir_.AppendASCII(
      "bjafgdebaacbbbecmhlhpofkepfkgcpa/2.0")));
  EXPECT_TRUE(base::PathExists(extensions_install_dir_.AppendASCII(
      "hpiknbiabeeppbpihjehijgoemciehgk/2")));
  EXPECT_FALSE(base::PathExists(extensions_install_dir_.AppendASCII(
      "hpiknbiabeeppbpihjehijgoemciehgk/3")));
}

// Test that pending updates are properly handled on startup.
TEST_F(ExtensionServiceTest, UpdateOnStartup) {
  InitPluginService();

  base::FilePath source_install_dir = data_dir_
      .AppendASCII("pending_updates")
      .AppendASCII("Extensions");
  base::FilePath pref_path = source_install_dir
      .DirName()
      .AppendASCII("Preferences");

  InitializeInstalledExtensionService(pref_path, source_install_dir);

  // This is the directory that is going to be deleted, so make sure it actually
  // is there before the garbage collection.
  ASSERT_TRUE(base::PathExists(extensions_install_dir_.AppendASCII(
      "hpiknbiabeeppbpihjehijgoemciehgk/3")));

  service_->Init();
  // A delayed task to call GarbageCollectExtensions is posted by
  // ExtensionService::Init. As the test won't wait for the delayed task to
  // be called, call it manually instead.
  service_->GarbageCollectExtensions();
  // Wait for GarbageCollectExtensions task to complete.
  base::RunLoop().RunUntilIdle();

  // Verify that the pending update for the first extension got installed.
  EXPECT_FALSE(base::PathExists(extensions_install_dir_.AppendASCII(
      "bjafgdebaacbbbecmhlhpofkepfkgcpa/1.0")));
  EXPECT_TRUE(base::PathExists(extensions_install_dir_.AppendASCII(
      "bjafgdebaacbbbecmhlhpofkepfkgcpa/2.0")));
  EXPECT_TRUE(base::PathExists(extensions_install_dir_.AppendASCII(
      "hpiknbiabeeppbpihjehijgoemciehgk/2")));
  EXPECT_FALSE(base::PathExists(extensions_install_dir_.AppendASCII(
      "hpiknbiabeeppbpihjehijgoemciehgk/3")));

  // Make sure update information got deleted.
  ExtensionPrefs* prefs = service_->extension_prefs();
  EXPECT_FALSE(
      prefs->GetDelayedInstallInfo("bjafgdebaacbbbecmhlhpofkepfkgcpa"));
}

// Test various cases for delayed install because of missing imports.
TEST_F(ExtensionServiceTest, PendingImports) {
  InitPluginService();

  base::FilePath source_install_dir = data_dir_
      .AppendASCII("pending_updates_with_imports")
      .AppendASCII("Extensions");
  base::FilePath pref_path = source_install_dir
      .DirName()
      .AppendASCII("Preferences");

  InitializeInstalledExtensionService(pref_path, source_install_dir);

  // Verify there are no pending extensions initially.
  EXPECT_FALSE(service_->pending_extension_manager()->HasPendingExtensions());

  service_->Init();
  // Wait for GarbageCollectExtensions task to complete.
  base::RunLoop().RunUntilIdle();

  // These extensions are used by the extensions we test below, they must be
  // installed.
  EXPECT_TRUE(base::PathExists(extensions_install_dir_.AppendASCII(
      "bjafgdebaacbbbecmhlhpofkepfkgcpa/1.0")));
  EXPECT_TRUE(base::PathExists(extensions_install_dir_.AppendASCII(
      "hpiknbiabeeppbpihjehijgoemciehgk/2")));

  // Each of these extensions should have been rejected because of dependencies
  // that cannot be satisfied.
  ExtensionPrefs* prefs = service_->extension_prefs();
  EXPECT_FALSE(
      prefs->GetDelayedInstallInfo("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  EXPECT_FALSE(
      prefs->GetInstalledExtensionInfo("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  EXPECT_FALSE(
      prefs->GetDelayedInstallInfo("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  EXPECT_FALSE(
      prefs->GetInstalledExtensionInfo("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
  EXPECT_FALSE(
      prefs->GetDelayedInstallInfo("cccccccccccccccccccccccccccccccc"));
  EXPECT_FALSE(
      prefs->GetInstalledExtensionInfo("cccccccccccccccccccccccccccccccc"));

  // Make sure the import started for the extension with a dependency.
  EXPECT_TRUE(
      prefs->GetDelayedInstallInfo("behllobkkfkfnphdnhnkndlbkcpglgmj"));
  EXPECT_EQ(ExtensionPrefs::DELAY_REASON_WAIT_FOR_IMPORTS,
      prefs->GetDelayedInstallReason("behllobkkfkfnphdnhnkndlbkcpglgmj"));

  EXPECT_FALSE(base::PathExists(extensions_install_dir_.AppendASCII(
      "behllobkkfkfnphdnhnkndlbkcpglgmj/1.0.0.0")));

  EXPECT_TRUE(service_->pending_extension_manager()->HasPendingExtensions());
  std::string pending_id("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
  EXPECT_TRUE(service_->pending_extension_manager()->IsIdPending(pending_id));
  // Remove it because we are not testing the pending extension manager's
  // ability to download and install extensions.
  EXPECT_TRUE(service_->pending_extension_manager()->Remove(pending_id));
}

// Test installing extensions. This test tries to install few extensions using
// crx files. If you need to change those crx files, feel free to repackage
// them, throw away the key used and change the id's above.
TEST_F(ExtensionServiceTest, InstallExtension) {
  InitializeEmptyExtensionService();

  // Extensions not enabled.
  set_extensions_enabled(false);
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_FAILED);
  set_extensions_enabled(true);

  ValidatePrefKeyCount(0);

  // A simple extension that should install without error.
  path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_NEW);
  // TODO(erikkay): verify the contents of the installed extension.

  int pref_count = 0;
  ValidatePrefKeyCount(++pref_count);
  ValidateIntegerPref(good_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(good_crx, "location", Manifest::INTERNAL);

  // An extension with page actions.
  path = data_dir_.AppendASCII("page_action.crx");
  InstallCRX(path, INSTALL_NEW);
  ValidatePrefKeyCount(++pref_count);
  ValidateIntegerPref(page_action, "state", Extension::ENABLED);
  ValidateIntegerPref(page_action, "location", Manifest::INTERNAL);

  // Bad signature.
  path = data_dir_.AppendASCII("bad_signature.crx");
  InstallCRX(path, INSTALL_FAILED);
  ValidatePrefKeyCount(pref_count);

  // 0-length extension file.
  path = data_dir_.AppendASCII("not_an_extension.crx");
  InstallCRX(path, INSTALL_FAILED);
  ValidatePrefKeyCount(pref_count);

  // Bad magic number.
  path = data_dir_.AppendASCII("bad_magic.crx");
  InstallCRX(path, INSTALL_FAILED);
  ValidatePrefKeyCount(pref_count);

  // Extensions cannot have folders or files that have underscores except in
  // certain whitelisted cases (eg _locales). This is an example of a broader
  // class of validation that we do to the directory structure of the extension.
  // We did not used to handle this correctly for installation.
  path = data_dir_.AppendASCII("bad_underscore.crx");
  InstallCRX(path, INSTALL_FAILED);
  ValidatePrefKeyCount(pref_count);

  // TODO(erikkay): add more tests for many of the failure cases.
  // TODO(erikkay): add tests for upgrade cases.
}

struct MockInstallObserver : public extensions::InstallObserver {
  MockInstallObserver() {
  }

  virtual ~MockInstallObserver() {
  }

  virtual void OnBeginExtensionInstall(
      const std::string& extension_id,
      const std::string& extension_name,
      const gfx::ImageSkia& installing_icon,
      bool is_app,
      bool is_platform_app) OVERRIDE {
  }

  virtual void OnDownloadProgress(const std::string& extension_id,
                                  int percent_downloaded) OVERRIDE {
  }

  virtual void OnExtensionInstalled(const Extension* extension) OVERRIDE {
    last_extension_installed = extension->id();
  }

  virtual void OnInstallFailure(const std::string& extension_id) OVERRIDE {
  }

  virtual void OnExtensionLoaded(const Extension* extension) OVERRIDE {
  }

  virtual void OnExtensionUnloaded(const Extension* extension) OVERRIDE {
  }

  virtual void OnExtensionUninstalled(const Extension* extension) OVERRIDE {
    last_extension_uninstalled = extension->id();
  }

  virtual void OnAppsReordered() OVERRIDE {
  }

  virtual void OnAppInstalledToAppList(
      const std::string& extension_id) OVERRIDE {
  }

  virtual void OnShutdown() OVERRIDE {
  }

  std::string last_extension_installed;
  std::string last_extension_uninstalled;
};

// Test that correct notifications are sent to InstallTracker observers on
// extension install and uninstall.
TEST_F(ExtensionServiceTest, InstallObserverNotified) {
  InitializeEmptyExtensionService();

  extensions::InstallTracker* tracker(
      extensions::InstallTrackerFactory::GetForProfile(profile_.get()));
  MockInstallObserver observer;
  tracker->AddObserver(&observer);

  // A simple extension that should install without error.
  ASSERT_TRUE(observer.last_extension_installed.empty());
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_NEW);
  ASSERT_EQ(good_crx, observer.last_extension_installed);

  // Uninstall the extension.
  ASSERT_TRUE(observer.last_extension_uninstalled.empty());
  UninstallExtension(good_crx, false);
  ASSERT_EQ(good_crx, observer.last_extension_uninstalled);

  tracker->RemoveObserver(&observer);
}

// Tests that flags passed to OnExternalExtensionFileFound() make it to the
// extension object.
TEST_F(ExtensionServiceTest, InstallingExternalExtensionWithFlags) {
  const char kPrefFromBookmark[] = "from_bookmark";

  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  set_extensions_enabled(true);

  // Register and install an external extension.
  Version version("1.0.0.0");
  if (service_->OnExternalExtensionFileFound(
          good_crx,
          &version,
          path,
          Manifest::EXTERNAL_PREF,
          Extension::FROM_BOOKMARK,
          false /* mark_acknowledged */)) {
    content::WindowedNotificationObserver(
        chrome::NOTIFICATION_CRX_INSTALLER_DONE,
        content::NotificationService::AllSources()).Wait();
  }

  const Extension* extension = service_->GetExtensionById(good_crx, false);
  ASSERT_TRUE(extension);
  ASSERT_TRUE(extension->from_bookmark());
  ValidateBooleanPref(good_crx, kPrefFromBookmark, true);

  // Upgrade to version 2.0, the flag should be preserved.
  path = data_dir_.AppendASCII("good2.crx");
  UpdateExtension(good_crx, path, ENABLED);
  ValidateBooleanPref(good_crx, kPrefFromBookmark, true);
  extension = service_->GetExtensionById(good_crx, false);
  ASSERT_TRUE(extension);
  ASSERT_TRUE(extension->from_bookmark());
}

// Test the handling of Extension::EXTERNAL_EXTENSION_UNINSTALLED
TEST_F(ExtensionServiceTest, UninstallingExternalExtensions) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  set_extensions_enabled(true);

  Version version("1.0.0.0");
  // Install an external extension.
  if (service_->OnExternalExtensionFileFound(good_crx, &version,
                                             path, Manifest::EXTERNAL_PREF,
                                             Extension::NO_FLAGS, false)) {
    content::WindowedNotificationObserver(
        chrome::NOTIFICATION_CRX_INSTALLER_DONE,
        content::NotificationService::AllSources()).Wait();
  }

  ASSERT_TRUE(service_->GetExtensionById(good_crx, false));

  // Uninstall it and check that its killbit gets set.
  UninstallExtension(good_crx, false);
  ValidateIntegerPref(good_crx, "location",
                      Extension::EXTERNAL_EXTENSION_UNINSTALLED);

  // Try to re-install it externally. This should fail because of the killbit.
  service_->OnExternalExtensionFileFound(good_crx, &version,
                                         path, Manifest::EXTERNAL_PREF,
                                         Extension::NO_FLAGS, false);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(NULL == service_->GetExtensionById(good_crx, false));
  ValidateIntegerPref(good_crx, "location",
                      Extension::EXTERNAL_EXTENSION_UNINSTALLED);

  version = Version("1.0.0.1");
  // Repeat the same thing with a newer version of the extension.
  path = data_dir_.AppendASCII("good2.crx");
  service_->OnExternalExtensionFileFound(good_crx, &version,
                                         path, Manifest::EXTERNAL_PREF,
                                         Extension::NO_FLAGS, false);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(NULL == service_->GetExtensionById(good_crx, false));
  ValidateIntegerPref(good_crx, "location",
                      Extension::EXTERNAL_EXTENSION_UNINSTALLED);

  // Try adding the same extension from an external update URL.
  ASSERT_FALSE(service_->pending_extension_manager()->AddFromExternalUpdateUrl(
      good_crx,
      GURL("http:://fake.update/url"),
      Manifest::EXTERNAL_PREF_DOWNLOAD));

  ASSERT_FALSE(service_->pending_extension_manager()->IsIdPending(good_crx));
}

// Test that uninstalling an external extension does not crash when
// the extension could not be loaded.
// This extension shown in preferences file requires an experimental permission.
// It could not be loaded without such permission.
TEST_F(ExtensionServiceTest, UninstallingNotLoadedExtension) {
  base::FilePath source_install_dir = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions");
  // The preference contains an external extension
  // that requires 'experimental' permission.
  base::FilePath pref_path = source_install_dir
      .DirName()
      .AppendASCII("PreferencesExperimental");

  // Aforementioned extension will not be loaded if
  // there is no '--enable-experimental-extension-apis' command line flag.
  InitializeInstalledExtensionService(pref_path, source_install_dir);

  service_->Init();

  // Check and try to uninstall it.
  // If we don't check whether the extension is loaded before we uninstall it
  // in CheckExternalUninstall, a crash will happen here because we will get or
  // dereference a NULL pointer (extension) inside UninstallExtension.
  MockExtensionProvider provider(NULL, Manifest::EXTERNAL_REGISTRY);
  service_->OnExternalProviderReady(&provider);
}

// Test that external extensions with incorrect IDs are not installed.
TEST_F(ExtensionServiceTest, FailOnWrongId) {
  InitializeEmptyExtensionService();
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  set_extensions_enabled(true);

  Version version("1.0.0.0");

  const std::string wrong_id = all_zero;
  const std::string correct_id = good_crx;
  ASSERT_NE(correct_id, wrong_id);

  // Install an external extension with an ID from the external
  // source that is not equal to the ID in the extension manifest.
  service_->OnExternalExtensionFileFound(
      wrong_id, &version, path, Manifest::EXTERNAL_PREF,
      Extension::NO_FLAGS, false);

  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();
  ASSERT_FALSE(service_->GetExtensionById(good_crx, false));

  // Try again with the right ID. Expect success.
  if (service_->OnExternalExtensionFileFound(
          correct_id, &version, path, Manifest::EXTERNAL_PREF,
          Extension::NO_FLAGS, false)) {
    content::WindowedNotificationObserver(
        chrome::NOTIFICATION_CRX_INSTALLER_DONE,
        content::NotificationService::AllSources()).Wait();
  }
  ASSERT_TRUE(service_->GetExtensionById(good_crx, false));
}

// Test that external extensions with incorrect versions are not installed.
TEST_F(ExtensionServiceTest, FailOnWrongVersion) {
  InitializeEmptyExtensionService();
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  set_extensions_enabled(true);

  // Install an external extension with a version from the external
  // source that is not equal to the version in the extension manifest.
  Version wrong_version("1.2.3.4");
  service_->OnExternalExtensionFileFound(
      good_crx, &wrong_version, path, Manifest::EXTERNAL_PREF,
      Extension::NO_FLAGS, false);

  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();
  ASSERT_FALSE(service_->GetExtensionById(good_crx, false));

  // Try again with the right version. Expect success.
  service_->pending_extension_manager()->Remove(good_crx);
  Version correct_version("1.0.0.0");
  if (service_->OnExternalExtensionFileFound(
          good_crx, &correct_version, path, Manifest::EXTERNAL_PREF,
          Extension::NO_FLAGS, false)) {
    content::WindowedNotificationObserver(
        chrome::NOTIFICATION_CRX_INSTALLER_DONE,
        content::NotificationService::AllSources()).Wait();
  }
  ASSERT_TRUE(service_->GetExtensionById(good_crx, false));
}

// Install a user script (they get converted automatically to an extension)
TEST_F(ExtensionServiceTest, InstallUserScript) {
  // The details of script conversion are tested elsewhere, this just tests
  // integration with ExtensionService.
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_
             .AppendASCII("user_script_basic.user.js");

  ASSERT_TRUE(base::PathExists(path));
  scoped_refptr<CrxInstaller> installer(CrxInstaller::CreateSilent(service_));
  installer->set_allow_silent_install(true);
  installer->InstallUserScript(
      path,
      GURL("http://www.aaronboodman.com/scripts/user_script_basic.user.js"));

  base::RunLoop().RunUntilIdle();
  std::vector<string16> errors = GetErrors();
  EXPECT_TRUE(installed_) << "Nothing was installed.";
  EXPECT_FALSE(was_update_) << path.value();
  ASSERT_EQ(1u, loaded_.size()) << "Nothing was loaded.";
  EXPECT_EQ(0u, errors.size()) << "There were errors: "
                               << JoinString(errors, ',');
  EXPECT_TRUE(service_->GetExtensionById(loaded_[0]->id(), false)) <<
              path.value();

  installed_ = NULL;
  was_update_ = false;
  loaded_.clear();
  ExtensionErrorReporter::GetInstance()->ClearErrors();
}

// Extensions don't install during shutdown.
TEST_F(ExtensionServiceTest, InstallExtensionDuringShutdown) {
  InitializeEmptyExtensionService();

  // Simulate shutdown.
  service_->set_browser_terminating_for_test(true);

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  scoped_refptr<CrxInstaller> installer(CrxInstaller::CreateSilent(service_));
  installer->set_allow_silent_install(true);
  installer->InstallCrx(path);
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(installed_) << "Extension installed during shutdown.";
  ASSERT_EQ(0u, loaded_.size()) << "Extension loaded during shutdown.";
}

// This tests that the granted permissions preferences are correctly set when
// installing an extension.
TEST_F(ExtensionServiceTest, GrantedPermissions) {
  InitializeEmptyExtensionService();
  base::FilePath path = data_dir_
      .AppendASCII("permissions");

  base::FilePath pem_path = path.AppendASCII("unknown.pem");
  path = path.AppendASCII("unknown");

  ASSERT_TRUE(base::PathExists(pem_path));
  ASSERT_TRUE(base::PathExists(path));

  ExtensionPrefs* prefs = service_->extension_prefs();

  APIPermissionSet expected_api_perms;
  URLPatternSet expected_host_perms;

  // Make sure there aren't any granted permissions before the
  // extension is installed.
  scoped_refptr<PermissionSet> known_perms(
      prefs->GetGrantedPermissions(permissions_crx));
  EXPECT_FALSE(known_perms.get());

  const Extension* extension = PackAndInstallCRX(path, pem_path, INSTALL_NEW);

  EXPECT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(permissions_crx, extension->id());

  // Verify that the valid API permissions have been recognized.
  expected_api_perms.insert(APIPermission::kTab);

  AddPattern(&expected_host_perms, "http://*.google.com/*");
  AddPattern(&expected_host_perms, "https://*.google.com/*");
  AddPattern(&expected_host_perms, "http://*.google.com.hk/*");
  AddPattern(&expected_host_perms, "http://www.example.com/*");

  known_perms = prefs->GetGrantedPermissions(extension->id());
  EXPECT_TRUE(known_perms.get());
  EXPECT_FALSE(known_perms->IsEmpty());
  EXPECT_EQ(expected_api_perms, known_perms->apis());
  EXPECT_FALSE(known_perms->HasEffectiveFullAccess());
  EXPECT_EQ(expected_host_perms, known_perms->effective_hosts());
}


#if !defined(OS_CHROMEOS)
// This tests that the granted permissions preferences are correctly set for
// default apps.
TEST_F(ExtensionServiceTest, DefaultAppsGrantedPermissions) {
  InitializeEmptyExtensionService();
  base::FilePath path = data_dir_
      .AppendASCII("permissions");

  base::FilePath pem_path = path.AppendASCII("unknown.pem");
  path = path.AppendASCII("unknown");

  ASSERT_TRUE(base::PathExists(pem_path));
  ASSERT_TRUE(base::PathExists(path));

  ExtensionPrefs* prefs = service_->extension_prefs();

  APIPermissionSet expected_api_perms;
  URLPatternSet expected_host_perms;

  // Make sure there aren't any granted permissions before the
  // extension is installed.
  scoped_refptr<PermissionSet> known_perms(
      prefs->GetGrantedPermissions(permissions_crx));
  EXPECT_FALSE(known_perms.get());

  const Extension* extension = PackAndInstallCRX(
      path, pem_path, INSTALL_NEW, Extension::WAS_INSTALLED_BY_DEFAULT);

  EXPECT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(permissions_crx, extension->id());

  // Verify that the valid API permissions have been recognized.
  expected_api_perms.insert(APIPermission::kTab);

  known_perms = prefs->GetGrantedPermissions(extension->id());
  EXPECT_TRUE(known_perms.get());
  EXPECT_FALSE(known_perms->IsEmpty());
  EXPECT_EQ(expected_api_perms, known_perms->apis());
  EXPECT_FALSE(known_perms->HasEffectiveFullAccess());
}
#endif

#if !defined(OS_CHROMEOS)
// Tests that the granted permissions full_access bit gets set correctly when
// an extension contains an NPAPI plugin. Don't run this test on Chrome OS
// since they don't support plugins.
TEST_F(ExtensionServiceTest, GrantedFullAccessPermissions) {
  InitPluginService();

  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII(good1)
      .AppendASCII("2");

  ASSERT_TRUE(base::PathExists(path));
  const Extension* extension = PackAndInstallCRX(path, INSTALL_NEW);
  EXPECT_EQ(0u, GetErrors().size());
  EXPECT_EQ(1u, service_->extensions()->size());
  ExtensionPrefs* prefs = service_->extension_prefs();

  scoped_refptr<PermissionSet> permissions(
      prefs->GetGrantedPermissions(extension->id()));
  EXPECT_FALSE(permissions->IsEmpty());
  EXPECT_TRUE(permissions->HasEffectiveFullAccess());
  EXPECT_FALSE(permissions->apis().empty());
  EXPECT_TRUE(permissions->HasAPIPermission(APIPermission::kPlugin));

  // Full access implies full host access too...
  EXPECT_TRUE(permissions->HasEffectiveAccessToAllHosts());
}
#endif

// Tests that the extension is disabled when permissions are missing from
// the extension's granted permissions preferences. (This simulates updating
// the browser to a version which recognizes more permissions).
TEST_F(ExtensionServiceTest, GrantedAPIAndHostPermissions) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_
      .AppendASCII("permissions")
      .AppendASCII("unknown");

  ASSERT_TRUE(base::PathExists(path));

  const Extension* extension = PackAndInstallCRX(path, INSTALL_NEW);

  EXPECT_EQ(0u, GetErrors().size());
  EXPECT_EQ(1u, service_->extensions()->size());
  std::string extension_id = extension->id();

  ExtensionPrefs* prefs = service_->extension_prefs();

  APIPermissionSet expected_api_permissions;
  URLPatternSet expected_host_permissions;

  expected_api_permissions.insert(APIPermission::kTab);
  AddPattern(&expected_host_permissions, "http://*.google.com/*");
  AddPattern(&expected_host_permissions, "https://*.google.com/*");
  AddPattern(&expected_host_permissions, "http://*.google.com.hk/*");
  AddPattern(&expected_host_permissions, "http://www.example.com/*");

  std::set<std::string> host_permissions;

  // Test that the extension is disabled when an API permission is missing from
  // the extension's granted api permissions preference. (This simulates
  // updating the browser to a version which recognizes a new API permission).
  SetPref(extension_id, "granted_permissions.api",
          new ListValue(), "granted_permissions.api");
  service_->ReloadExtensions();

  EXPECT_EQ(1u, service_->disabled_extensions()->size());
  extension = service_->disabled_extensions()->begin()->get();

  ASSERT_TRUE(prefs->IsExtensionDisabled(extension_id));
  ASSERT_FALSE(service_->IsExtensionEnabled(extension_id));
  ASSERT_TRUE(prefs->DidExtensionEscalatePermissions(extension_id));

  // Now grant and re-enable the extension, making sure the prefs are updated.
  service_->GrantPermissionsAndEnableExtension(extension);

  ASSERT_FALSE(prefs->IsExtensionDisabled(extension_id));
  ASSERT_TRUE(service_->IsExtensionEnabled(extension_id));
  ASSERT_FALSE(prefs->DidExtensionEscalatePermissions(extension_id));

  scoped_refptr<PermissionSet> current_perms(
      prefs->GetGrantedPermissions(extension_id));
  ASSERT_TRUE(current_perms.get());
  ASSERT_FALSE(current_perms->IsEmpty());
  ASSERT_FALSE(current_perms->HasEffectiveFullAccess());
  ASSERT_EQ(expected_api_permissions, current_perms->apis());
  ASSERT_EQ(expected_host_permissions, current_perms->effective_hosts());

  // Tests that the extension is disabled when a host permission is missing from
  // the extension's granted host permissions preference. (This simulates
  // updating the browser to a version which recognizes additional host
  // permissions).
  host_permissions.clear();
  current_perms = NULL;

  host_permissions.insert("http://*.google.com/*");
  host_permissions.insert("https://*.google.com/*");
  host_permissions.insert("http://*.google.com.hk/*");

  ListValue* api_permissions = new ListValue();
  api_permissions->Append(
      Value::CreateStringValue("tabs"));
  SetPref(extension_id, "granted_permissions.api",
          api_permissions, "granted_permissions.api");
  SetPrefStringSet(
      extension_id, "granted_permissions.scriptable_host", host_permissions);

  service_->ReloadExtensions();

  EXPECT_EQ(1u, service_->disabled_extensions()->size());
  extension = service_->disabled_extensions()->begin()->get();

  ASSERT_TRUE(prefs->IsExtensionDisabled(extension_id));
  ASSERT_FALSE(service_->IsExtensionEnabled(extension_id));
  ASSERT_TRUE(prefs->DidExtensionEscalatePermissions(extension_id));

  // Now grant and re-enable the extension, making sure the prefs are updated.
  service_->GrantPermissionsAndEnableExtension(extension);

  ASSERT_TRUE(service_->IsExtensionEnabled(extension_id));
  ASSERT_FALSE(prefs->DidExtensionEscalatePermissions(extension_id));

  current_perms = prefs->GetGrantedPermissions(extension_id);
  ASSERT_TRUE(current_perms.get());
  ASSERT_FALSE(current_perms->IsEmpty());
  ASSERT_FALSE(current_perms->HasEffectiveFullAccess());
  ASSERT_EQ(expected_api_permissions, current_perms->apis());
  ASSERT_EQ(expected_host_permissions, current_perms->effective_hosts());
}

// Test Packaging and installing an extension.
TEST_F(ExtensionServiceTest, PackExtension) {
  InitializeEmptyExtensionService();
  base::FilePath input_directory = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj")
      .AppendASCII("1.0.0.0");

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath output_directory = temp_dir.path();

  base::FilePath crx_path(output_directory.AppendASCII("ex1.crx"));
  base::FilePath privkey_path(output_directory.AppendASCII("privkey.pem"));

  scoped_ptr<ExtensionCreator> creator(new ExtensionCreator());
  ASSERT_TRUE(creator->Run(input_directory, crx_path, base::FilePath(),
      privkey_path, ExtensionCreator::kNoRunFlags));
  ASSERT_TRUE(base::PathExists(crx_path));
  ASSERT_TRUE(base::PathExists(privkey_path));

  // Repeat the run with the pem file gone, and no special flags
  // Should refuse to overwrite the existing crx.
  base::DeleteFile(privkey_path, false);
  ASSERT_FALSE(creator->Run(input_directory, crx_path, base::FilePath(),
      privkey_path, ExtensionCreator::kNoRunFlags));

  // OK, now try it with a flag to overwrite existing crx.  Should work.
  ASSERT_TRUE(creator->Run(input_directory, crx_path, base::FilePath(),
      privkey_path, ExtensionCreator::kOverwriteCRX));

  // Repeat the run allowing existing crx, but the existing pem is still
  // an error.  Should fail.
  ASSERT_FALSE(creator->Run(input_directory, crx_path, base::FilePath(),
      privkey_path, ExtensionCreator::kOverwriteCRX));

  ASSERT_TRUE(base::PathExists(privkey_path));
  InstallCRX(crx_path, INSTALL_NEW);

  // Try packing with invalid paths.
  creator.reset(new ExtensionCreator());
  ASSERT_FALSE(
      creator->Run(base::FilePath(), base::FilePath(), base::FilePath(),
                   base::FilePath(), ExtensionCreator::kOverwriteCRX));

  // Try packing an empty directory. Should fail because an empty directory is
  // not a valid extension.
  base::ScopedTempDir temp_dir2;
  ASSERT_TRUE(temp_dir2.CreateUniqueTempDir());
  creator.reset(new ExtensionCreator());
  ASSERT_FALSE(creator->Run(temp_dir2.path(), crx_path, privkey_path,
                            base::FilePath(), ExtensionCreator::kOverwriteCRX));

  // Try packing with an invalid manifest.
  std::string invalid_manifest_content = "I am not a manifest.";
  ASSERT_TRUE(file_util::WriteFile(
      temp_dir2.path().Append(extensions::kManifestFilename),
      invalid_manifest_content.c_str(), invalid_manifest_content.size()));
  creator.reset(new ExtensionCreator());
  ASSERT_FALSE(creator->Run(temp_dir2.path(), crx_path, privkey_path,
                            base::FilePath(), ExtensionCreator::kOverwriteCRX));

  // Try packing with a private key that is a valid key, but invalid for the
  // extension.
  base::FilePath bad_private_key_dir = data_dir_.AppendASCII("bad_private_key");
  crx_path = output_directory.AppendASCII("bad_private_key.crx");
  privkey_path = data_dir_.AppendASCII("bad_private_key.pem");
  ASSERT_FALSE(creator->Run(bad_private_key_dir, crx_path, base::FilePath(),
      privkey_path, ExtensionCreator::kOverwriteCRX));
}

// Test Packaging and installing an extension whose name contains punctuation.
TEST_F(ExtensionServiceTest, PackPunctuatedExtension) {
  InitializeEmptyExtensionService();
  base::FilePath input_directory = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII(good0)
      .AppendASCII("1.0.0.0");

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Extension names containing punctuation, and the expected names for the
  // packed extensions.
  const base::FilePath punctuated_names[] = {
    base::FilePath(FILE_PATH_LITERAL("this.extensions.name.has.periods")),
    base::FilePath(FILE_PATH_LITERAL(".thisextensionsnamestartswithaperiod")),
    base::FilePath(FILE_PATH_LITERAL("thisextensionhasaslashinitsname/")).
        NormalizePathSeparators(),
  };
  const base::FilePath expected_crx_names[] = {
    base::FilePath(FILE_PATH_LITERAL("this.extensions.name.has.periods.crx")),
    base::FilePath(
        FILE_PATH_LITERAL(".thisextensionsnamestartswithaperiod.crx")),
    base::FilePath(FILE_PATH_LITERAL("thisextensionhasaslashinitsname.crx")),
  };
  const base::FilePath expected_private_key_names[] = {
    base::FilePath(FILE_PATH_LITERAL("this.extensions.name.has.periods.pem")),
    base::FilePath(
        FILE_PATH_LITERAL(".thisextensionsnamestartswithaperiod.pem")),
    base::FilePath(FILE_PATH_LITERAL("thisextensionhasaslashinitsname.pem")),
  };

  for (size_t i = 0; i < arraysize(punctuated_names); ++i) {
    SCOPED_TRACE(punctuated_names[i].value().c_str());
    base::FilePath output_dir = temp_dir.path().Append(punctuated_names[i]);

    // Copy the extension into the output directory, as PackExtensionJob doesn't
    // let us choose where to output the packed extension.
    ASSERT_TRUE(base::CopyDirectory(input_directory, output_dir, true));

    base::FilePath expected_crx_path =
        temp_dir.path().Append(expected_crx_names[i]);
    base::FilePath expected_private_key_path =
        temp_dir.path().Append(expected_private_key_names[i]);
    PackExtensionTestClient pack_client(expected_crx_path,
                                        expected_private_key_path);
    scoped_refptr<extensions::PackExtensionJob> packer(
        new extensions::PackExtensionJob(&pack_client, output_dir,
                                         base::FilePath(),
                                         ExtensionCreator::kOverwriteCRX));
    packer->Start();

    // The packer will post a notification task to the current thread's message
    // loop when it is finished.  We manually run the loop here so that we
    // block and catch the notification; otherwise, the process would exit.
    // This call to |Run()| is matched by a call to |Quit()| in the
    // |PackExtensionTestClient|'s notification handling code.
    base::MessageLoop::current()->Run();

    if (HasFatalFailure())
      return;

    InstallCRX(expected_crx_path, INSTALL_NEW);
  }
}

TEST_F(ExtensionServiceTest, PackExtensionContainingKeyFails) {
  InitializeEmptyExtensionService();

  base::ScopedTempDir extension_temp_dir;
  ASSERT_TRUE(extension_temp_dir.CreateUniqueTempDir());
  base::FilePath input_directory = extension_temp_dir.path().AppendASCII("ext");
  ASSERT_TRUE(base::CopyDirectory(
      data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj")
      .AppendASCII("1.0.0.0"),
      input_directory,
      /*recursive=*/true));

  base::ScopedTempDir output_temp_dir;
  ASSERT_TRUE(output_temp_dir.CreateUniqueTempDir());
  base::FilePath output_directory = output_temp_dir.path();

  base::FilePath crx_path(output_directory.AppendASCII("ex1.crx"));
  base::FilePath privkey_path(output_directory.AppendASCII("privkey.pem"));

  // Pack the extension once to get a private key.
  scoped_ptr<ExtensionCreator> creator(new ExtensionCreator());
  ASSERT_TRUE(creator->Run(input_directory, crx_path, base::FilePath(),
      privkey_path, ExtensionCreator::kNoRunFlags))
      << creator->error_message();
  ASSERT_TRUE(base::PathExists(crx_path));
  ASSERT_TRUE(base::PathExists(privkey_path));

  base::DeleteFile(crx_path, false);
  // Move the pem file into the extension.
  base::Move(privkey_path,
                  input_directory.AppendASCII("privkey.pem"));

  // This pack should fail because of the contained private key.
  EXPECT_FALSE(creator->Run(input_directory, crx_path, base::FilePath(),
      privkey_path, ExtensionCreator::kNoRunFlags));
  EXPECT_THAT(creator->error_message(),
              testing::ContainsRegex(
                  "extension includes the key file.*privkey.pem"));
}

// Test Packaging and installing an extension using an openssl generated key.
// The openssl is generated with the following:
// > openssl genrsa -out privkey.pem 1024
// > openssl pkcs8 -topk8 -nocrypt -in privkey.pem -out privkey_asn1.pem
// The privkey.pem is a PrivateKey, and the pcks8 -topk8 creates a
// PrivateKeyInfo ASN.1 structure, we our RSAPrivateKey expects.
TEST_F(ExtensionServiceTest, PackExtensionOpenSSLKey) {
  InitializeEmptyExtensionService();
  base::FilePath input_directory = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj")
      .AppendASCII("1.0.0.0");
  base::FilePath privkey_path(data_dir_.AppendASCII(
      "openssl_privkey_asn1.pem"));
  ASSERT_TRUE(base::PathExists(privkey_path));

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath output_directory = temp_dir.path();

  base::FilePath crx_path(output_directory.AppendASCII("ex1.crx"));

  scoped_ptr<ExtensionCreator> creator(new ExtensionCreator());
  ASSERT_TRUE(creator->Run(input_directory, crx_path, privkey_path,
      base::FilePath(), ExtensionCreator::kOverwriteCRX));

  InstallCRX(crx_path, INSTALL_NEW);
}

TEST_F(ExtensionServiceTest, InstallTheme) {
  InitializeEmptyExtensionService();

  // A theme.
  base::FilePath path = data_dir_.AppendASCII("theme.crx");
  InstallCRX(path, INSTALL_NEW);
  int pref_count = 0;
  ValidatePrefKeyCount(++pref_count);
  ValidateIntegerPref(theme_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(theme_crx, "location", Manifest::INTERNAL);

  // A theme when extensions are disabled. Themes can be installed, even when
  // extensions are disabled.
  set_extensions_enabled(false);
  path = data_dir_.AppendASCII("theme2.crx");
  InstallCRX(path, INSTALL_NEW);
  ValidatePrefKeyCount(++pref_count);
  ValidateIntegerPref(theme2_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(theme2_crx, "location", Manifest::INTERNAL);

  // A theme with extension elements. Themes cannot have extension elements,
  // so any such elements (like content scripts) should be ignored.
  set_extensions_enabled(true);
  {
    path = data_dir_.AppendASCII("theme_with_extension.crx");
    const Extension* extension = InstallCRX(path, INSTALL_NEW);
    ValidatePrefKeyCount(++pref_count);
    ASSERT_TRUE(extension);
    EXPECT_TRUE(extension->is_theme());
    EXPECT_EQ(
        0u,
        extensions::ContentScriptsInfo::GetContentScripts(extension).size());
  }

  // A theme with image resources missing (misspelt path).
  path = data_dir_.AppendASCII("theme_missing_image.crx");
  InstallCRX(path, INSTALL_FAILED);
  ValidatePrefKeyCount(pref_count);
}

TEST_F(ExtensionServiceTest, LoadLocalizedTheme) {
  // Load.
  InitializeEmptyExtensionService();
  base::FilePath extension_path = data_dir_
      .AppendASCII("theme_i18n");

  extensions::UnpackedInstaller::Create(service_)->Load(extension_path);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  EXPECT_EQ(1u, service_->extensions()->size());
  const Extension* theme = service_->extensions()->begin()->get();
  EXPECT_EQ("name", theme->name());
  EXPECT_EQ("description", theme->description());

  // Cleanup the "Cached Theme.pak" file. Ideally, this would be installed in a
  // temporary directory, but it automatically installs to the extension's
  // directory, and we don't want to copy the whole extension for a unittest.
  base::FilePath theme_file = extension_path.Append(chrome::kThemePackFilename);
  ASSERT_TRUE(base::PathExists(theme_file));
  ASSERT_TRUE(base::DeleteFile(theme_file, false));  // Not recursive.
}

// Tests that we can change the ID of an unpacked extension by adding a key
// to its manifest.
TEST_F(ExtensionServiceTest, UnpackedExtensionCanChangeID) {
  InitializeEmptyExtensionService();

  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  base::FilePath extension_path = temp.path();
  base::FilePath manifest_path =
      extension_path.Append(extensions::kManifestFilename);
  base::FilePath manifest_no_key = data_dir_.
      AppendASCII("unpacked").
      AppendASCII("manifest_no_key.json");

  base::FilePath manifest_with_key = data_dir_.
      AppendASCII("unpacked").
      AppendASCII("manifest_with_key.json");

  ASSERT_TRUE(base::PathExists(manifest_no_key));
  ASSERT_TRUE(base::PathExists(manifest_with_key));

  // Load the unpacked extension with no key.
  base::CopyFile(manifest_no_key, manifest_path);
  extensions::UnpackedInstaller::Create(service_)->Load(extension_path);

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  EXPECT_EQ(1u, service_->extensions()->size());

  // Add the key to the manifest.
  base::CopyFile(manifest_with_key, manifest_path);
  loaded_.clear();

  // Reload the extensions.
  service_->ReloadExtensions();
  const Extension* extension = service_->GetExtensionById(unpacked, false);
  EXPECT_EQ(unpacked, extension->id());
  ASSERT_EQ(1u, loaded_.size());

  // TODO(jstritar): Right now this just makes sure we don't crash and burn, but
  // we should also test that preferences are preserved.
}

#if defined(OS_POSIX)
TEST_F(ExtensionServiceTest, UnpackedExtensionMayContainSymlinkedFiles) {
  base::FilePath source_data_dir = data_dir_.
      AppendASCII("unpacked").
      AppendASCII("symlinks_allowed");

  // Paths to test data files.
  base::FilePath source_manifest = source_data_dir.AppendASCII("manifest.json");
  ASSERT_TRUE(base::PathExists(source_manifest));
  base::FilePath source_icon = source_data_dir.AppendASCII("icon.png");
  ASSERT_TRUE(base::PathExists(source_icon));

  // Set up the temporary extension directory.
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  base::FilePath extension_path = temp.path();
  base::FilePath manifest = extension_path.Append(
      extensions::kManifestFilename);
  base::FilePath icon_symlink = extension_path.AppendASCII("icon.png");
  base::CopyFile(source_manifest, manifest);
  file_util::CreateSymbolicLink(source_icon, icon_symlink);

  // Load extension.
  InitializeEmptyExtensionService();
  extensions::UnpackedInstaller::Create(service_)->Load(extension_path);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(GetErrors().empty());
  ASSERT_EQ(1u, loaded_.size());
  EXPECT_EQ(1u, service_->extensions()->size());
}
#endif

TEST_F(ExtensionServiceTest, InstallLocalizedTheme) {
  InitializeEmptyExtensionService();
  base::FilePath theme_path = data_dir_
      .AppendASCII("theme_i18n");

  const Extension* theme = PackAndInstallCRX(theme_path, INSTALL_NEW);

  EXPECT_EQ(0u, GetErrors().size());
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ("name", theme->name());
  EXPECT_EQ("description", theme->description());
}

TEST_F(ExtensionServiceTest, InstallApps) {
  InitializeEmptyExtensionService();

  // An empty app.
  const Extension* app = PackAndInstallCRX(data_dir_.AppendASCII("app1"),
                                           INSTALL_NEW);
  int pref_count = 0;
  ValidatePrefKeyCount(++pref_count);
  ASSERT_EQ(1u, service_->extensions()->size());
  ValidateIntegerPref(app->id(), "state", Extension::ENABLED);
  ValidateIntegerPref(app->id(), "location", Manifest::INTERNAL);

  // Another app with non-overlapping extent. Should succeed.
  PackAndInstallCRX(data_dir_.AppendASCII("app2"), INSTALL_NEW);
  ValidatePrefKeyCount(++pref_count);

  // A third app whose extent overlaps the first. Should fail.
  PackAndInstallCRX(data_dir_.AppendASCII("app3"), INSTALL_FAILED);
  ValidatePrefKeyCount(pref_count);
}

// Tests that file access is OFF by default.
TEST_F(ExtensionServiceTest, DefaultFileAccess) {
  InitializeEmptyExtensionService();
  const Extension* extension =
      PackAndInstallCRX(data_dir_
                        .AppendASCII("permissions")
                        .AppendASCII("files"),
                        INSTALL_NEW);
  EXPECT_EQ(0u, GetErrors().size());
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_FALSE(service_->extension_prefs()->AllowFileAccess(extension->id()));
}

TEST_F(ExtensionServiceTest, UpdateApps) {
  InitializeEmptyExtensionService();
  base::FilePath extensions_path = data_dir_.AppendASCII("app_update");

  // First install v1 of a hosted app.
  const Extension* extension =
      InstallCRX(extensions_path.AppendASCII("v1.crx"), INSTALL_NEW);
  ASSERT_EQ(1u, service_->extensions()->size());
  std::string id = extension->id();
  ASSERT_EQ(std::string("1"), extension->version()->GetString());

  // Now try updating to v2.
  UpdateExtension(id,
                  extensions_path.AppendASCII("v2.crx"),
                  ENABLED);
  ASSERT_EQ(std::string("2"),
            service_->GetExtensionById(id, false)->version()->GetString());
}

// Verifies that the NTP page and launch ordinals are kept when updating apps.
TEST_F(ExtensionServiceTest, UpdateAppsRetainOrdinals) {
  InitializeEmptyExtensionService();
  ExtensionSorting* sorting = service_->extension_prefs()->extension_sorting();
  base::FilePath extensions_path = data_dir_.AppendASCII("app_update");

  // First install v1 of a hosted app.
  const Extension* extension =
      InstallCRX(extensions_path.AppendASCII("v1.crx"), INSTALL_NEW);
  ASSERT_EQ(1u, service_->extensions()->size());
  std::string id = extension->id();
  ASSERT_EQ(std::string("1"), extension->version()->GetString());

  // Modify the ordinals so we can distinguish them from the defaults.
  syncer::StringOrdinal new_page_ordinal =
      sorting->GetPageOrdinal(id).CreateAfter();
  syncer::StringOrdinal new_launch_ordinal =
      sorting->GetAppLaunchOrdinal(id).CreateBefore();

  sorting->SetPageOrdinal(id, new_page_ordinal);
  sorting->SetAppLaunchOrdinal(id, new_launch_ordinal);

  // Now try updating to v2.
  UpdateExtension(id, extensions_path.AppendASCII("v2.crx"), ENABLED);
  ASSERT_EQ(std::string("2"),
            service_->GetExtensionById(id, false)->version()->GetString());

  // Verify that the ordinals match.
  ASSERT_TRUE(new_page_ordinal.Equals(sorting->GetPageOrdinal(id)));
  ASSERT_TRUE(new_launch_ordinal.Equals(sorting->GetAppLaunchOrdinal(id)));
}

// Ensures that the CWS has properly initialized ordinals.
TEST_F(ExtensionServiceTest, EnsureCWSOrdinalsInitialized) {
  InitializeEmptyExtensionService();
  service_->component_loader()->Add(
      IDR_WEBSTORE_MANIFEST, base::FilePath(FILE_PATH_LITERAL("web_store")));
  service_->Init();

  ExtensionSorting* sorting = service_->extension_prefs()->extension_sorting();
  EXPECT_TRUE(
      sorting->GetPageOrdinal(extension_misc::kWebStoreAppId).IsValid());
  EXPECT_TRUE(
      sorting->GetAppLaunchOrdinal(extension_misc::kWebStoreAppId).IsValid());
}

TEST_F(ExtensionServiceTest, InstallAppsWithUnlimitedStorage) {
  InitializeEmptyExtensionService();
  EXPECT_TRUE(service_->extensions()->is_empty());

  int pref_count = 0;

  // Install app1 with unlimited storage.
  const Extension* extension =
      PackAndInstallCRX(data_dir_.AppendASCII("app1"), INSTALL_NEW);
  ValidatePrefKeyCount(++pref_count);
  ASSERT_EQ(1u, service_->extensions()->size());
  const std::string id1 = extension->id();
  EXPECT_TRUE(extension->HasAPIPermission(
      APIPermission::kUnlimitedStorage));
  EXPECT_TRUE(extension->web_extent().MatchesURL(
      extensions::AppLaunchInfo::GetFullLaunchURL(extension)));
  const GURL origin1(
      extensions::AppLaunchInfo::GetFullLaunchURL(extension).GetOrigin());
  EXPECT_TRUE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageUnlimited(origin1));

  // Install app2 from the same origin with unlimited storage.
  extension = PackAndInstallCRX(data_dir_.AppendASCII("app2"), INSTALL_NEW);
  ValidatePrefKeyCount(++pref_count);
  ASSERT_EQ(2u, service_->extensions()->size());
  const std::string id2 = extension->id();
  EXPECT_TRUE(extension->HasAPIPermission(
      APIPermission::kUnlimitedStorage));
  EXPECT_TRUE(extension->web_extent().MatchesURL(
      extensions::AppLaunchInfo::GetFullLaunchURL(extension)));
  const GURL origin2(
      extensions::AppLaunchInfo::GetFullLaunchURL(extension).GetOrigin());
  EXPECT_EQ(origin1, origin2);
  EXPECT_TRUE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageUnlimited(origin2));


  // Uninstall one of them, unlimited storage should still be granted
  // to the origin.
  UninstallExtension(id1, false);
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_TRUE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageUnlimited(origin1));

  // Uninstall the other, unlimited storage should be revoked.
  UninstallExtension(id2, false);
  EXPECT_EQ(0u, service_->extensions()->size());
  EXPECT_FALSE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageUnlimited(origin2));
}

TEST_F(ExtensionServiceTest, InstallAppsAndCheckStorageProtection) {
  InitializeEmptyExtensionService();
  EXPECT_TRUE(service_->extensions()->is_empty());

  int pref_count = 0;

  const Extension* extension =
      PackAndInstallCRX(data_dir_.AppendASCII("app1"), INSTALL_NEW);
  ValidatePrefKeyCount(++pref_count);
  ASSERT_EQ(1u, service_->extensions()->size());
  EXPECT_TRUE(extension->is_app());
  const std::string id1 = extension->id();
  const GURL origin1(
      extensions::AppLaunchInfo::GetFullLaunchURL(extension).GetOrigin());
  EXPECT_TRUE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageProtected(origin1));

  // App 4 has a different origin (maps.google.com).
  extension = PackAndInstallCRX(data_dir_.AppendASCII("app4"), INSTALL_NEW);
  ValidatePrefKeyCount(++pref_count);
  ASSERT_EQ(2u, service_->extensions()->size());
  const std::string id2 = extension->id();
  const GURL origin2(
      extensions::AppLaunchInfo::GetFullLaunchURL(extension).GetOrigin());
  ASSERT_NE(origin1, origin2);
  EXPECT_TRUE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageProtected(origin2));

  UninstallExtension(id1, false);
  EXPECT_EQ(1u, service_->extensions()->size());

  UninstallExtension(id2, false);

  EXPECT_TRUE(service_->extensions()->is_empty());
  EXPECT_FALSE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageProtected(origin1));
  EXPECT_FALSE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageProtected(origin2));
}

// Test that when an extension version is reinstalled, nothing happens.
TEST_F(ExtensionServiceTest, Reinstall) {
  InitializeEmptyExtensionService();

  // A simple extension that should install without error.
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_NEW);

  ValidatePrefKeyCount(1);
  ValidateIntegerPref(good_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(good_crx, "location", Manifest::INTERNAL);

  // Reinstall the same version, it should overwrite the previous one.
  InstallCRX(path, INSTALL_UPDATED);

  ValidatePrefKeyCount(1);
  ValidateIntegerPref(good_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(good_crx, "location", Manifest::INTERNAL);
}

// Test that we can determine if extensions came from the
// Chrome web store.
TEST_F(ExtensionServiceTest, FromWebStore) {
  InitializeEmptyExtensionService();

  // A simple extension that should install without error.
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  // Not from web store.
  const Extension* extension = InstallCRX(path, INSTALL_NEW);
  std::string id = extension->id();

  ValidatePrefKeyCount(1);
  ValidateBooleanPref(good_crx, "from_webstore", false);
  ASSERT_FALSE(extension->from_webstore());

  // Test install from web store.
  InstallCRXFromWebStore(path, INSTALL_UPDATED);  // From web store.

  ValidatePrefKeyCount(1);
  ValidateBooleanPref(good_crx, "from_webstore", true);

  // Reload so extension gets reinitialized with new value.
  service_->ReloadExtensions();
  extension = service_->GetExtensionById(id, false);
  ASSERT_TRUE(extension->from_webstore());

  // Upgrade to version 2.0
  path = data_dir_.AppendASCII("good2.crx");
  UpdateExtension(good_crx, path, ENABLED);
  ValidatePrefKeyCount(1);
  ValidateBooleanPref(good_crx, "from_webstore", true);
}

// Test upgrading a signed extension.
TEST_F(ExtensionServiceTest, UpgradeSignedGood) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  const Extension* extension = InstallCRX(path, INSTALL_NEW);
  std::string id = extension->id();

  ASSERT_EQ("1.0.0.0", extension->version()->GetString());
  ASSERT_EQ(0u, GetErrors().size());

  // Upgrade to version 1.0.0.1.
  // Also test that the extension's old and new title are correctly retrieved.
  path = data_dir_.AppendASCII("good2.crx");
  InstallCRX(path, INSTALL_UPDATED, Extension::NO_FLAGS, "My extension 1");
  extension = service_->GetExtensionById(id, false);

  ASSERT_EQ("1.0.0.1", extension->version()->GetString());
  ASSERT_EQ("My updated extension 1", extension->name());
  ASSERT_EQ(0u, GetErrors().size());
}

// Test upgrading a signed extension with a bad signature.
TEST_F(ExtensionServiceTest, UpgradeSignedBad) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_NEW);

  // Try upgrading with a bad signature. This should fail during the unpack,
  // because the key will not match the signature.
  path = data_dir_.AppendASCII("bad_signature.crx");
  InstallCRX(path, INSTALL_FAILED);
}

// Test a normal update via the UpdateExtension API
TEST_F(ExtensionServiceTest, UpdateExtension) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");

  const Extension* good = InstallCRX(path, INSTALL_NEW);
  ASSERT_EQ("1.0.0.0", good->VersionString());
  ASSERT_EQ(good_crx, good->id());

  path = data_dir_.AppendASCII("good2.crx");
  UpdateExtension(good_crx, path, ENABLED);
  ASSERT_EQ("1.0.0.1",
            service_->GetExtensionById(good_crx, false)->
            version()->GetString());
}

// Extensions should not be updated during browser shutdown.
TEST_F(ExtensionServiceTest, UpdateExtensionDuringShutdown) {
  InitializeEmptyExtensionService();

  // Install an extension.
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  const Extension* good = InstallCRX(path, INSTALL_NEW);
  ASSERT_EQ(good_crx, good->id());

  // Simulate shutdown.
  service_->set_browser_terminating_for_test(true);

  // Update should fail and extension should not be updated.
  path = data_dir_.AppendASCII("good2.crx");
  bool updated = service_->UpdateExtension(good_crx, path, GURL(), NULL);
  ASSERT_FALSE(updated);
  ASSERT_EQ("1.0.0.0",
            service_->GetExtensionById(good_crx, false)->
                version()->GetString());
}

// Test updating a not-already-installed extension - this should fail
TEST_F(ExtensionServiceTest, UpdateNotInstalledExtension) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  UpdateExtension(good_crx, path, UPDATED);
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(0u, service_->extensions()->size());
  ASSERT_FALSE(installed_);
  ASSERT_EQ(0u, loaded_.size());
}

// Makes sure you can't downgrade an extension via UpdateExtension
TEST_F(ExtensionServiceTest, UpdateWillNotDowngrade) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good2.crx");

  const Extension* good = InstallCRX(path, INSTALL_NEW);
  ASSERT_EQ("1.0.0.1", good->VersionString());
  ASSERT_EQ(good_crx, good->id());

  // Change path from good2.crx -> good.crx
  path = data_dir_.AppendASCII("good.crx");
  UpdateExtension(good_crx, path, FAILED);
  ASSERT_EQ("1.0.0.1",
            service_->GetExtensionById(good_crx, false)->
            version()->GetString());
}

// Make sure calling update with an identical version does nothing
TEST_F(ExtensionServiceTest, UpdateToSameVersionIsNoop) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");

  const Extension* good = InstallCRX(path, INSTALL_NEW);
  ASSERT_EQ(good_crx, good->id());
  UpdateExtension(good_crx, path, FAILED_SILENTLY);
}

// Tests that updating an extension does not clobber old state.
TEST_F(ExtensionServiceTest, UpdateExtensionPreservesState) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");

  const Extension* good = InstallCRX(path, INSTALL_NEW);
  ASSERT_EQ("1.0.0.0", good->VersionString());
  ASSERT_EQ(good_crx, good->id());

  // Disable it and allow it to run in incognito. These settings should carry
  // over to the updated version.
  service_->DisableExtension(good->id(), Extension::DISABLE_USER_ACTION);
  service_->SetIsIncognitoEnabled(good->id(), true);
  service_->extension_prefs()->SetDidExtensionEscalatePermissions(good, true);

  path = data_dir_.AppendASCII("good2.crx");
  UpdateExtension(good_crx, path, INSTALLED);
  ASSERT_EQ(1u, service_->disabled_extensions()->size());\
  const Extension* good2 = service_->GetExtensionById(good_crx, true);
  ASSERT_EQ("1.0.0.1", good2->version()->GetString());
  EXPECT_TRUE(service_->IsIncognitoEnabled(good2->id()));
  EXPECT_TRUE(service_->extension_prefs()->DidExtensionEscalatePermissions(
      good2->id()));
}

// Tests that updating preserves extension location.
TEST_F(ExtensionServiceTest, UpdateExtensionPreservesLocation) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");

  const Extension* good =
      InstallCRXWithLocation(path, Manifest::EXTERNAL_PREF, INSTALL_NEW);

  ASSERT_EQ("1.0.0.0", good->VersionString());
  ASSERT_EQ(good_crx, good->id());

  path = data_dir_.AppendASCII("good2.crx");
  UpdateExtension(good_crx, path, ENABLED);
  const Extension* good2 = service_->GetExtensionById(good_crx, false);
  ASSERT_EQ("1.0.0.1", good2->version()->GetString());
  EXPECT_EQ(good2->location(), Manifest::EXTERNAL_PREF);
}

// Makes sure that LOAD extension types can downgrade.
TEST_F(ExtensionServiceTest, LoadExtensionsCanDowngrade) {
  InitializeEmptyExtensionService();

  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  // We'll write the extension manifest dynamically to a temporary path
  // to make it easier to change the version number.
  base::FilePath extension_path = temp.path();
  base::FilePath manifest_path =
      extension_path.Append(extensions::kManifestFilename);
  ASSERT_FALSE(base::PathExists(manifest_path));

  // Start with version 2.0.
  DictionaryValue manifest;
  manifest.SetString("version", "2.0");
  manifest.SetString("name", "LOAD Downgrade Test");
  manifest.SetInteger("manifest_version", 2);

  JSONFileValueSerializer serializer(manifest_path);
  ASSERT_TRUE(serializer.Serialize(manifest));

  extensions::UnpackedInstaller::Create(service_)->Load(extension_path);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  EXPECT_EQ(Manifest::UNPACKED, loaded_[0]->location());
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ("2.0", loaded_[0]->VersionString());

  // Now set the version number to 1.0, reload the extensions and verify that
  // the downgrade was accepted.
  manifest.SetString("version", "1.0");
  ASSERT_TRUE(serializer.Serialize(manifest));

  extensions::UnpackedInstaller::Create(service_)->Load(extension_path);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  EXPECT_EQ(Manifest::UNPACKED, loaded_[0]->location());
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ("1.0", loaded_[0]->VersionString());
}

#if !defined(OS_CHROMEOS)
// LOAD extensions with plugins require approval.
TEST_F(ExtensionServiceTest, LoadExtensionsWithPlugins) {
  base::FilePath extension_with_plugin_path = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII(good1)
      .AppendASCII("2");
  base::FilePath extension_no_plugin_path = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII(good2)
      .AppendASCII("1.0");

  InitPluginService();
  InitializeEmptyExtensionService();
  InitializeExtensionProcessManager();
  service_->set_show_extensions_prompts(true);

  // Start by canceling any install prompts.
  CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      switches::kAppsGalleryInstallAutoConfirmForTests,
      "cancel");

  // The extension that has a plugin should not install.
  extensions::UnpackedInstaller::Create(service_)->Load(
      extension_with_plugin_path);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, GetErrors().size());
  EXPECT_EQ(0u, loaded_.size());
  EXPECT_EQ(0u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());

  // But the extension with no plugin should since there's no prompt.
  ExtensionErrorReporter::GetInstance()->ClearErrors();
  extensions::UnpackedInstaller::Create(service_)->Load(
      extension_no_plugin_path);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, GetErrors().size());
  EXPECT_EQ(1u, loaded_.size());
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());
  EXPECT_TRUE(service_->extensions()->Contains(good2));

  // The plugin extension should install if we accept the dialog.
  CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      switches::kAppsGalleryInstallAutoConfirmForTests,
      "accept");

  ExtensionErrorReporter::GetInstance()->ClearErrors();
  extensions::UnpackedInstaller::Create(service_)->Load(
      extension_with_plugin_path);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, GetErrors().size());
  EXPECT_EQ(2u, loaded_.size());
  EXPECT_EQ(2u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());
  EXPECT_TRUE(service_->extensions()->Contains(good1));
  EXPECT_TRUE(service_->extensions()->Contains(good2));

  // Make sure the granted permissions have been setup.
  scoped_refptr<PermissionSet> permissions(
      service_->extension_prefs()->GetGrantedPermissions(good1));
  EXPECT_FALSE(permissions->IsEmpty());
  EXPECT_TRUE(permissions->HasEffectiveFullAccess());
  EXPECT_FALSE(permissions->apis().empty());
  EXPECT_TRUE(permissions->HasAPIPermission(APIPermission::kPlugin));

  // We should be able to reload the extension without getting another prompt.
  loaded_.clear();
  CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      switches::kAppsGalleryInstallAutoConfirmForTests,
      "cancel");

  service_->ReloadExtension(good1);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, loaded_.size());
  EXPECT_EQ(2u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());
}
#endif

namespace {

bool IsExtension(const Extension* extension) {
  return extension->GetType() == Manifest::TYPE_EXTENSION;
}

}  // namespace

// Test adding a pending extension.
TEST_F(ExtensionServiceTest, AddPendingExtensionFromSync) {
  InitializeEmptyExtensionService();

  const std::string kFakeId(all_zero);
  const GURL kFakeUpdateURL("http:://fake.update/url");
  const bool kFakeInstallSilently(true);

  EXPECT_TRUE(service_->pending_extension_manager()->AddFromSync(
      kFakeId, kFakeUpdateURL, &IsExtension,
      kFakeInstallSilently));

  const extensions::PendingExtensionInfo* pending_extension_info;
  ASSERT_TRUE((pending_extension_info = service_->pending_extension_manager()->
      GetById(kFakeId)));
  EXPECT_EQ(kFakeUpdateURL, pending_extension_info->update_url());
  EXPECT_EQ(&IsExtension, pending_extension_info->should_allow_install_);
  EXPECT_EQ(kFakeInstallSilently, pending_extension_info->install_silently());
}

namespace {
const char kGoodId[] = "ldnnhddmnhbkjipkidpdiheffobcpfmf";
const char kGoodUpdateURL[] = "http://good.update/url";
const bool kGoodIsFromSync = true;
const bool kGoodInstallSilently = true;
}  // namespace

// Test updating a pending extension.
TEST_F(ExtensionServiceTest, UpdatePendingExtension) {
  InitializeEmptyExtensionService();
  EXPECT_TRUE(service_->pending_extension_manager()->AddFromSync(
      kGoodId, GURL(kGoodUpdateURL), &IsExtension,
      kGoodInstallSilently));
  EXPECT_TRUE(service_->pending_extension_manager()->IsIdPending(kGoodId));

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  UpdateExtension(kGoodId, path, ENABLED);

  EXPECT_FALSE(service_->pending_extension_manager()->IsIdPending(kGoodId));

  const Extension* extension = service_->GetExtensionById(kGoodId, true);
  ASSERT_TRUE(extension);
}

namespace {

bool IsTheme(const Extension* extension) {
  return extension->is_theme();
}

}  // namespace

// Test updating a pending theme.
// Disabled due to ASAN failure. http://crbug.com/108320
TEST_F(ExtensionServiceTest, DISABLED_UpdatePendingTheme) {
  InitializeEmptyExtensionService();
  EXPECT_TRUE(service_->pending_extension_manager()->AddFromSync(
      theme_crx, GURL(), &IsTheme, false));
  EXPECT_TRUE(service_->pending_extension_manager()->IsIdPending(theme_crx));

  base::FilePath path = data_dir_.AppendASCII("theme.crx");
  UpdateExtension(theme_crx, path, ENABLED);

  EXPECT_FALSE(service_->pending_extension_manager()->IsIdPending(theme_crx));

  const Extension* extension = service_->GetExtensionById(theme_crx, true);
  ASSERT_TRUE(extension);

  EXPECT_FALSE(
      service_->extension_prefs()->IsExtensionDisabled(extension->id()));
  EXPECT_TRUE(service_->IsExtensionEnabled(theme_crx));
}

#if defined(OS_CHROMEOS)
// Always fails on ChromeOS: http://crbug.com/79737
#define MAYBE_UpdatePendingExternalCrx DISABLED_UpdatePendingExternalCrx
#else
#define MAYBE_UpdatePendingExternalCrx UpdatePendingExternalCrx
#endif
// Test updating a pending CRX as if the source is an external extension
// with an update URL.  In this case we don't know if the CRX is a theme
// or not.
TEST_F(ExtensionServiceTest, MAYBE_UpdatePendingExternalCrx) {
  InitializeEmptyExtensionService();
  EXPECT_TRUE(service_->pending_extension_manager()->AddFromExternalUpdateUrl(
      theme_crx, GURL(), Manifest::EXTERNAL_PREF_DOWNLOAD));

  EXPECT_TRUE(service_->pending_extension_manager()->IsIdPending(theme_crx));

  base::FilePath path = data_dir_.AppendASCII("theme.crx");
  UpdateExtension(theme_crx, path, ENABLED);

  EXPECT_FALSE(service_->pending_extension_manager()->IsIdPending(theme_crx));

  const Extension* extension = service_->GetExtensionById(theme_crx, true);
  ASSERT_TRUE(extension);

  EXPECT_FALSE(
      service_->extension_prefs()->IsExtensionDisabled(extension->id()));
  EXPECT_TRUE(service_->IsExtensionEnabled(extension->id()));
  EXPECT_FALSE(service_->IsIncognitoEnabled(extension->id()));
}

// Test updating a pending CRX as if the source is an external extension
// with an update URL.  The external update should overwrite a sync update,
// but a sync update should not overwrite a non-sync update.
TEST_F(ExtensionServiceTest, UpdatePendingExternalCrxWinsOverSync) {
  InitializeEmptyExtensionService();

  // Add a crx to be installed from the update mechanism.
  EXPECT_TRUE(service_->pending_extension_manager()->AddFromSync(
      kGoodId, GURL(kGoodUpdateURL), &IsExtension,
      kGoodInstallSilently));

  // Check that there is a pending crx, with is_from_sync set to true.
  const extensions::PendingExtensionInfo* pending_extension_info;
  ASSERT_TRUE((pending_extension_info = service_->pending_extension_manager()->
      GetById(kGoodId)));
  EXPECT_TRUE(pending_extension_info->is_from_sync());

  // Add a crx to be updated, with the same ID, from a non-sync source.
  EXPECT_TRUE(service_->pending_extension_manager()->AddFromExternalUpdateUrl(
      kGoodId, GURL(kGoodUpdateURL), Manifest::EXTERNAL_PREF_DOWNLOAD));

  // Check that there is a pending crx, with is_from_sync set to false.
  ASSERT_TRUE((pending_extension_info = service_->pending_extension_manager()->
      GetById(kGoodId)));
  EXPECT_FALSE(pending_extension_info->is_from_sync());
  EXPECT_EQ(Manifest::EXTERNAL_PREF_DOWNLOAD,
            pending_extension_info->install_source());

  // Add a crx to be installed from the update mechanism.
  EXPECT_FALSE(service_->pending_extension_manager()->AddFromSync(
      kGoodId, GURL(kGoodUpdateURL), &IsExtension,
      kGoodInstallSilently));

  // Check that the external, non-sync update was not overridden.
  ASSERT_TRUE((pending_extension_info = service_->pending_extension_manager()->
      GetById(kGoodId)));
  EXPECT_FALSE(pending_extension_info->is_from_sync());
  EXPECT_EQ(Manifest::EXTERNAL_PREF_DOWNLOAD,
            pending_extension_info->install_source());
}

// Updating a theme should fail if the updater is explicitly told that
// the CRX is not a theme.
TEST_F(ExtensionServiceTest, UpdatePendingCrxThemeMismatch) {
  InitializeEmptyExtensionService();
  EXPECT_TRUE(service_->pending_extension_manager()->AddFromSync(
      theme_crx, GURL(), &IsExtension, true));

  EXPECT_TRUE(service_->pending_extension_manager()->IsIdPending(theme_crx));

  base::FilePath path = data_dir_.AppendASCII("theme.crx");
  UpdateExtension(theme_crx, path, FAILED_SILENTLY);

  EXPECT_FALSE(service_->pending_extension_manager()->IsIdPending(theme_crx));

  const Extension* extension = service_->GetExtensionById(theme_crx, true);
  ASSERT_FALSE(extension);
}

// TODO(akalin): Test updating a pending extension non-silently once
// we can mock out ExtensionInstallUI and inject our version into
// UpdateExtension().

// Test updating a pending extension which fails the should-install test.
TEST_F(ExtensionServiceTest, UpdatePendingExtensionFailedShouldInstallTest) {
  InitializeEmptyExtensionService();
  // Add pending extension with a flipped is_theme.
  EXPECT_TRUE(service_->pending_extension_manager()->AddFromSync(
      kGoodId, GURL(kGoodUpdateURL), &IsTheme, kGoodInstallSilently));
  EXPECT_TRUE(service_->pending_extension_manager()->IsIdPending(kGoodId));

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  UpdateExtension(kGoodId, path, UPDATED);

  // TODO(akalin): Figure out how to check that the extensions
  // directory is cleaned up properly in OnExtensionInstalled().

  EXPECT_FALSE(service_->pending_extension_manager()->IsIdPending(kGoodId));
}

// TODO(akalin): Figure out how to test that installs of pending
// unsyncable extensions are blocked.

// Test updating a pending extension for one that is not pending.
TEST_F(ExtensionServiceTest, UpdatePendingExtensionNotPending) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  UpdateExtension(kGoodId, path, UPDATED);

  EXPECT_FALSE(service_->pending_extension_manager()->IsIdPending(kGoodId));
}

// Test updating a pending extension for one that is already
// installed.
TEST_F(ExtensionServiceTest, UpdatePendingExtensionAlreadyInstalled) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  const Extension* good = InstallCRX(path, INSTALL_NEW);
  ASSERT_EQ(1u, service_->extensions()->size());

  EXPECT_FALSE(good->is_theme());

  // Use AddExtensionImpl() as AddFrom*() would balk.
  service_->pending_extension_manager()->AddExtensionImpl(
      good->id(), extensions::ManifestURL::GetUpdateURL(good),
      Version(), &IsExtension, kGoodIsFromSync,
      kGoodInstallSilently, Manifest::INTERNAL);
  UpdateExtension(good->id(), path, ENABLED);

  EXPECT_FALSE(service_->pending_extension_manager()->IsIdPending(kGoodId));
}

// Test pref settings for blacklist and unblacklist extensions.
TEST_F(ExtensionServiceTest, SetUnsetBlacklistInPrefs) {
  InitializeEmptyExtensionService();
  std::vector<std::string> blacklist;
  blacklist.push_back(good0);
  blacklist.push_back("invalid_id");  // an invalid id
  blacklist.push_back(good1);
  ExtensionSystem::Get(profile_.get())->blacklist()->SetFromUpdater(blacklist,
                                                                    "v1");

  // Make sure pref is updated
  base::RunLoop().RunUntilIdle();

  // blacklist is set for good0,1,2
  ValidateBooleanPref(good0, "blacklist", true);
  ValidateBooleanPref(good1, "blacklist", true);
  // invalid_id should not be inserted to pref.
  EXPECT_FALSE(IsPrefExist("invalid_id", "blacklist"));

  // remove good1, add good2
  blacklist.pop_back();
  blacklist.push_back(good2);
  ExtensionSystem::Get(profile_.get())->blacklist()->SetFromUpdater(blacklist,
                                                                    "v2");

  // only good0 and good1 should be set
  ValidateBooleanPref(good0, "blacklist", true);
  EXPECT_FALSE(IsPrefExist(good1, "blacklist"));
  ValidateBooleanPref(good2, "blacklist", true);
  EXPECT_FALSE(IsPrefExist("invalid_id", "blacklist"));
}

// Unload installed extension from blacklist.
TEST_F(ExtensionServiceTest, UnloadBlacklistedExtension) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");

  const Extension* good = InstallCRX(path, INSTALL_NEW);
  EXPECT_EQ(good_crx, good->id());
  UpdateExtension(good_crx, path, FAILED_SILENTLY);

  std::vector<std::string> blacklist;
  blacklist.push_back(good_crx);
  ExtensionSystem::Get(profile_.get())->blacklist()->SetFromUpdater(blacklist,
                                                                     "v1");

  // Make sure pref is updated
  base::RunLoop().RunUntilIdle();

  // Now, the good_crx is blacklisted.
  ValidateBooleanPref(good_crx, "blacklist", true);
  EXPECT_EQ(0u, service_->extensions()->size());

  // Remove good_crx from blacklist
  blacklist.pop_back();
  ExtensionSystem::Get(profile_.get())->blacklist()->SetFromUpdater(blacklist,
                                                                     "v2");

  // Make sure pref is updated
  base::RunLoop().RunUntilIdle();
  // blacklist value should not be set for good_crx
  EXPECT_FALSE(IsPrefExist(good_crx, "blacklist"));
}

// Unload installed extension from blacklist.
TEST_F(ExtensionServiceTest, BlacklistedExtensionWillNotInstall) {
  InitializeEmptyExtensionService();

  // Fake the blacklisting of good_crx by pretending that we get an update
  // which includes it.
  extensions::Blacklist* blacklist =
      ExtensionSystem::Get(profile_.get())->blacklist();
  blacklist->SetFromUpdater(std::vector<std::string>(1, good_crx), "v1");

  // Now good_crx is blacklisted.
  ValidateBooleanPref(good_crx, "blacklist", true);

  // We cannot install good_crx.
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  // HACK: specify WAS_INSTALLED_BY_DEFAULT so that test machinery doesn't
  // decide to install this silently. Somebody should fix these tests, all
  // 6,000 lines of them. Hah!
  InstallCRX(path, INSTALL_FAILED, Extension::WAS_INSTALLED_BY_DEFAULT);
  EXPECT_EQ(0u, service_->extensions()->size());
  ValidateBooleanPref(good_crx, "blacklist", true);
}

// Unload blacklisted extension on policy change.
TEST_F(ExtensionServiceTest, UnloadBlacklistedExtensionPolicy) {
  InitializeEmptyExtensionService();
  base::FilePath path = data_dir_.AppendASCII("good.crx");

  const Extension* good = InstallCRX(path, INSTALL_NEW);
  EXPECT_EQ(good_crx, good->id());
  UpdateExtension(good_crx, path, FAILED_SILENTLY);
  EXPECT_EQ(1u, service_->extensions()->size());

  base::ListValue whitelist;
  PrefService* prefs = service_->extension_prefs()->pref_service();
  whitelist.Append(base::Value::CreateStringValue(good_crx));
  prefs->Set(prefs::kExtensionInstallAllowList, whitelist);

  std::vector<std::string> blacklist;
  blacklist.push_back(good_crx);
  ExtensionSystem::Get(profile_.get())->blacklist()->SetFromUpdater(blacklist,
                                                                    "v1");

  // Make sure pref is updated
  base::RunLoop().RunUntilIdle();

  // The good_crx is blacklisted and the whitelist doesn't negate it.
  ValidateBooleanPref(good_crx, "blacklist", true);
  EXPECT_EQ(0u, service_->extensions()->size());
}

// Test loading extensions from the profile directory, except
// blacklisted ones.
TEST_F(ExtensionServiceTest, WillNotLoadBlacklistedExtensionsFromDirectory) {
  // Initialize the test dir with a good Preferences/extensions.
  base::FilePath source_install_dir = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions");
  base::FilePath pref_path = source_install_dir
      .DirName()
      .AppendASCII("Preferences");
  InitializeInstalledExtensionService(pref_path, source_install_dir);

  // Blacklist good1.
  std::vector<std::string> blacklist;
  blacklist.push_back(good1);
  ExtensionSystem::Get(profile_.get())->blacklist()->SetFromUpdater(blacklist,
                                                                    "v1");

  // Make sure pref is updated
  base::RunLoop().RunUntilIdle();

  ValidateBooleanPref(good1, "blacklist", true);

  // Load extensions.
  service_->Init();

  std::vector<string16> errors = GetErrors();
  for (std::vector<string16>::iterator err = errors.begin();
    err != errors.end(); ++err) {
    LOG(ERROR) << *err;
  }
  ASSERT_EQ(2u, loaded_.size());

  EXPECT_TRUE(service_->GetInstalledExtension(good1));
  int include_mask = ExtensionService::INCLUDE_EVERYTHING &
                    ~ExtensionService::INCLUDE_BLACKLISTED;
  EXPECT_FALSE(service_->GetExtensionById(good1, include_mask));
}

// Will not install extension blacklisted by policy.
TEST_F(ExtensionServiceTest, BlacklistedByPolicyWillNotInstall) {
  InitializeEmptyExtensionService();

  // Blacklist everything.
  {
    ListPrefUpdate update(profile_->GetPrefs(),
                          prefs::kExtensionInstallDenyList);
    ListValue* blacklist = update.Get();
    blacklist->Append(Value::CreateStringValue("*"));
  }

  // Blacklist prevents us from installing good_crx.
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_FAILED);
  EXPECT_EQ(0u, service_->extensions()->size());

  // Now whitelist this particular extension.
  {
    ListPrefUpdate update(profile_->GetPrefs(),
                          prefs::kExtensionInstallAllowList);
    ListValue* whitelist = update.Get();
    whitelist->Append(Value::CreateStringValue(good_crx));
  }

  // Ensure we can now install good_crx.
  InstallCRX(path, INSTALL_NEW);
  EXPECT_EQ(1u, service_->extensions()->size());
}

// Extension blacklisted by policy get unloaded after installing.
TEST_F(ExtensionServiceTest, BlacklistedByPolicyRemovedIfRunning) {
  InitializeEmptyExtensionService();

  // Install good_crx.
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_NEW);
  EXPECT_EQ(1u, service_->extensions()->size());

  { // Scope for pref update notification.
    PrefService* prefs = profile_->GetPrefs();
    ListPrefUpdate update(prefs, prefs::kExtensionInstallDenyList);
    ListValue* blacklist = update.Get();
    ASSERT_TRUE(blacklist != NULL);

    // Blacklist this extension.
    blacklist->Append(Value::CreateStringValue(good_crx));
  }

  // Extension should not be running now.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, service_->extensions()->size());
}

// Tests that component extensions are not blacklisted by policy.
TEST_F(ExtensionServiceTest, ComponentExtensionWhitelisted) {
  InitializeEmptyExtensionService();

  // Blacklist everything.
  {
    ListPrefUpdate update(profile_->GetPrefs(),
                          prefs::kExtensionInstallDenyList);
    ListValue* blacklist = update.Get();
    blacklist->Append(Value::CreateStringValue("*"));
  }

  // Install a component extension.
  base::FilePath path = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII(good0)
      .AppendASCII("1.0.0.0");
  std::string manifest;
  ASSERT_TRUE(file_util::ReadFileToString(
      path.Append(extensions::kManifestFilename), &manifest));
  service_->component_loader()->Add(manifest, path);
  service_->Init();

  // Extension should be installed despite blacklist.
  ASSERT_EQ(1u, service_->extensions()->size());
  EXPECT_TRUE(service_->GetExtensionById(good0, false));

  // Poke external providers and make sure the extension is still present.
  service_->CheckForExternalUpdates();
  ASSERT_EQ(1u, service_->extensions()->size());
  EXPECT_TRUE(service_->GetExtensionById(good0, false));

  // Extension should not be uninstalled on blacklist changes.
  {
    ListPrefUpdate update(profile_->GetPrefs(),
                          prefs::kExtensionInstallDenyList);
    ListValue* blacklist = update.Get();
    blacklist->Append(Value::CreateStringValue(good0));
  }
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(1u, service_->extensions()->size());
  EXPECT_TRUE(service_->GetExtensionById(good0, false));
}

// Tests that policy-installed extensions are not blacklisted by policy.
TEST_F(ExtensionServiceTest, PolicyInstalledExtensionsWhitelisted) {
  InitializeEmptyExtensionService();

  {
    // Blacklist everything.
    ListPrefUpdate blacklist_update(profile_->GetPrefs(),
                                    prefs::kExtensionInstallDenyList);
    ListValue* blacklist = blacklist_update.Get();
    blacklist->AppendString("*");

    // Mark good.crx for force-installation.
    DictionaryPrefUpdate forcelist_update(profile_->GetPrefs(),
                                          prefs::kExtensionInstallForceList);
    extensions::ExternalPolicyLoader::AddExtension(
        forcelist_update.Get(), good_crx, "http://example.com/update_url");
  }

  // Have policy force-install an extension.
  MockExtensionProvider* provider =
      new MockExtensionProvider(service_,
                                Manifest::EXTERNAL_POLICY_DOWNLOAD);
  AddMockExternalProvider(provider);
  provider->UpdateOrAddExtension(good_crx, "1.0.0.0",
                                 data_dir_.AppendASCII("good.crx"));

  // Reloading extensions should find our externally registered extension
  // and install it.
  service_->CheckForExternalUpdates();
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();

  // Extension should be installed despite blacklist.
  ASSERT_EQ(1u, service_->extensions()->size());
  EXPECT_TRUE(service_->GetExtensionById(good_crx, false));

  // Blacklist update should not uninstall the extension.
  {
    ListPrefUpdate update(profile_->GetPrefs(),
                          prefs::kExtensionInstallDenyList);
    ListValue* blacklist = update.Get();
    blacklist->Append(Value::CreateStringValue(good0));
  }
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(1u, service_->extensions()->size());
  EXPECT_TRUE(service_->GetExtensionById(good_crx, false));
}

// Tests that extensions cannot be installed if the policy provider prohibits
// it. This functionality is implemented in CrxInstaller::ConfirmInstall().
TEST_F(ExtensionServiceTest, ManagementPolicyProhibitsInstall) {
  InitializeEmptyExtensionService();

  management_policy_->UnregisterAllProviders();
  extensions::TestManagementPolicyProvider provider_(
      extensions::TestManagementPolicyProvider::PROHIBIT_LOAD);
  management_policy_->RegisterProvider(&provider_);

  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_FAILED);
  EXPECT_EQ(0u, service_->extensions()->size());
}

// Tests that extensions cannot be loaded from prefs if the policy provider
// prohibits it. This functionality is implemented in InstalledLoader::Load().
TEST_F(ExtensionServiceTest, ManagementPolicyProhibitsLoadFromPrefs) {
  InitializeEmptyExtensionService();

  // Create a fake extension to be loaded as though it were read from prefs.
  base::FilePath path = data_dir_.AppendASCII("management")
                           .AppendASCII("simple_extension");
  DictionaryValue manifest;
  manifest.SetString(keys::kName, "simple_extension");
  manifest.SetString(keys::kVersion, "1");
  // UNPACKED is for extensions loaded from a directory. We use it here, even
  // though we're testing loading from prefs, so that we don't need to provide
  // an extension key.
  extensions::ExtensionInfo extension_info(
      &manifest, std::string(), path, Manifest::UNPACKED);

  // Ensure we can load it with no management policy in place.
  management_policy_->UnregisterAllProviders();
  EXPECT_EQ(0u, service_->extensions()->size());
  extensions::InstalledLoader(service_).Load(extension_info, false);
  EXPECT_EQ(1u, service_->extensions()->size());

  const Extension* extension = (service_->extensions()->begin())->get();
  EXPECT_TRUE(service_->UninstallExtension(extension->id(), false, NULL));
  EXPECT_EQ(0u, service_->extensions()->size());

  // Ensure we cannot load it if management policy prohibits installation.
  extensions::TestManagementPolicyProvider provider_(
      extensions::TestManagementPolicyProvider::PROHIBIT_LOAD);
  management_policy_->RegisterProvider(&provider_);

  extensions::InstalledLoader(service_).Load(extension_info, false);
  EXPECT_EQ(0u, service_->extensions()->size());
}

// Tests disabling an extension when prohibited by the ManagementPolicy.
TEST_F(ExtensionServiceTest, ManagementPolicyProhibitsDisable) {
  InitializeEmptyExtensionService();

  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());

  management_policy_->UnregisterAllProviders();
  extensions::TestManagementPolicyProvider provider(
      extensions::TestManagementPolicyProvider::PROHIBIT_MODIFY_STATUS);
  management_policy_->RegisterProvider(&provider);

  // Attempt to disable it.
  service_->DisableExtension(good_crx, Extension::DISABLE_USER_ACTION);

  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_TRUE(service_->GetExtensionById(good_crx, false));
  EXPECT_EQ(0u, service_->disabled_extensions()->size());
}

// Tests uninstalling an extension when prohibited by the ManagementPolicy.
TEST_F(ExtensionServiceTest, ManagementPolicyProhibitsUninstall) {
  InitializeEmptyExtensionService();

  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());

  management_policy_->UnregisterAllProviders();
  extensions::TestManagementPolicyProvider provider(
      extensions::TestManagementPolicyProvider::PROHIBIT_MODIFY_STATUS);
  management_policy_->RegisterProvider(&provider);

  // Attempt to uninstall it.
  EXPECT_FALSE(service_->UninstallExtension(good_crx, false, NULL));

  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_TRUE(service_->GetExtensionById(good_crx, false));
}

// Tests that previously installed extensions that are now prohibited from
// being installed are removed.
TEST_F(ExtensionServiceTest, ManagementPolicyUnloadsAllProhibited) {
  InitializeEmptyExtensionService();

  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  InstallCRX(data_dir_.AppendASCII("page_action.crx"), INSTALL_NEW);
  EXPECT_EQ(2u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());

  management_policy_->UnregisterAllProviders();
  extensions::TestManagementPolicyProvider provider(
      extensions::TestManagementPolicyProvider::PROHIBIT_LOAD);
  management_policy_->RegisterProvider(&provider);

  // Run the policy check.
  service_->CheckManagementPolicy();
  EXPECT_EQ(0u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());
}

// Tests that previously disabled extensions that are now required to be
// enabled are re-enabled on reinstall.
TEST_F(ExtensionServiceTest, ManagementPolicyRequiresEnable) {
  InitializeEmptyExtensionService();

  // Install, then disable, an extension.
  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  EXPECT_EQ(1u, service_->extensions()->size());
  service_->DisableExtension(good_crx, Extension::DISABLE_USER_ACTION);
  EXPECT_EQ(1u, service_->disabled_extensions()->size());

  // Register an ExtensionMnagementPolicy that requires the extension to remain
  // enabled.
  management_policy_->UnregisterAllProviders();
  extensions::TestManagementPolicyProvider provider(
      extensions::TestManagementPolicyProvider::MUST_REMAIN_ENABLED);
  management_policy_->RegisterProvider(&provider);

  // Reinstall the extension.
  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_UPDATED);
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());
}

TEST_F(ExtensionServiceTest, ExternalExtensionAutoAcknowledgement) {
  InitializeEmptyExtensionService();
  set_extensions_enabled(true);

  {
    // Register and install an external extension.
    MockExtensionProvider* provider =
        new MockExtensionProvider(service_, Manifest::EXTERNAL_PREF);
    AddMockExternalProvider(provider);
    provider->UpdateOrAddExtension(good_crx, "1.0.0.0",
                                   data_dir_.AppendASCII("good.crx"));
  }
  {
    // Have policy force-install an extension.
    MockExtensionProvider* provider =
        new MockExtensionProvider(service_,
                                  Manifest::EXTERNAL_POLICY_DOWNLOAD);
    AddMockExternalProvider(provider);
    provider->UpdateOrAddExtension(page_action, "1.0.0.0",
                                   data_dir_.AppendASCII("page_action.crx"));
  }

  // Providers are set up. Let them run.
  service_->CheckForExternalUpdates();

  int count = 2;
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      base::Bind(&WaitForCountNotificationsCallback, &count)).Wait();

  ASSERT_EQ(2u, service_->extensions()->size());
  EXPECT_TRUE(service_->GetExtensionById(good_crx, false));
  EXPECT_TRUE(service_->GetExtensionById(page_action, false));
  ExtensionPrefs* prefs = service_->extension_prefs();
  ASSERT_TRUE(!prefs->IsExternalExtensionAcknowledged(good_crx));
  ASSERT_TRUE(prefs->IsExternalExtensionAcknowledged(page_action));
}

#if !defined(OS_CHROMEOS)
// This tests if default apps are installed correctly.
TEST_F(ExtensionServiceTest, DefaultAppsInstall) {
  InitializeEmptyExtensionService();
  set_extensions_enabled(true);

  {
    std::string json_data =
        "{"
        "  \"ldnnhddmnhbkjipkidpdiheffobcpfmf\" : {"
        "    \"external_crx\": \"good.crx\","
        "    \"external_version\": \"1.0.0.0\","
        "    \"is_bookmark_app\": false"
        "  }"
        "}";
    default_apps::Provider* provider =
        new default_apps::Provider(
            profile_.get(),
            service_,
            new extensions::ExternalTestingLoader(json_data, data_dir_),
            Manifest::INTERNAL,
            Manifest::INVALID_LOCATION,
            Extension::FROM_WEBSTORE | Extension::WAS_INSTALLED_BY_DEFAULT);

    AddMockExternalProvider(provider);
  }

  ASSERT_EQ(0u, service_->extensions()->size());
  service_->CheckForExternalUpdates();
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();

  ASSERT_EQ(1u, service_->extensions()->size());
  EXPECT_TRUE(service_->GetExtensionById(good_crx, false));
  const Extension* extension = service_->GetExtensionById(good_crx, false);
  EXPECT_TRUE(extension->from_webstore());
  EXPECT_TRUE(extension->was_installed_by_default());
}
#endif

// Tests disabling extensions
TEST_F(ExtensionServiceTest, DisableExtension) {
  InitializeEmptyExtensionService();

  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  EXPECT_FALSE(service_->extensions()->is_empty());
  EXPECT_TRUE(service_->GetExtensionById(good_crx, true));
  EXPECT_TRUE(service_->GetExtensionById(good_crx, false));
  EXPECT_TRUE(service_->disabled_extensions()->is_empty());

  // Disable it.
  service_->DisableExtension(good_crx, Extension::DISABLE_USER_ACTION);

  EXPECT_TRUE(service_->extensions()->is_empty());
  EXPECT_TRUE(service_->GetExtensionById(good_crx, true));
  EXPECT_FALSE(service_->GetExtensionById(good_crx, false));
  EXPECT_FALSE(service_->disabled_extensions()->is_empty());
}

TEST_F(ExtensionServiceTest, DisableTerminatedExtension) {
  InitializeEmptyExtensionService();

  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  TerminateExtension(good_crx);
  EXPECT_TRUE(service_->GetTerminatedExtension(good_crx));

  // Disable it.
  service_->DisableExtension(good_crx, Extension::DISABLE_USER_ACTION);

  EXPECT_FALSE(service_->GetTerminatedExtension(good_crx));
  EXPECT_TRUE(service_->GetExtensionById(good_crx, true));
  EXPECT_FALSE(service_->disabled_extensions()->is_empty());
}

// Tests disabling all extensions (simulating --disable-extensions flag).
TEST_F(ExtensionServiceTest, DisableAllExtensions) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_NEW);

  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());

  // Disable extensions.
  service_->set_extensions_enabled(false);
  service_->ReloadExtensions();

  // There shouldn't be extensions in either list.
  EXPECT_EQ(0u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());

  // This shouldn't do anything when all extensions are disabled.
  service_->EnableExtension(good_crx);
  service_->ReloadExtensions();

  // There still shouldn't be extensions in either list.
  EXPECT_EQ(0u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());

  // And then re-enable the extensions.
  service_->set_extensions_enabled(true);
  service_->ReloadExtensions();

  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());
}

// Tests reloading extensions.
TEST_F(ExtensionServiceTest, ReloadExtensions) {
  InitializeEmptyExtensionService();

  // Simple extension that should install without error.
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_NEW,
             Extension::FROM_WEBSTORE | Extension::WAS_INSTALLED_BY_DEFAULT);
  const char* extension_id = good_crx;
  service_->DisableExtension(extension_id, Extension::DISABLE_USER_ACTION);

  EXPECT_EQ(0u, service_->extensions()->size());
  EXPECT_EQ(1u, service_->disabled_extensions()->size());

  service_->ReloadExtensions();

  // The creation flags should not change when reloading the extension.
  const Extension* extension = service_->GetExtensionById(good_crx, true);
  EXPECT_TRUE(extension->from_webstore());
  EXPECT_TRUE(extension->was_installed_by_default());
  EXPECT_FALSE(extension->from_bookmark());

  // Extension counts shouldn't change.
  EXPECT_EQ(0u, service_->extensions()->size());
  EXPECT_EQ(1u, service_->disabled_extensions()->size());

  service_->EnableExtension(extension_id);

  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());

  // Need to clear |loaded_| manually before reloading as the
  // EnableExtension() call above inserted into it and
  // UnloadAllExtensions() doesn't send out notifications.
  loaded_.clear();
  service_->ReloadExtensions();

  // Extension counts shouldn't change.
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());
}

// Tests reloading an extension.
TEST_F(ExtensionServiceTest, ReloadExtension) {
  InitializeEmptyExtensionService();
  InitializeExtensionProcessManager();

  // Simple extension that should install without error.
  const char* extension_id = "behllobkkfkfnphdnhnkndlbkcpglgmj";
  base::FilePath ext = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII(extension_id)
      .AppendASCII("1.0.0.0");
  extensions::UnpackedInstaller::Create(service_)->Load(ext);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());

  service_->ReloadExtension(extension_id);

  // Extension should be disabled now, waiting to be reloaded.
  EXPECT_EQ(0u, service_->extensions()->size());
  EXPECT_EQ(1u, service_->disabled_extensions()->size());
  EXPECT_EQ(Extension::DISABLE_RELOAD,
            service_->extension_prefs()->GetDisableReasons(extension_id));

  // Reloading again should not crash.
  service_->ReloadExtension(extension_id);

  // Finish reloading
  base::RunLoop().RunUntilIdle();

  // Extension should be enabled again.
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(0u, service_->disabled_extensions()->size());
}

TEST_F(ExtensionServiceTest, UninstallExtension) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  EXPECT_EQ(1u, service_->extensions()->size());
  UninstallExtension(good_crx, false);
  EXPECT_EQ(0u, service_->extensions()->size());
}

TEST_F(ExtensionServiceTest, UninstallTerminatedExtension) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  TerminateExtension(good_crx);
  UninstallExtension(good_crx, false);
}

// Tests the uninstaller helper.
TEST_F(ExtensionServiceTest, UninstallExtensionHelper) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  UninstallExtension(good_crx, true);
}

TEST_F(ExtensionServiceTest, UninstallExtensionHelperTerminated) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  TerminateExtension(good_crx);
  UninstallExtension(good_crx, true);
}

// An extension disabled because of unsupported requirements should re-enabled
// if updated to a version with supported requirements as long as there are no
// other disable reasons.
TEST_F(ExtensionServiceTest, UpgradingRequirementsEnabled) {
  InitializeEmptyExtensionService();
  BlackListWebGL();

  base::FilePath path = data_dir_.AppendASCII("requirements");
  base::FilePath pem_path = data_dir_.AppendASCII("requirements")
                               .AppendASCII("v1_good.pem");
  const Extension* extension_v1 = PackAndInstallCRX(path.AppendASCII("v1_good"),
                                                    pem_path,
                                                    INSTALL_NEW);
  std::string id = extension_v1->id();
  EXPECT_TRUE(service_->IsExtensionEnabled(id));

  base::FilePath v2_bad_requirements_crx = GetTemporaryFile();

  PackCRX(path.AppendASCII("v2_bad_requirements"),
          pem_path,
          v2_bad_requirements_crx);
  UpdateExtension(id, v2_bad_requirements_crx, INSTALLED);
  EXPECT_FALSE(service_->IsExtensionEnabled(id));

  base::FilePath v3_good_crx = GetTemporaryFile();

  PackCRX(path.AppendASCII("v3_good"), pem_path, v3_good_crx);
  UpdateExtension(id, v3_good_crx, ENABLED);
  EXPECT_TRUE(service_->IsExtensionEnabled(id));
}

// Extensions disabled through user action should stay disabled.
TEST_F(ExtensionServiceTest, UpgradingRequirementsDisabled) {
  InitializeEmptyExtensionService();
  BlackListWebGL();

  base::FilePath path = data_dir_.AppendASCII("requirements");
  base::FilePath pem_path = data_dir_.AppendASCII("requirements")
                               .AppendASCII("v1_good.pem");
  const Extension* extension_v1 = PackAndInstallCRX(path.AppendASCII("v1_good"),
                                                    pem_path,
                                                    INSTALL_NEW);
  std::string id = extension_v1->id();
  service_->DisableExtension(id, Extension::DISABLE_USER_ACTION);
  EXPECT_FALSE(service_->IsExtensionEnabled(id));

  base::FilePath v2_bad_requirements_crx = GetTemporaryFile();

  PackCRX(path.AppendASCII("v2_bad_requirements"),
          pem_path,
          v2_bad_requirements_crx);
  UpdateExtension(id, v2_bad_requirements_crx, INSTALLED);
  EXPECT_FALSE(service_->IsExtensionEnabled(id));

  base::FilePath v3_good_crx = GetTemporaryFile();

  PackCRX(path.AppendASCII("v3_good"), pem_path, v3_good_crx);
  UpdateExtension(id, v3_good_crx, INSTALLED);
  EXPECT_FALSE(service_->IsExtensionEnabled(id));
}

// The extension should not re-enabled because it was disabled from a
// permission increase.
TEST_F(ExtensionServiceTest, UpgradingRequirementsPermissions) {
  InitializeEmptyExtensionService();
  BlackListWebGL();

  base::FilePath path = data_dir_.AppendASCII("requirements");
  base::FilePath pem_path = data_dir_.AppendASCII("requirements")
                               .AppendASCII("v1_good.pem");
  const Extension* extension_v1 = PackAndInstallCRX(path.AppendASCII("v1_good"),
                                                    pem_path,
                                                    INSTALL_NEW);
  std::string id = extension_v1->id();
  EXPECT_TRUE(service_->IsExtensionEnabled(id));

  base::FilePath v2_bad_requirements_and_permissions_crx = GetTemporaryFile();

  PackCRX(path.AppendASCII("v2_bad_requirements_and_permissions"),
          pem_path,
          v2_bad_requirements_and_permissions_crx);
  UpdateExtension(id, v2_bad_requirements_and_permissions_crx, INSTALLED);
  EXPECT_FALSE(service_->IsExtensionEnabled(id));

  base::FilePath v3_bad_permissions_crx = GetTemporaryFile();

  PackCRX(path.AppendASCII("v3_bad_permissions"),
          pem_path,
          v3_bad_permissions_crx);
  UpdateExtension(id, v3_bad_permissions_crx, INSTALLED);
  EXPECT_FALSE(service_->IsExtensionEnabled(id));
}

// Unpacked extensions are not allowed to be installed if they have unsupported
// requirements.
TEST_F(ExtensionServiceTest, UnpackedRequirements) {
  InitializeEmptyExtensionService();
  BlackListWebGL();

  base::FilePath path = data_dir_.AppendASCII("requirements")
                           .AppendASCII("v2_bad_requirements");
  extensions::UnpackedInstaller::Create(service_)->Load(path);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, GetErrors().size());
  EXPECT_EQ(0u, service_->extensions()->size());
}

class ExtensionCookieCallback {
 public:
  ExtensionCookieCallback()
    : result_(false),
      weak_factory_(base::MessageLoop::current()) {}

  void SetCookieCallback(bool result) {
    base::MessageLoop::current()->PostTask(FROM_HERE,
        base::Bind(&base::MessageLoop::Quit, weak_factory_.GetWeakPtr()));
    result_ = result;
  }

  void GetAllCookiesCallback(const net::CookieList& list) {
    base::MessageLoop::current()->PostTask(FROM_HERE,
        base::Bind(&base::MessageLoop::Quit, weak_factory_.GetWeakPtr()));
    list_ = list;
  }
  net::CookieList list_;
  bool result_;
  base::WeakPtrFactory<base::MessageLoop> weak_factory_;
};

// Verifies extension state is removed upon uninstall.
TEST_F(ExtensionServiceTest, ClearExtensionData) {
  InitializeEmptyExtensionService();
  ExtensionCookieCallback callback;

  // Load a test extension.
  base::FilePath path = data_dir_;
  path = path.AppendASCII("good.crx");
  const Extension* extension = InstallCRX(path, INSTALL_NEW);
  ASSERT_TRUE(extension);
  GURL ext_url(extension->url());
  std::string origin_id = webkit_database::GetIdentifierFromOrigin(ext_url);

  // Set a cookie for the extension.
  net::CookieMonster* cookie_monster =
      profile_->GetRequestContextForExtensions()->GetURLRequestContext()->
      cookie_store()->GetCookieMonster();
  ASSERT_TRUE(cookie_monster);
  net::CookieOptions options;
  cookie_monster->SetCookieWithOptionsAsync(
       ext_url, "dummy=value", options,
       base::Bind(&ExtensionCookieCallback::SetCookieCallback,
                  base::Unretained(&callback)));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback.result_);

  cookie_monster->GetAllCookiesForURLAsync(
      ext_url,
      base::Bind(&ExtensionCookieCallback::GetAllCookiesCallback,
                 base::Unretained(&callback)));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1U, callback.list_.size());

  // Open a database.
  webkit_database::DatabaseTracker* db_tracker =
      BrowserContext::GetDefaultStoragePartition(profile_.get())->
          GetDatabaseTracker();
  string16 db_name = UTF8ToUTF16("db");
  string16 description = UTF8ToUTF16("db_description");
  int64 size;
  db_tracker->DatabaseOpened(origin_id, db_name, description, 1, &size);
  db_tracker->DatabaseClosed(origin_id, db_name);
  std::vector<webkit_database::OriginInfo> origins;
  db_tracker->GetAllOriginsInfo(&origins);
  EXPECT_EQ(1U, origins.size());
  EXPECT_EQ(origin_id, origins[0].GetOriginIdentifier());

  // Create local storage. We only simulate this by creating the backing files.
  // Note: This test depends on details of how the dom_storage library
  // stores data in the host file system.
  base::FilePath lso_dir_path =
      profile_->GetPath().AppendASCII("Local Storage");
  base::FilePath lso_file_path = lso_dir_path.AppendASCII(origin_id)
      .AddExtension(FILE_PATH_LITERAL(".localstorage"));
  EXPECT_TRUE(file_util::CreateDirectory(lso_dir_path));
  EXPECT_EQ(0, file_util::WriteFile(lso_file_path, NULL, 0));
  EXPECT_TRUE(base::PathExists(lso_file_path));

  // Create indexed db. Similarly, it is enough to only simulate this by
  // creating the directory on the disk.
  IndexedDBContext* idb_context =
      BrowserContext::GetDefaultStoragePartition(profile_.get())->
          GetIndexedDBContext();
  idb_context->SetTaskRunnerForTesting(
      base::MessageLoop::current()->message_loop_proxy().get());
  base::FilePath idb_path = idb_context->GetFilePathForTesting(origin_id);
  EXPECT_TRUE(file_util::CreateDirectory(idb_path));
  EXPECT_TRUE(base::DirectoryExists(idb_path));

  // Uninstall the extension.
  service_->UninstallExtension(good_crx, false, NULL);
  base::RunLoop().RunUntilIdle();

  // Check that the cookie is gone.
  cookie_monster->GetAllCookiesForURLAsync(
       ext_url,
       base::Bind(&ExtensionCookieCallback::GetAllCookiesCallback,
                  base::Unretained(&callback)));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0U, callback.list_.size());

  // The database should have vanished as well.
  origins.clear();
  db_tracker->GetAllOriginsInfo(&origins);
  EXPECT_EQ(0U, origins.size());

  // Check that the LSO file has been removed.
  EXPECT_FALSE(base::PathExists(lso_file_path));

  // Check if the indexed db has disappeared too.
  EXPECT_FALSE(base::DirectoryExists(idb_path));
}

// Verifies app state is removed upon uninstall.
TEST_F(ExtensionServiceTest, ClearAppData) {
  InitializeEmptyExtensionService();
  ExtensionCookieCallback callback;

  int pref_count = 0;

  // Install app1 with unlimited storage.
  const Extension* extension =
      PackAndInstallCRX(data_dir_.AppendASCII("app1"), INSTALL_NEW);
  ValidatePrefKeyCount(++pref_count);
  ASSERT_EQ(1u, service_->extensions()->size());
  const std::string id1 = extension->id();
  EXPECT_TRUE(extension->HasAPIPermission(
      APIPermission::kUnlimitedStorage));
  const GURL origin1(
      extensions::AppLaunchInfo::GetFullLaunchURL(extension).GetOrigin());
  EXPECT_TRUE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageUnlimited(origin1));
  std::string origin_id = webkit_database::GetIdentifierFromOrigin(origin1);

  // Install app2 from the same origin with unlimited storage.
  extension = PackAndInstallCRX(data_dir_.AppendASCII("app2"), INSTALL_NEW);
  ValidatePrefKeyCount(++pref_count);
  ASSERT_EQ(2u, service_->extensions()->size());
  const std::string id2 = extension->id();
  EXPECT_TRUE(extension->HasAPIPermission(
      APIPermission::kUnlimitedStorage));
  EXPECT_TRUE(extension->web_extent().MatchesURL(
      extensions::AppLaunchInfo::GetFullLaunchURL(extension)));
  const GURL origin2(
      extensions::AppLaunchInfo::GetFullLaunchURL(extension).GetOrigin());
  EXPECT_EQ(origin1, origin2);
  EXPECT_TRUE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageUnlimited(origin2));

  // Set a cookie for the extension.
  net::CookieMonster* cookie_monster =
      profile_->GetRequestContext()->GetURLRequestContext()->
      cookie_store()->GetCookieMonster();
  ASSERT_TRUE(cookie_monster);
  net::CookieOptions options;
  cookie_monster->SetCookieWithOptionsAsync(
       origin1, "dummy=value", options,
       base::Bind(&ExtensionCookieCallback::SetCookieCallback,
                  base::Unretained(&callback)));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback.result_);

  cookie_monster->GetAllCookiesForURLAsync(
      origin1,
      base::Bind(&ExtensionCookieCallback::GetAllCookiesCallback,
                 base::Unretained(&callback)));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1U, callback.list_.size());

  // Open a database.
  webkit_database::DatabaseTracker* db_tracker =
      BrowserContext::GetDefaultStoragePartition(profile_.get())->
          GetDatabaseTracker();
  string16 db_name = UTF8ToUTF16("db");
  string16 description = UTF8ToUTF16("db_description");
  int64 size;
  db_tracker->DatabaseOpened(origin_id, db_name, description, 1, &size);
  db_tracker->DatabaseClosed(origin_id, db_name);
  std::vector<webkit_database::OriginInfo> origins;
  db_tracker->GetAllOriginsInfo(&origins);
  EXPECT_EQ(1U, origins.size());
  EXPECT_EQ(origin_id, origins[0].GetOriginIdentifier());

  // Create local storage. We only simulate this by creating the backing files.
  // Note: This test depends on details of how the dom_storage library
  // stores data in the host file system.
  base::FilePath lso_dir_path =
      profile_->GetPath().AppendASCII("Local Storage");
  base::FilePath lso_file_path = lso_dir_path.AppendASCII(origin_id)
      .AddExtension(FILE_PATH_LITERAL(".localstorage"));
  EXPECT_TRUE(file_util::CreateDirectory(lso_dir_path));
  EXPECT_EQ(0, file_util::WriteFile(lso_file_path, NULL, 0));
  EXPECT_TRUE(base::PathExists(lso_file_path));

  // Create indexed db. Similarly, it is enough to only simulate this by
  // creating the directory on the disk.
  IndexedDBContext* idb_context =
      BrowserContext::GetDefaultStoragePartition(profile_.get())->
          GetIndexedDBContext();
  idb_context->SetTaskRunnerForTesting(
      base::MessageLoop::current()->message_loop_proxy().get());
  base::FilePath idb_path = idb_context->GetFilePathForTesting(origin_id);
  EXPECT_TRUE(file_util::CreateDirectory(idb_path));
  EXPECT_TRUE(base::DirectoryExists(idb_path));

  // Uninstall one of them, unlimited storage should still be granted
  // to the origin.
  UninstallExtension(id1, false);
  EXPECT_EQ(1u, service_->extensions()->size());
  EXPECT_TRUE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageUnlimited(origin1));

  // Check that the cookie is still there.
  cookie_monster->GetAllCookiesForURLAsync(
       origin1,
       base::Bind(&ExtensionCookieCallback::GetAllCookiesCallback,
                  base::Unretained(&callback)));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1U, callback.list_.size());

  // Now uninstall the other. Storage should be cleared for the apps.
  UninstallExtension(id2, false);
  EXPECT_EQ(0u, service_->extensions()->size());
  EXPECT_FALSE(profile_->GetExtensionSpecialStoragePolicy()->
      IsStorageUnlimited(origin1));

  // Check that the cookie is gone.
  cookie_monster->GetAllCookiesForURLAsync(
       origin1,
       base::Bind(&ExtensionCookieCallback::GetAllCookiesCallback,
                  base::Unretained(&callback)));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0U, callback.list_.size());

  // The database should have vanished as well.
  origins.clear();
  db_tracker->GetAllOriginsInfo(&origins);
  EXPECT_EQ(0U, origins.size());

  // Check that the LSO file has been removed.
  EXPECT_FALSE(base::PathExists(lso_file_path));

  // Check if the indexed db has disappeared too.
  EXPECT_FALSE(base::DirectoryExists(idb_path));
}

// Tests loading single extensions (like --load-extension)
// Flaky crashes. http://crbug.com/231806
TEST_F(ExtensionServiceTest, DISABLED_LoadExtension) {
  InitializeEmptyExtensionService();

  base::FilePath ext1 = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj")
      .AppendASCII("1.0.0.0");
  extensions::UnpackedInstaller::Create(service_)->Load(ext1);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  EXPECT_EQ(Manifest::UNPACKED, loaded_[0]->location());
  EXPECT_EQ(1u, service_->extensions()->size());

  ValidatePrefKeyCount(1);

  base::FilePath no_manifest = data_dir_
      .AppendASCII("bad")
      // .AppendASCII("Extensions")
      .AppendASCII("cccccccccccccccccccccccccccccccc")
      .AppendASCII("1");
  extensions::UnpackedInstaller::Create(service_)->Load(no_manifest);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  EXPECT_EQ(1u, service_->extensions()->size());

  // Test uninstall.
  std::string id = loaded_[0]->id();
  EXPECT_FALSE(unloaded_id_.length());
  service_->UninstallExtension(id, false, NULL);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(id, unloaded_id_);
  ASSERT_EQ(0u, loaded_.size());
  EXPECT_EQ(0u, service_->extensions()->size());
}

// Tests that we generate IDs when they are not specified in the manifest for
// --load-extension.
TEST_F(ExtensionServiceTest, GenerateID) {
  InitializeEmptyExtensionService();

  base::FilePath no_id_ext = data_dir_.AppendASCII("no_id");
  extensions::UnpackedInstaller::Create(service_)->Load(no_id_ext);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  ASSERT_TRUE(Extension::IdIsValid(loaded_[0]->id()));
  EXPECT_EQ(loaded_[0]->location(), Manifest::UNPACKED);

  ValidatePrefKeyCount(1);

  std::string previous_id = loaded_[0]->id();

  // If we reload the same path, we should get the same extension ID.
  extensions::UnpackedInstaller::Create(service_)->Load(no_id_ext);
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(1u, loaded_.size());
  ASSERT_EQ(previous_id, loaded_[0]->id());
}

void ExtensionServiceTest::TestExternalProvider(
    MockExtensionProvider* provider, Manifest::Location location) {
  // Verify that starting with no providers loads no extensions.
  service_->Init();
  ASSERT_EQ(0u, loaded_.size());

  provider->set_visit_count(0);

  // Register a test extension externally using the mock registry provider.
  base::FilePath source_path = data_dir_.AppendASCII("good.crx");

  // Add the extension.
  provider->UpdateOrAddExtension(good_crx, "1.0.0.0", source_path);

  // Reloading extensions should find our externally registered extension
  // and install it.
  service_->CheckForExternalUpdates();
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();

  ASSERT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  ASSERT_EQ(location, loaded_[0]->location());
  ASSERT_EQ("1.0.0.0", loaded_[0]->version()->GetString());
  ValidatePrefKeyCount(1);
  ValidateIntegerPref(good_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(good_crx, "location", location);

  // Reload extensions without changing anything. The extension should be
  // loaded again.
  loaded_.clear();
  service_->ReloadExtensions();
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  ValidatePrefKeyCount(1);
  ValidateIntegerPref(good_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(good_crx, "location", location);

  // Now update the extension with a new version. We should get upgraded.
  source_path = source_path.DirName().AppendASCII("good2.crx");
  provider->UpdateOrAddExtension(good_crx, "1.0.0.1", source_path);

  loaded_.clear();
  service_->CheckForExternalUpdates();
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();
  ASSERT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  ASSERT_EQ("1.0.0.1", loaded_[0]->version()->GetString());
  ValidatePrefKeyCount(1);
  ValidateIntegerPref(good_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(good_crx, "location", location);

  // Uninstall the extension and reload. Nothing should happen because the
  // preference should prevent us from reinstalling.
  std::string id = loaded_[0]->id();
  bool no_uninstall =
      management_policy_->MustRemainEnabled(loaded_[0].get(), NULL);
  service_->UninstallExtension(id, false, NULL);
  base::RunLoop().RunUntilIdle();

  base::FilePath install_path = extensions_install_dir_.AppendASCII(id);
  if (no_uninstall) {
    // Policy controlled extensions should not have been touched by uninstall.
    ASSERT_TRUE(base::PathExists(install_path));
  } else {
    // The extension should also be gone from the install directory.
    ASSERT_FALSE(base::PathExists(install_path));
    loaded_.clear();
    service_->CheckForExternalUpdates();
    base::RunLoop().RunUntilIdle();
    ASSERT_EQ(0u, loaded_.size());
    ValidatePrefKeyCount(1);
    ValidateIntegerPref(good_crx, "state",
                        Extension::EXTERNAL_EXTENSION_UNINSTALLED);
    ValidateIntegerPref(good_crx, "location", location);

    // Now clear the preference and reinstall.
    SetPrefInteg(good_crx, "state", Extension::ENABLED);

    loaded_.clear();
    service_->CheckForExternalUpdates();
    content::WindowedNotificationObserver(
        chrome::NOTIFICATION_CRX_INSTALLER_DONE,
        content::NotificationService::AllSources()).Wait();
    ASSERT_EQ(1u, loaded_.size());
  }
  ValidatePrefKeyCount(1);
  ValidateIntegerPref(good_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(good_crx, "location", location);

  if (management_policy_->MustRemainEnabled(loaded_[0].get(), NULL)) {
    EXPECT_EQ(2, provider->visit_count());
  } else {
    // Now test an externally triggered uninstall (deleting the registry key or
    // the pref entry).
    provider->RemoveExtension(good_crx);

    loaded_.clear();
    service_->OnExternalProviderReady(provider);
    base::RunLoop().RunUntilIdle();
    ASSERT_EQ(0u, loaded_.size());
    ValidatePrefKeyCount(0);

    // The extension should also be gone from the install directory.
    ASSERT_FALSE(base::PathExists(install_path));

    // Now test the case where user uninstalls and then the extension is removed
    // from the external provider.
    provider->UpdateOrAddExtension(good_crx, "1.0.0.1", source_path);
    service_->CheckForExternalUpdates();
    content::WindowedNotificationObserver(
        chrome::NOTIFICATION_CRX_INSTALLER_DONE,
        content::NotificationService::AllSources()).Wait();

    ASSERT_EQ(1u, loaded_.size());
    ASSERT_EQ(0u, GetErrors().size());

    // User uninstalls.
    loaded_.clear();
    service_->UninstallExtension(id, false, NULL);
    base::RunLoop().RunUntilIdle();
    ASSERT_EQ(0u, loaded_.size());

    // Then remove the extension from the extension provider.
    provider->RemoveExtension(good_crx);

    // Should still be at 0.
    loaded_.clear();
    extensions::InstalledLoader(service_).LoadAllExtensions();
    base::RunLoop().RunUntilIdle();
    ASSERT_EQ(0u, loaded_.size());
    ValidatePrefKeyCount(1);

    EXPECT_EQ(5, provider->visit_count());
  }
}

// Tests the external installation feature
#if defined(OS_WIN)
TEST_F(ExtensionServiceTest, ExternalInstallRegistry) {
  // This should all work, even when normal extension installation is disabled.
  InitializeEmptyExtensionService();
  set_extensions_enabled(false);

  // Now add providers. Extension system takes ownership of the objects.
  MockExtensionProvider* reg_provider =
      new MockExtensionProvider(service_, Manifest::EXTERNAL_REGISTRY);
  AddMockExternalProvider(reg_provider);
  TestExternalProvider(reg_provider, Manifest::EXTERNAL_REGISTRY);
}
#endif

TEST_F(ExtensionServiceTest, ExternalInstallPref) {
  InitializeEmptyExtensionService();

  // Now add providers. Extension system takes ownership of the objects.
  MockExtensionProvider* pref_provider =
      new MockExtensionProvider(service_, Manifest::EXTERNAL_PREF);

  AddMockExternalProvider(pref_provider);
  TestExternalProvider(pref_provider, Manifest::EXTERNAL_PREF);
}

TEST_F(ExtensionServiceTest, ExternalInstallPrefUpdateUrl) {
  // This should all work, even when normal extension installation is disabled.
  InitializeEmptyExtensionService();
  set_extensions_enabled(false);

  // TODO(skerner): The mock provider is not a good model of a provider
  // that works with update URLs, because it adds file and version info.
  // Extend the mock to work with update URLs.  This test checks the
  // behavior that is common to all external extension visitors.  The
  // browser test ExtensionManagementTest.ExternalUrlUpdate tests that
  // what the visitor does results in an extension being downloaded and
  // installed.
  MockExtensionProvider* pref_provider =
      new MockExtensionProvider(service_,
                                Manifest::EXTERNAL_PREF_DOWNLOAD);
  AddMockExternalProvider(pref_provider);
  TestExternalProvider(pref_provider, Manifest::EXTERNAL_PREF_DOWNLOAD);
}

TEST_F(ExtensionServiceTest, ExternalInstallPolicyUpdateUrl) {
  // This should all work, even when normal extension installation is disabled.
  InitializeEmptyExtensionService();
  set_extensions_enabled(false);

  // TODO(skerner): The mock provider is not a good model of a provider
  // that works with update URLs, because it adds file and version info.
  // Extend the mock to work with update URLs. This test checks the
  // behavior that is common to all external extension visitors. The
  // browser test ExtensionManagementTest.ExternalUrlUpdate tests that
  // what the visitor does results in an extension being downloaded and
  // installed.
  MockExtensionProvider* pref_provider =
      new MockExtensionProvider(service_,
                                Manifest::EXTERNAL_POLICY_DOWNLOAD);
  AddMockExternalProvider(pref_provider);
  TestExternalProvider(pref_provider, Manifest::EXTERNAL_POLICY_DOWNLOAD);
}

// Tests that external extensions get uninstalled when the external extension
// providers can't account for them.
TEST_F(ExtensionServiceTest, ExternalUninstall) {
  // Start the extensions service with one external extension already installed.
  base::FilePath source_install_dir = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions");
  base::FilePath pref_path = source_install_dir
      .DirName()
      .AppendASCII("PreferencesExternal");

  // This initializes the extensions service with no ExternalProviders.
  InitializeInstalledExtensionService(pref_path, source_install_dir);
  set_extensions_enabled(false);

  service_->Init();

  ASSERT_EQ(0u, GetErrors().size());
  ASSERT_EQ(0u, loaded_.size());

  // Verify that it's not the disabled extensions flag causing it not to load.
  set_extensions_enabled(true);
  service_->ReloadExtensions();
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(0u, GetErrors().size());
  ASSERT_EQ(0u, loaded_.size());
}

// Test that running multiple update checks simultaneously does not
// keep the update from succeeding.
TEST_F(ExtensionServiceTest, MultipleExternalUpdateCheck) {
  InitializeEmptyExtensionService();

  MockExtensionProvider* provider =
      new MockExtensionProvider(service_, Manifest::EXTERNAL_PREF);
  AddMockExternalProvider(provider);

  // Verify that starting with no providers loads no extensions.
  service_->Init();
  ASSERT_EQ(0u, loaded_.size());

  // Start two checks for updates.
  provider->set_visit_count(0);
  service_->CheckForExternalUpdates();
  service_->CheckForExternalUpdates();
  base::RunLoop().RunUntilIdle();

  // Two calls should cause two checks for external extensions.
  EXPECT_EQ(2, provider->visit_count());
  EXPECT_EQ(0u, GetErrors().size());
  EXPECT_EQ(0u, loaded_.size());

  // Register a test extension externally using the mock registry provider.
  base::FilePath source_path = data_dir_.AppendASCII("good.crx");
  provider->UpdateOrAddExtension(good_crx, "1.0.0.0", source_path);

  // Two checks for external updates should find the extension, and install it
  // once.
  provider->set_visit_count(0);
  service_->CheckForExternalUpdates();
  service_->CheckForExternalUpdates();
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();
  EXPECT_EQ(2, provider->visit_count());
  ASSERT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  ASSERT_EQ(Manifest::EXTERNAL_PREF, loaded_[0]->location());
  ASSERT_EQ("1.0.0.0", loaded_[0]->version()->GetString());
  ValidatePrefKeyCount(1);
  ValidateIntegerPref(good_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(good_crx, "location", Manifest::EXTERNAL_PREF);

  provider->RemoveExtension(good_crx);
  provider->set_visit_count(0);
  service_->CheckForExternalUpdates();
  service_->CheckForExternalUpdates();
  base::RunLoop().RunUntilIdle();

  // Two calls should cause two checks for external extensions.
  // Because the external source no longer includes good_crx,
  // good_crx will be uninstalled.  So, expect that no extensions
  // are loaded.
  EXPECT_EQ(2, provider->visit_count());
  EXPECT_EQ(0u, GetErrors().size());
  EXPECT_EQ(0u, loaded_.size());
}

namespace {
  class ScopedBrowserLocale {
   public:
    explicit ScopedBrowserLocale(const std::string& new_locale)
      : old_locale_(g_browser_process->GetApplicationLocale()) {
      g_browser_process->SetApplicationLocale(new_locale);
    }

    ~ScopedBrowserLocale() {
      g_browser_process->SetApplicationLocale(old_locale_);
    }

   private:
    std::string old_locale_;
  };
}  // namespace

TEST_F(ExtensionServiceTest, ExternalPrefProvider) {
  InitializeEmptyExtensionService();

  // Test some valid extension records.
  // Set a base path to avoid erroring out on relative paths.
  // Paths starting with // are absolute on every platform we support.
  base::FilePath base_path(FILE_PATH_LITERAL("//base/path"));
  ASSERT_TRUE(base_path.IsAbsolute());
  MockProviderVisitor visitor(base_path);
  std::string json_data =
      "{"
      "  \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": {"
      "    \"external_crx\": \"RandomExtension.crx\","
      "    \"external_version\": \"1.0\""
      "  },"
      "  \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\": {"
      "    \"external_crx\": \"RandomExtension2.crx\","
      "    \"external_version\": \"2.0\""
      "  },"
      "  \"cccccccccccccccccccccccccccccccc\": {"
      "    \"external_update_url\": \"http:\\\\foo.com/update\""
      "  }"
      "}";
  EXPECT_EQ(3, visitor.Visit(json_data));

  // Simulate an external_extensions.json file that contains seven invalid
  // records:
  // - One that is missing the 'external_crx' key.
  // - One that is missing the 'external_version' key.
  // - One that is specifying .. in the path.
  // - One that specifies both a file and update URL.
  // - One that specifies no file or update URL.
  // - One that has an update URL that is not well formed.
  // - One that contains a malformed version.
  // - One that has an invalid id.
  // - One that has a non-dictionary value.
  // - One that has an integer 'external_version' instead of a string.
  // The final extension is valid, and we check that it is read to make sure
  // failures don't stop valid records from being read.
  json_data =
      "{"
      "  \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": {"
      "    \"external_version\": \"1.0\""
      "  },"
      "  \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\": {"
      "    \"external_crx\": \"RandomExtension.crx\""
      "  },"
      "  \"cccccccccccccccccccccccccccccccc\": {"
      "    \"external_crx\": \"..\\\\foo\\\\RandomExtension2.crx\","
      "    \"external_version\": \"2.0\""
      "  },"
      "  \"dddddddddddddddddddddddddddddddd\": {"
      "    \"external_crx\": \"RandomExtension2.crx\","
      "    \"external_version\": \"2.0\","
      "    \"external_update_url\": \"http:\\\\foo.com/update\""
      "  },"
      "  \"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\": {"
      "  },"
      "  \"ffffffffffffffffffffffffffffffff\": {"
      "    \"external_update_url\": \"This string is not a valid URL\""
      "  },"
      "  \"gggggggggggggggggggggggggggggggg\": {"
      "    \"external_crx\": \"RandomExtension3.crx\","
      "    \"external_version\": \"This is not a valid version!\""
      "  },"
      "  \"This is not a valid id!\": {},"
      "  \"hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh\": true,"
      "  \"iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii\": {"
      "    \"external_crx\": \"RandomExtension4.crx\","
      "    \"external_version\": 1.0"
      "  },"
      "  \"pppppppppppppppppppppppppppppppp\": {"
      "    \"external_crx\": \"RandomValidExtension.crx\","
      "    \"external_version\": \"1.0\""
      "  }"
      "}";
  EXPECT_EQ(1, visitor.Visit(json_data));

  // Check that if a base path is not provided, use of a relative
  // path fails.
  base::FilePath empty;
  MockProviderVisitor visitor_no_relative_paths(empty);

  // Use absolute paths.  Expect success.
  json_data =
      "{"
      "  \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": {"
      "    \"external_crx\": \"//RandomExtension1.crx\","
      "    \"external_version\": \"3.0\""
      "  },"
      "  \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\": {"
      "    \"external_crx\": \"//path/to/RandomExtension2.crx\","
      "    \"external_version\": \"3.0\""
      "  }"
      "}";
  EXPECT_EQ(2, visitor_no_relative_paths.Visit(json_data));

  // Use a relative path.  Expect that it will error out.
  json_data =
      "{"
      "  \"cccccccccccccccccccccccccccccccc\": {"
      "    \"external_crx\": \"RandomExtension2.crx\","
      "    \"external_version\": \"3.0\""
      "  }"
      "}";
  EXPECT_EQ(0, visitor_no_relative_paths.Visit(json_data));

  // Test supported_locales.
  json_data =
      "{"
      "  \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": {"
      "    \"external_crx\": \"RandomExtension.crx\","
      "    \"external_version\": \"1.0\","
      "    \"supported_locales\": [ \"en\" ]"
      "  },"
      "  \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\": {"
      "    \"external_crx\": \"RandomExtension2.crx\","
      "    \"external_version\": \"2.0\","
      "    \"supported_locales\": [ \"en-GB\" ]"
      "  },"
      "  \"cccccccccccccccccccccccccccccccc\": {"
      "    \"external_crx\": \"RandomExtension2.crx\","
      "    \"external_version\": \"3.0\","
      "    \"supported_locales\": [ \"en_US\", \"fr\" ]"
      "  }"
      "}";
  {
    ScopedBrowserLocale guard("en-US");
    EXPECT_EQ(2, visitor.Visit(json_data));
  }

  // Test keep_if_present.
  json_data =
      "{"
      "  \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": {"
      "    \"external_crx\": \"RandomExtension.crx\","
      "    \"external_version\": \"1.0\","
      "    \"keep_if_present\": true"
      "  }"
      "}";
  {
    EXPECT_EQ(0, visitor.Visit(json_data));
  }

  // Test is_bookmark_app.
  MockProviderVisitor from_bookmark_visitor(
      base_path, Extension::FROM_BOOKMARK);
  json_data =
      "{"
      "  \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": {"
      "    \"external_crx\": \"RandomExtension.crx\","
      "    \"external_version\": \"1.0\","
      "    \"is_bookmark_app\": true"
      "  }"
      "}";
  EXPECT_EQ(1, from_bookmark_visitor.Visit(json_data));

  // Test is_from_webstore.
  MockProviderVisitor from_webstore_visitor(
      base_path, Extension::FROM_WEBSTORE);
  json_data =
      "{"
      "  \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": {"
      "    \"external_crx\": \"RandomExtension.crx\","
      "    \"external_version\": \"1.0\","
      "    \"is_from_webstore\": true"
      "  }"
      "}";
  EXPECT_EQ(1, from_webstore_visitor.Visit(json_data));
}

// Test loading good extensions from the profile directory.
TEST_F(ExtensionServiceTest, LoadAndRelocalizeExtensions) {
  // Ensure we're testing in "en" and leave global state untouched.
  extension_l10n_util::ScopedLocaleForTest testLocale("en");

  // Initialize the test dir with a good Preferences/extensions.
  base::FilePath source_install_dir = data_dir_
      .AppendASCII("l10n");
  base::FilePath pref_path = source_install_dir.AppendASCII("Preferences");
  InitializeInstalledExtensionService(pref_path, source_install_dir);

  service_->Init();

  ASSERT_EQ(3u, loaded_.size());

  // This was equal to "sr" on load.
  ValidateStringPref(loaded_[0]->id(), keys::kCurrentLocale, "en");

  // These are untouched by re-localization.
  ValidateStringPref(loaded_[1]->id(), keys::kCurrentLocale, "en");
  EXPECT_FALSE(IsPrefExist(loaded_[1]->id(), keys::kCurrentLocale));

  // This one starts with Serbian name, and gets re-localized into English.
  EXPECT_EQ("My name is simple.", loaded_[0]->name());

  // These are untouched by re-localization.
  EXPECT_EQ("My name is simple.", loaded_[1]->name());
  EXPECT_EQ("no l10n", loaded_[2]->name());
}

class ExtensionsReadyRecorder : public content::NotificationObserver {
 public:
  ExtensionsReadyRecorder() : ready_(false) {
    registrar_.Add(this, chrome::NOTIFICATION_EXTENSIONS_READY,
                   content::NotificationService::AllSources());
  }

  void set_ready(bool value) { ready_ = value; }
  bool ready() { return ready_; }

 private:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    switch (type) {
      case chrome::NOTIFICATION_EXTENSIONS_READY:
        ready_ = true;
        break;
      default:
        NOTREACHED();
    }
  }

  content::NotificationRegistrar registrar_;
  bool ready_;
};

// Test that we get enabled/disabled correctly for all the pref/command-line
// combinations. We don't want to derive from the ExtensionServiceTest class
// for this test, so we use ExtensionServiceTestSimple.
//
// Also tests that we always fire EXTENSIONS_READY, no matter whether we are
// enabled or not.
TEST(ExtensionServiceTestSimple, Enabledness) {
  // Make sure the PluginService singleton is destroyed at the end of the test.
  base::ShadowingAtExitManager at_exit_manager;
#if defined(ENABLE_PLUGINS)
  content::PluginService::GetInstance()->Init();
  content::PluginService::GetInstance()->DisablePluginsDiscoveryForTesting();
#endif

  ExtensionErrorReporter::Init(false);  // no noisy errors
  ExtensionsReadyRecorder recorder;
  scoped_ptr<TestingProfile> profile(new TestingProfile());
  content::TestBrowserThreadBundle thread_bundle_;
#if defined OS_CHROMEOS
  chromeos::ScopedTestDeviceSettingsService device_settings_service;
  chromeos::ScopedTestCrosSettings cros_settings;
  scoped_ptr<chromeos::ScopedTestUserManager> user_manager(
      new chromeos::ScopedTestUserManager);
#endif
  scoped_ptr<CommandLine> command_line;
  base::FilePath install_dir = profile->GetPath()
      .AppendASCII(extensions::kInstallDirectoryName);

  // By default, we are enabled.
  command_line.reset(new CommandLine(CommandLine::NO_PROGRAM));
  ExtensionService* service = static_cast<extensions::TestExtensionSystem*>(
      ExtensionSystem::Get(profile.get()))->
      CreateExtensionService(
          command_line.get(),
          install_dir,
          false);
  EXPECT_TRUE(service->extensions_enabled());
  service->Init();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(recorder.ready());
#if defined OS_CHROMEOS
  user_manager.reset();
#endif

  // If either the command line or pref is set, we are disabled.
  recorder.set_ready(false);
  profile.reset(new TestingProfile());
  command_line->AppendSwitch(switches::kDisableExtensions);
  service = static_cast<extensions::TestExtensionSystem*>(
      ExtensionSystem::Get(profile.get()))->
      CreateExtensionService(
          command_line.get(),
          install_dir,
          false);
  EXPECT_FALSE(service->extensions_enabled());
  service->Init();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(recorder.ready());

  recorder.set_ready(false);
  profile.reset(new TestingProfile());
  profile->GetPrefs()->SetBoolean(prefs::kDisableExtensions, true);
  service = static_cast<extensions::TestExtensionSystem*>(
      ExtensionSystem::Get(profile.get()))->
      CreateExtensionService(
          command_line.get(),
          install_dir,
          false);
  EXPECT_FALSE(service->extensions_enabled());
  service->Init();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(recorder.ready());

  recorder.set_ready(false);
  profile.reset(new TestingProfile());
  profile->GetPrefs()->SetBoolean(prefs::kDisableExtensions, true);
  command_line.reset(new CommandLine(CommandLine::NO_PROGRAM));
  service = static_cast<extensions::TestExtensionSystem*>(
      ExtensionSystem::Get(profile.get()))->
      CreateExtensionService(
          command_line.get(),
          install_dir,
          false);
  EXPECT_FALSE(service->extensions_enabled());
  service->Init();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(recorder.ready());

  // Explicitly delete all the resources used in this test.
  profile.reset();
  service = NULL;
  // Execute any pending deletion tasks.
  base::RunLoop().RunUntilIdle();
}

// Test loading extensions that require limited and unlimited storage quotas.
TEST_F(ExtensionServiceTest, StorageQuota) {
  InitializeEmptyExtensionService();

  base::FilePath extensions_path = data_dir_
      .AppendASCII("storage_quota");

  base::FilePath limited_quota_ext =
      extensions_path.AppendASCII("limited_quota")
      .AppendASCII("1.0");

  // The old permission name for unlimited quota was "unlimited_storage", but
  // we changed it to "unlimitedStorage". This tests both versions.
  base::FilePath unlimited_quota_ext =
      extensions_path.AppendASCII("unlimited_quota")
      .AppendASCII("1.0");
  base::FilePath unlimited_quota_ext2 =
      extensions_path.AppendASCII("unlimited_quota")
      .AppendASCII("2.0");
  extensions::UnpackedInstaller::Create(service_)->Load(limited_quota_ext);
  extensions::UnpackedInstaller::Create(service_)->Load(unlimited_quota_ext);
  extensions::UnpackedInstaller::Create(service_)->Load(unlimited_quota_ext2);
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(3u, loaded_.size());
  EXPECT_TRUE(profile_.get());
  EXPECT_FALSE(profile_->IsOffTheRecord());
  EXPECT_FALSE(profile_->GetExtensionSpecialStoragePolicy()->IsStorageUnlimited(
      loaded_[0]->url()));
  EXPECT_TRUE(profile_->GetExtensionSpecialStoragePolicy()->IsStorageUnlimited(
      loaded_[1]->url()));
  EXPECT_TRUE(profile_->GetExtensionSpecialStoragePolicy()->IsStorageUnlimited(
      loaded_[2]->url()));
}

// Tests ComponentLoader::Add().
TEST_F(ExtensionServiceTest, ComponentExtensions) {
  InitializeEmptyExtensionService();

  // Component extensions should work even when extensions are disabled.
  set_extensions_enabled(false);

  base::FilePath path = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII("behllobkkfkfnphdnhnkndlbkcpglgmj")
      .AppendASCII("1.0.0.0");

  std::string manifest;
  ASSERT_TRUE(file_util::ReadFileToString(
      path.Append(extensions::kManifestFilename), &manifest));

  service_->component_loader()->Add(manifest, path);
  service_->Init();

  // Note that we do not pump messages -- the extension should be loaded
  // immediately.

  EXPECT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, loaded_.size());
  EXPECT_EQ(Manifest::COMPONENT, loaded_[0]->location());
  EXPECT_EQ(1u, service_->extensions()->size());

  // Component extensions get a prefs entry on first install.
  ValidatePrefKeyCount(1);

  // Reload all extensions, and make sure it comes back.
  std::string extension_id = (*service_->extensions()->begin())->id();
  loaded_.clear();
  service_->ReloadExtensions();
  ASSERT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(extension_id, (*service_->extensions()->begin())->id());
}

namespace {
  class TestSyncProcessorStub : public syncer::SyncChangeProcessor {
    virtual syncer::SyncError ProcessSyncChanges(
        const tracked_objects::Location& from_here,
        const syncer::SyncChangeList& change_list) OVERRIDE {
      return syncer::SyncError();
    }
  };
}

TEST_F(ExtensionServiceTest, DeferredSyncStartupPreInstalledComponent) {
  InitializeEmptyExtensionService();

  bool flare_was_called = false;
  syncer::ModelType triggered_type(syncer::UNSPECIFIED);
  base::WeakPtrFactory<ExtensionServiceTest> factory(this);
  service_->SetSyncStartFlare(
      base::Bind(&ExtensionServiceTest::MockSyncStartFlare,
                 factory.GetWeakPtr(),
                 &flare_was_called,  // Safe due to WeakPtrFactory scope.
                 &triggered_type));  // Safe due to WeakPtrFactory scope.

  // Install a component extension.
  base::FilePath path = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions")
      .AppendASCII(good0)
      .AppendASCII("1.0.0.0");
  std::string manifest;
  ASSERT_TRUE(file_util::ReadFileToString(
      path.Append(extensions::kManifestFilename), &manifest));
  service_->component_loader()->Add(manifest, path);
  ASSERT_FALSE(service_->is_ready());
  service_->Init();
  ASSERT_TRUE(service_->is_ready());

  // Extensions added before service is_ready() don't trigger sync startup.
  EXPECT_FALSE(flare_was_called);
  ASSERT_EQ(syncer::UNSPECIFIED, triggered_type);
}

TEST_F(ExtensionServiceTest, DeferredSyncStartupPreInstalledNormal) {
  // Initialize the test dir with a good Preferences/extensions.
  base::FilePath source_install_dir = data_dir_
      .AppendASCII("good")
      .AppendASCII("Extensions");
  base::FilePath pref_path = source_install_dir
      .DirName()
      .AppendASCII("Preferences");
  InitializeInstalledExtensionService(pref_path, source_install_dir);

  bool flare_was_called = false;
  syncer::ModelType triggered_type(syncer::UNSPECIFIED);
  base::WeakPtrFactory<ExtensionServiceTest> factory(this);
  service_->SetSyncStartFlare(
      base::Bind(&ExtensionServiceTest::MockSyncStartFlare,
                 factory.GetWeakPtr(),
                 &flare_was_called,  // Safe due to WeakPtrFactory scope.
                 &triggered_type));  // Safe due to WeakPtrFactory scope.

  ASSERT_FALSE(service_->is_ready());
  service_->Init();
  ASSERT_TRUE(service_->is_ready());

  // Extensions added before service is_ready() don't trigger sync startup.
  EXPECT_FALSE(flare_was_called);
  ASSERT_EQ(syncer::UNSPECIFIED, triggered_type);
}

TEST_F(ExtensionServiceTest, DeferredSyncStartupOnInstall) {
  InitializeEmptyExtensionService();
  service_->Init();
  ASSERT_TRUE(service_->is_ready());

  bool flare_was_called = false;
  syncer::ModelType triggered_type(syncer::UNSPECIFIED);
  base::WeakPtrFactory<ExtensionServiceTest> factory(this);
  service_->SetSyncStartFlare(
      base::Bind(&ExtensionServiceTest::MockSyncStartFlare,
                 factory.GetWeakPtr(),
                 &flare_was_called,  // Safe due to WeakPtrFactory scope.
                 &triggered_type));  // Safe due to WeakPtrFactory scope.

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_NEW);

  EXPECT_TRUE(flare_was_called);
  EXPECT_EQ(syncer::EXTENSIONS, triggered_type);

  // Reset.
  flare_was_called = false;
  triggered_type = syncer::UNSPECIFIED;

  // Once sync starts, flare should no longer be invoked.
  service_->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));
  path = data_dir_.AppendASCII("page_action.crx");
  InstallCRX(path, INSTALL_NEW);
  EXPECT_FALSE(flare_was_called);
  ASSERT_EQ(syncer::UNSPECIFIED, triggered_type);
}

TEST_F(ExtensionServiceTest, GetSyncData) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  const Extension* extension = service_->GetInstalledExtension(good_crx);
  ASSERT_TRUE(extension);

  service_->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  syncer::SyncDataList list = service_->GetAllSyncData(syncer::EXTENSIONS);
  ASSERT_EQ(list.size(), 1U);
  extensions::ExtensionSyncData data(list[0]);
  EXPECT_EQ(extension->id(), data.id());
  EXPECT_FALSE(data.uninstalled());
  EXPECT_EQ(service_->IsExtensionEnabled(good_crx), data.enabled());
  EXPECT_EQ(service_->IsIncognitoEnabled(good_crx), data.incognito_enabled());
  EXPECT_TRUE(data.version().Equals(*extension->version()));
  EXPECT_EQ(extensions::ManifestURL::GetUpdateURL(extension),
            data.update_url());
  EXPECT_EQ(extension->name(), data.name());
}

TEST_F(ExtensionServiceTest, GetSyncDataTerminated) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  TerminateExtension(good_crx);
  const Extension* extension = service_->GetInstalledExtension(good_crx);
  ASSERT_TRUE(extension);

  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  syncer::SyncDataList list = service_->GetAllSyncData(syncer::EXTENSIONS);
  ASSERT_EQ(list.size(), 1U);
  extensions::ExtensionSyncData data(list[0]);
  EXPECT_EQ(extension->id(), data.id());
  EXPECT_FALSE(data.uninstalled());
  EXPECT_EQ(service_->IsExtensionEnabled(good_crx), data.enabled());
  EXPECT_EQ(service_->IsIncognitoEnabled(good_crx), data.incognito_enabled());
  EXPECT_TRUE(data.version().Equals(*extension->version()));
  EXPECT_EQ(extensions::ManifestURL::GetUpdateURL(extension),
            data.update_url());
  EXPECT_EQ(extension->name(), data.name());
}

TEST_F(ExtensionServiceTest, GetSyncDataFilter) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  const Extension* extension = service_->GetInstalledExtension(good_crx);
  ASSERT_TRUE(extension);

  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(syncer::APPS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  syncer::SyncDataList list = service_->GetAllSyncData(syncer::EXTENSIONS);
  ASSERT_EQ(list.size(), 0U);
}

TEST_F(ExtensionServiceTest, GetSyncExtensionDataUserSettings) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  const Extension* extension = service_->GetInstalledExtension(good_crx);
  ASSERT_TRUE(extension);

  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  {
    syncer::SyncDataList list = service_->GetAllSyncData(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    extensions::ExtensionSyncData data(list[0]);
    EXPECT_TRUE(data.enabled());
    EXPECT_FALSE(data.incognito_enabled());
  }

  service_->DisableExtension(good_crx, Extension::DISABLE_USER_ACTION);
  {
    syncer::SyncDataList list = service_->GetAllSyncData(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    extensions::ExtensionSyncData data(list[0]);
    EXPECT_FALSE(data.enabled());
    EXPECT_FALSE(data.incognito_enabled());
  }

  service_->SetIsIncognitoEnabled(good_crx, true);
  {
    syncer::SyncDataList list = service_->GetAllSyncData(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    extensions::ExtensionSyncData data(list[0]);
    EXPECT_FALSE(data.enabled());
    EXPECT_TRUE(data.incognito_enabled());
  }

  service_->EnableExtension(good_crx);
  {
    syncer::SyncDataList list = service_->GetAllSyncData(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    extensions::ExtensionSyncData data(list[0]);
    EXPECT_TRUE(data.enabled());
    EXPECT_TRUE(data.incognito_enabled());
  }
}

TEST_F(ExtensionServiceTest, SyncForUninstalledExternalExtension) {
  InitializeEmptyExtensionService();
  InstallCRXWithLocation(data_dir_.AppendASCII("good.crx"),
                         Manifest::EXTERNAL_PREF, INSTALL_NEW);
  const Extension* extension = service_->GetInstalledExtension(good_crx);
  ASSERT_TRUE(extension);

  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  UninstallExtension(good_crx, false);
  EXPECT_TRUE(service_->IsExternalExtensionUninstalled(good_crx));

  sync_pb::EntitySpecifics specifics;
  sync_pb::AppSpecifics* app_specifics = specifics.mutable_app();
  sync_pb::ExtensionSpecifics* extension_specifics =
      app_specifics->mutable_extension();
  extension_specifics->set_id(good_crx);
  extension_specifics->set_version("1.0");
  extension_specifics->set_enabled(true);

  syncer::SyncData sync_data =
      syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
  syncer::SyncChange sync_change(FROM_HERE,
                                 syncer::SyncChange::ACTION_UPDATE,
                                 sync_data);
  syncer::SyncChangeList list(1);
  list[0] = sync_change;

  service_->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_TRUE(service_->IsExternalExtensionUninstalled(good_crx));
}

TEST_F(ExtensionServiceTest, GetSyncAppDataUserSettings) {
  InitializeEmptyExtensionService();
  const Extension* app =
      PackAndInstallCRX(data_dir_.AppendASCII("app"), INSTALL_NEW);
  ASSERT_TRUE(app);
  ASSERT_TRUE(app->is_app());

  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(syncer::APPS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  syncer::StringOrdinal initial_ordinal =
      syncer::StringOrdinal::CreateInitialOrdinal();
  {
    syncer::SyncDataList list = service_->GetAllSyncData(syncer::APPS);
    ASSERT_EQ(list.size(), 1U);

    extensions::AppSyncData app_sync_data(list[0]);
    EXPECT_TRUE(initial_ordinal.Equals(app_sync_data.app_launch_ordinal()));
    EXPECT_TRUE(initial_ordinal.Equals(app_sync_data.page_ordinal()));
  }

  ExtensionSorting* sorting = service_->extension_prefs()->extension_sorting();
  sorting->SetAppLaunchOrdinal(app->id(), initial_ordinal.CreateAfter());
  {
    syncer::SyncDataList list = service_->GetAllSyncData(syncer::APPS);
    ASSERT_EQ(list.size(), 1U);

    extensions::AppSyncData app_sync_data(list[0]);
    EXPECT_TRUE(initial_ordinal.LessThan(app_sync_data.app_launch_ordinal()));
    EXPECT_TRUE(initial_ordinal.Equals(app_sync_data.page_ordinal()));
  }

  sorting->SetPageOrdinal(app->id(), initial_ordinal.CreateAfter());
  {
    syncer::SyncDataList list = service_->GetAllSyncData(syncer::APPS);
    ASSERT_EQ(list.size(), 1U);

    extensions::AppSyncData app_sync_data(list[0]);
    EXPECT_TRUE(initial_ordinal.LessThan(app_sync_data.app_launch_ordinal()));
    EXPECT_TRUE(initial_ordinal.LessThan(app_sync_data.page_ordinal()));
  }
}

TEST_F(ExtensionServiceTest, GetSyncAppDataUserSettingsOnExtensionMoved) {
  InitializeEmptyExtensionService();
  const size_t kAppCount = 3;
  const Extension* apps[kAppCount];
  apps[0] = PackAndInstallCRX(data_dir_.AppendASCII("app1"), INSTALL_NEW);
  apps[1] = PackAndInstallCRX(data_dir_.AppendASCII("app2"), INSTALL_NEW);
  apps[2] = PackAndInstallCRX(data_dir_.AppendASCII("app4"), INSTALL_NEW);
  for (size_t i = 0; i < kAppCount; ++i) {
    ASSERT_TRUE(apps[i]);
    ASSERT_TRUE(apps[i]->is_app());
  }

  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(syncer::APPS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  service_->OnExtensionMoved(apps[0]->id(), apps[1]->id(), apps[2]->id());
  {
    syncer::SyncDataList list = service_->GetAllSyncData(syncer::APPS);
    ASSERT_EQ(list.size(), 3U);

    extensions::AppSyncData data[kAppCount];
    for (size_t i = 0; i < kAppCount; ++i) {
      data[i] = extensions::AppSyncData(list[i]);
    }

    // The sync data is not always in the same order our apps were installed in,
    // so we do that sorting here so we can make sure the values are changed as
    // expected.
    syncer::StringOrdinal app_launch_ordinals[kAppCount];
    for (size_t i = 0; i < kAppCount; ++i) {
      for (size_t j = 0; j < kAppCount; ++j) {
        if (apps[i]->id() == data[j].id())
          app_launch_ordinals[i] = data[j].app_launch_ordinal();
      }
    }

    EXPECT_TRUE(app_launch_ordinals[1].LessThan(app_launch_ordinals[0]));
    EXPECT_TRUE(app_launch_ordinals[0].LessThan(app_launch_ordinals[2]));
  }
}

TEST_F(ExtensionServiceTest, GetSyncDataList) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  InstallCRX(data_dir_.AppendASCII("page_action.crx"), INSTALL_NEW);
  InstallCRX(data_dir_.AppendASCII("theme.crx"), INSTALL_NEW);
  InstallCRX(data_dir_.AppendASCII("theme2.crx"), INSTALL_NEW);

  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(syncer::APPS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));
  service_->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  service_->DisableExtension(page_action, Extension::DISABLE_USER_ACTION);
  TerminateExtension(theme2_crx);

  EXPECT_EQ(0u, service_->GetAllSyncData(syncer::APPS).size());
  EXPECT_EQ(2u, service_->GetAllSyncData(syncer::EXTENSIONS).size());
}

TEST_F(ExtensionServiceTest, ProcessSyncDataUninstall) {
  InitializeEmptyExtensionService();
  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  sync_pb::EntitySpecifics specifics;
  sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
  ext_specifics->set_id(good_crx);
  ext_specifics->set_version("1.0");
  syncer::SyncData sync_data =
      syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
  syncer::SyncChange sync_change(FROM_HERE,
                                 syncer::SyncChange::ACTION_DELETE,
                                 sync_data);
  syncer::SyncChangeList list(1);
  list[0] = sync_change;

  // Should do nothing.
  service_->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_FALSE(service_->GetExtensionById(good_crx, true));

  // Install the extension.
  base::FilePath extension_path = data_dir_.AppendASCII("good.crx");
  InstallCRX(extension_path, INSTALL_NEW);
  EXPECT_TRUE(service_->GetExtensionById(good_crx, true));

  // Should uninstall the extension.
  service_->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_FALSE(service_->GetExtensionById(good_crx, true));

  // Should again do nothing.
  service_->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_FALSE(service_->GetExtensionById(good_crx, true));
}

TEST_F(ExtensionServiceTest, ProcessSyncDataWrongType) {
  InitializeEmptyExtensionService();

  // Install the extension.
  base::FilePath extension_path = data_dir_.AppendASCII("good.crx");
  InstallCRX(extension_path, INSTALL_NEW);
  EXPECT_TRUE(service_->GetExtensionById(good_crx, true));

  sync_pb::EntitySpecifics specifics;
  sync_pb::AppSpecifics* app_specifics = specifics.mutable_app();
  sync_pb::ExtensionSpecifics* extension_specifics =
      app_specifics->mutable_extension();
  extension_specifics->set_id(good_crx);
  extension_specifics->set_version(
      service_->GetInstalledExtension(good_crx)->version()->GetString());

  {
    extension_specifics->set_enabled(true);
    syncer::SyncData sync_data =
        syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
    syncer::SyncChange sync_change(FROM_HERE,
                                   syncer::SyncChange::ACTION_DELETE,
                                   sync_data);
    syncer::SyncChangeList list(1);
    list[0] = sync_change;

    // Should do nothing
    service_->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_TRUE(service_->GetExtensionById(good_crx, true));
  }

  {
    extension_specifics->set_enabled(false);
    syncer::SyncData sync_data =
        syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
    syncer::SyncChange sync_change(FROM_HERE,
                                   syncer::SyncChange::ACTION_UPDATE,
                                   sync_data);
    syncer::SyncChangeList list(1);
    list[0] = sync_change;

    // Should again do nothing.
    service_->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_TRUE(service_->GetExtensionById(good_crx, false));
  }
}

TEST_F(ExtensionServiceTest, ProcessSyncDataSettings) {
  InitializeEmptyExtensionService();
  InitializeExtensionProcessManager();
  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  EXPECT_TRUE(service_->IsExtensionEnabled(good_crx));
  EXPECT_FALSE(service_->IsIncognitoEnabled(good_crx));

  sync_pb::EntitySpecifics specifics;
  sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
  ext_specifics->set_id(good_crx);
  ext_specifics->set_version(
      service_->GetInstalledExtension(good_crx)->version()->GetString());
  ext_specifics->set_enabled(false);

  {
    syncer::SyncData sync_data =
        syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
    syncer::SyncChange sync_change(FROM_HERE,
                                   syncer::SyncChange::ACTION_UPDATE,
                                   sync_data);
    syncer::SyncChangeList list(1);
    list[0] = sync_change;
    service_->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_FALSE(service_->IsExtensionEnabled(good_crx));
    EXPECT_FALSE(service_->IsIncognitoEnabled(good_crx));
  }

  {
    ext_specifics->set_enabled(true);
    ext_specifics->set_incognito_enabled(true);
    syncer::SyncData sync_data =
        syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
    syncer::SyncChange sync_change(FROM_HERE,
                                   syncer::SyncChange::ACTION_UPDATE,
                                   sync_data);
    syncer::SyncChangeList list(1);
    list[0] = sync_change;
    service_->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_TRUE(service_->IsExtensionEnabled(good_crx));
    EXPECT_TRUE(service_->IsIncognitoEnabled(good_crx));
  }

  {
    ext_specifics->set_enabled(false);
    ext_specifics->set_incognito_enabled(true);
    syncer::SyncData sync_data =
        syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
    syncer::SyncChange sync_change(FROM_HERE,
                                   syncer::SyncChange::ACTION_UPDATE,
                                   sync_data);
    syncer::SyncChangeList list(1);
    list[0] = sync_change;
    service_->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_FALSE(service_->IsExtensionEnabled(good_crx));
    EXPECT_TRUE(service_->IsIncognitoEnabled(good_crx));
  }

  EXPECT_FALSE(service_->pending_extension_manager()->IsIdPending(good_crx));
}

TEST_F(ExtensionServiceTest, ProcessSyncDataTerminatedExtension) {
  InitializeExtensionServiceWithUpdater();
  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  TerminateExtension(good_crx);
  EXPECT_TRUE(service_->IsExtensionEnabled(good_crx));
  EXPECT_FALSE(service_->IsIncognitoEnabled(good_crx));

  sync_pb::EntitySpecifics specifics;
  sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
  ext_specifics->set_id(good_crx);
  ext_specifics->set_version(
      service_->GetInstalledExtension(good_crx)->version()->GetString());
  ext_specifics->set_enabled(false);
  ext_specifics->set_incognito_enabled(true);
  syncer::SyncData sync_data =
      syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
  syncer::SyncChange sync_change(FROM_HERE,
                                 syncer::SyncChange::ACTION_UPDATE,
                                 sync_data);
  syncer::SyncChangeList list(1);
  list[0] = sync_change;

  service_->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_FALSE(service_->IsExtensionEnabled(good_crx));
  EXPECT_TRUE(service_->IsIncognitoEnabled(good_crx));

  EXPECT_FALSE(service_->pending_extension_manager()->IsIdPending(good_crx));
}

TEST_F(ExtensionServiceTest, ProcessSyncDataVersionCheck) {
  InitializeExtensionServiceWithUpdater();
  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  InstallCRX(data_dir_.AppendASCII("good.crx"), INSTALL_NEW);
  EXPECT_TRUE(service_->IsExtensionEnabled(good_crx));
  EXPECT_FALSE(service_->IsIncognitoEnabled(good_crx));

  sync_pb::EntitySpecifics specifics;
  sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
  ext_specifics->set_id(good_crx);
  ext_specifics->set_enabled(true);

  {
    ext_specifics->set_version(
        service_->GetInstalledExtension(good_crx)->version()->GetString());
    syncer::SyncData sync_data =
        syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
    syncer::SyncChange sync_change(FROM_HERE,
                                   syncer::SyncChange::ACTION_UPDATE,
                                   sync_data);
    syncer::SyncChangeList list(1);
    list[0] = sync_change;

    // Should do nothing if extension version == sync version.
    service_->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_FALSE(service_->updater()->WillCheckSoon());
  }

  // Should do nothing if extension version > sync version (but see
  // the TODO in ProcessExtensionSyncData).
  {
    ext_specifics->set_version("0.0.0.0");
    syncer::SyncData sync_data =
        syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
    syncer::SyncChange sync_change(FROM_HERE,
                                   syncer::SyncChange::ACTION_UPDATE,
                                   sync_data);
    syncer::SyncChangeList list(1);
    list[0] = sync_change;

    service_->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_FALSE(service_->updater()->WillCheckSoon());
  }

  // Should kick off an update if extension version < sync version.
  {
    ext_specifics->set_version("9.9.9.9");
    syncer::SyncData sync_data =
        syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
    syncer::SyncChange sync_change(FROM_HERE,
                                   syncer::SyncChange::ACTION_UPDATE,
                                   sync_data);
    syncer::SyncChangeList list(1);
    list[0] = sync_change;

    service_->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_TRUE(service_->updater()->WillCheckSoon());
  }

  EXPECT_FALSE(service_->pending_extension_manager()->IsIdPending(good_crx));
}

TEST_F(ExtensionServiceTest, ProcessSyncDataNotInstalled) {
  InitializeExtensionServiceWithUpdater();
  TestSyncProcessorStub processor;
  service_->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      scoped_ptr<syncer::SyncChangeProcessor>(new TestSyncProcessorStub),
      scoped_ptr<syncer::SyncErrorFactory>(new syncer::SyncErrorFactoryMock()));

  sync_pb::EntitySpecifics specifics;
  sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
  ext_specifics->set_id(good_crx);
  ext_specifics->set_enabled(false);
  ext_specifics->set_incognito_enabled(true);
  ext_specifics->set_update_url("http://www.google.com/");
  ext_specifics->set_version("1.2.3.4");
  syncer::SyncData sync_data =
      syncer::SyncData::CreateLocalData(good_crx, "Name", specifics);
  syncer::SyncChange sync_change(FROM_HERE,
                                 syncer::SyncChange::ACTION_UPDATE,
                                 sync_data);
  syncer::SyncChangeList list(1);
  list[0] = sync_change;


  EXPECT_TRUE(service_->IsExtensionEnabled(good_crx));
  EXPECT_FALSE(service_->IsIncognitoEnabled(good_crx));
  service_->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_TRUE(service_->updater()->WillCheckSoon());
  EXPECT_FALSE(service_->IsExtensionEnabled(good_crx));
  EXPECT_TRUE(service_->IsIncognitoEnabled(good_crx));

  const extensions::PendingExtensionInfo* info;
  EXPECT_TRUE((info = service_->pending_extension_manager()->
      GetById(good_crx)));
  EXPECT_EQ(ext_specifics->update_url(), info->update_url().spec());
  EXPECT_TRUE(info->is_from_sync());
  EXPECT_TRUE(info->install_silently());
  EXPECT_EQ(Manifest::INTERNAL, info->install_source());
  // TODO(akalin): Figure out a way to test |info.ShouldAllowInstall()|.
}

TEST_F(ExtensionServiceTest, InstallPriorityExternalUpdateUrl) {
  InitializeEmptyExtensionService();

  base::FilePath path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_NEW);
  ValidatePrefKeyCount(1u);
  ValidateIntegerPref(good_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(good_crx, "location", Manifest::INTERNAL);

  extensions::PendingExtensionManager* pending =
      service_->pending_extension_manager();
  EXPECT_FALSE(pending->IsIdPending(kGoodId));

  // Skip install when the location is the same.
  EXPECT_FALSE(
      service_->OnExternalExtensionUpdateUrlFound(
          kGoodId, GURL(kGoodUpdateURL), Manifest::INTERNAL));
  EXPECT_FALSE(pending->IsIdPending(kGoodId));

  // Install when the location has higher priority.
  EXPECT_TRUE(
      service_->OnExternalExtensionUpdateUrlFound(
          kGoodId, GURL(kGoodUpdateURL), Manifest::EXTERNAL_POLICY_DOWNLOAD));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));

  // Try the low priority again.  Should be rejected.
  EXPECT_FALSE(
      service_->OnExternalExtensionUpdateUrlFound(
          kGoodId, GURL(kGoodUpdateURL), Manifest::EXTERNAL_PREF_DOWNLOAD));
  // The existing record should still be present in the pending extension
  // manager.
  EXPECT_TRUE(pending->IsIdPending(kGoodId));

  pending->Remove(kGoodId);

  // Skip install when the location has the same priority as the installed
  // location.
  EXPECT_FALSE(service_->OnExternalExtensionUpdateUrlFound(
      kGoodId, GURL(kGoodUpdateURL), Manifest::INTERNAL));

  EXPECT_FALSE(pending->IsIdPending(kGoodId));
}

TEST_F(ExtensionServiceTest, InstallPriorityExternalLocalFile) {
  Version older_version("0.1.0.0");
  Version newer_version("2.0.0.0");

  // We don't want the extension to be installed.  A path that doesn't
  // point to a valid CRX ensures this.
  const base::FilePath kInvalidPathToCrx = base::FilePath();

  const int kCreationFlags = 0;
  const bool kDontMarkAcknowledged = false;

  InitializeEmptyExtensionService();

  // The test below uses install source constants to test that
  // priority is enforced.  It assumes a specific ranking of install
  // sources: Registry (EXTERNAL_REGISTRY) overrides external pref
  // (EXTERNAL_PREF), and external pref overrides user install (INTERNAL).
  // The following assertions verify these assumptions:
  ASSERT_EQ(Manifest::EXTERNAL_REGISTRY,
            Manifest::GetHigherPriorityLocation(Manifest::EXTERNAL_REGISTRY,
                                                 Manifest::EXTERNAL_PREF));
  ASSERT_EQ(Manifest::EXTERNAL_REGISTRY,
            Manifest::GetHigherPriorityLocation(Manifest::EXTERNAL_REGISTRY,
                                                 Manifest::INTERNAL));
  ASSERT_EQ(Manifest::EXTERNAL_PREF,
            Manifest::GetHigherPriorityLocation(Manifest::EXTERNAL_PREF,
                                                 Manifest::INTERNAL));

  extensions::PendingExtensionManager* pending =
      service_->pending_extension_manager();
  EXPECT_FALSE(pending->IsIdPending(kGoodId));

  // Simulate an external source adding the extension as INTERNAL.
  EXPECT_TRUE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &older_version, kInvalidPathToCrx,
          Manifest::INTERNAL, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));
  WaitForCrxInstall(kInvalidPathToCrx, INSTALL_FAILED);

  // Simulate an external source adding the extension as EXTERNAL_PREF.
  EXPECT_TRUE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &older_version, kInvalidPathToCrx,
          Manifest::EXTERNAL_PREF, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));
  WaitForCrxInstall(kInvalidPathToCrx, INSTALL_FAILED);

  // Simulate an external source adding as EXTERNAL_PREF again.
  // This is rejected because the version and the location are the same as
  // the previous installation, which is still pending.
  EXPECT_FALSE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &older_version, kInvalidPathToCrx,
          Manifest::EXTERNAL_PREF, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));

  // Try INTERNAL again.  Should fail.
  EXPECT_FALSE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &older_version, kInvalidPathToCrx,
          Manifest::INTERNAL, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));

  // Now the registry adds the extension.
  EXPECT_TRUE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &older_version, kInvalidPathToCrx,
          Manifest::EXTERNAL_REGISTRY, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));
  WaitForCrxInstall(kInvalidPathToCrx, INSTALL_FAILED);

  // Registry outranks both external pref and internal, so both fail.
  EXPECT_FALSE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &older_version, kInvalidPathToCrx,
          Manifest::EXTERNAL_PREF, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));

  EXPECT_FALSE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &older_version, kInvalidPathToCrx,
          Manifest::INTERNAL, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));

  pending->Remove(kGoodId);

  // Install the extension.
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  const Extension* ext = InstallCRX(path, INSTALL_NEW);
  ValidatePrefKeyCount(1u);
  ValidateIntegerPref(good_crx, "state", Extension::ENABLED);
  ValidateIntegerPref(good_crx, "location", Manifest::INTERNAL);

  // Now test the logic of OnExternalExtensionFileFound() when the extension
  // being added is already installed.

  // Tests assume |older_version| is less than the installed version, and
  // |newer_version| is greater.  Verify this:
  ASSERT_TRUE(older_version.IsOlderThan(ext->VersionString()));
  ASSERT_TRUE(ext->version()->IsOlderThan(newer_version.GetString()));

  // An external install for the same location should fail if the version is
  // older, or the same, and succeed if the version is newer.

  // Older than the installed version...
  EXPECT_FALSE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &older_version, kInvalidPathToCrx,
          Manifest::INTERNAL, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_FALSE(pending->IsIdPending(kGoodId));

  // Same version as the installed version...
  EXPECT_FALSE(
      service_->OnExternalExtensionFileFound(
          kGoodId, ext->version(), kInvalidPathToCrx,
          Manifest::INTERNAL, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_FALSE(pending->IsIdPending(kGoodId));

  // Newer than the installed version...
  EXPECT_TRUE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &newer_version, kInvalidPathToCrx,
          Manifest::INTERNAL, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));

  // An external install for a higher priority install source should succeed
  // if the version is greater.  |older_version| is not...
  EXPECT_FALSE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &older_version, kInvalidPathToCrx,
          Manifest::EXTERNAL_PREF, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));

  // |newer_version| is newer.
  EXPECT_TRUE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &newer_version, kInvalidPathToCrx,
          Manifest::EXTERNAL_PREF, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));

  // An external install for an even higher priority install source should
  // succeed if the version is greater.
  EXPECT_TRUE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &newer_version, kInvalidPathToCrx,
          Manifest::EXTERNAL_REGISTRY, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));

  // Because EXTERNAL_PREF is a lower priority source than EXTERNAL_REGISTRY,
  // adding from external pref will now fail.
  EXPECT_FALSE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &newer_version, kInvalidPathToCrx,
          Manifest::EXTERNAL_PREF, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE(pending->IsIdPending(kGoodId));
}

TEST_F(ExtensionServiceTest, ConcurrentExternalLocalFile) {
  Version kVersion123("1.2.3");
  Version kVersion124("1.2.4");
  Version kVersion125("1.2.5");
  const base::FilePath kInvalidPathToCrx = base::FilePath();
  const int kCreationFlags = 0;
  const bool kDontMarkAcknowledged = false;

  InitializeEmptyExtensionService();

  extensions::PendingExtensionManager* pending =
      service_->pending_extension_manager();
  EXPECT_FALSE(pending->IsIdPending(kGoodId));

  // An external provider starts installing from a local crx.
  EXPECT_TRUE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &kVersion123, kInvalidPathToCrx,
          Manifest::EXTERNAL_PREF, kCreationFlags, kDontMarkAcknowledged));
  const extensions::PendingExtensionInfo* info;
  EXPECT_TRUE((info = pending->GetById(kGoodId)));
  EXPECT_TRUE(info->version().IsValid());
  EXPECT_TRUE(info->version().Equals(kVersion123));

  // Adding a newer version overrides the currently pending version.
  EXPECT_TRUE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &kVersion124, kInvalidPathToCrx,
          Manifest::EXTERNAL_PREF, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE((info = pending->GetById(kGoodId)));
  EXPECT_TRUE(info->version().IsValid());
  EXPECT_TRUE(info->version().Equals(kVersion124));

  // Adding an older version fails.
  EXPECT_FALSE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &kVersion123, kInvalidPathToCrx,
          Manifest::EXTERNAL_PREF, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE((info = pending->GetById(kGoodId)));
  EXPECT_TRUE(info->version().IsValid());
  EXPECT_TRUE(info->version().Equals(kVersion124));

  // Adding an older version fails even when coming from a higher-priority
  // location.
  EXPECT_FALSE(
      service_->OnExternalExtensionFileFound(
          kGoodId, &kVersion123, kInvalidPathToCrx,
          Manifest::EXTERNAL_REGISTRY, kCreationFlags, kDontMarkAcknowledged));
  EXPECT_TRUE((info = pending->GetById(kGoodId)));
  EXPECT_TRUE(info->version().IsValid());
  EXPECT_TRUE(info->version().Equals(kVersion124));

  // Adding the latest version from the webstore overrides a specific version.
  GURL kUpdateUrl("http://example.com/update");
  EXPECT_TRUE(
      service_->OnExternalExtensionUpdateUrlFound(
          kGoodId, kUpdateUrl, Manifest::EXTERNAL_POLICY_DOWNLOAD));
  EXPECT_TRUE((info = pending->GetById(kGoodId)));
  EXPECT_FALSE(info->version().IsValid());
}

// This makes sure we can package and install CRX files that use whitelisted
// permissions.
TEST_F(ExtensionServiceTest, InstallWhitelistedExtension) {
  std::string test_id = "hdkklepkcpckhnpgjnmbdfhehckloojk";
  CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      switches::kWhitelistedExtensionID, test_id);

  InitializeEmptyExtensionService();
  base::FilePath path = data_dir_
      .AppendASCII("permissions");
  base::FilePath pem_path = path
      .AppendASCII("whitelist.pem");
  path = path
      .AppendASCII("whitelist");

  const Extension* extension = PackAndInstallCRX(path, pem_path, INSTALL_NEW);
  EXPECT_EQ(0u, GetErrors().size());
  ASSERT_EQ(1u, service_->extensions()->size());
  EXPECT_EQ(test_id, extension->id());
}

// Test that when multiple sources try to install an extension,
// we consistently choose the right one. To make tests easy to read,
// methods that fake requests to install crx files in several ways
// are provided.
class ExtensionSourcePriorityTest : public ExtensionServiceTest {
 public:
  virtual void SetUp() {
    ExtensionServiceTest::SetUp();

    // All tests use a single extension.  Put the id and path in member vars
    // that all methods can read.
    crx_id_ = kGoodId;
    crx_path_ = data_dir_.AppendASCII("good.crx");
  }

  // Fake an external source adding a URL to fetch an extension from.
  bool AddPendingExternalPrefUrl() {
    return service_->pending_extension_manager()->AddFromExternalUpdateUrl(
        crx_id_, GURL(), Manifest::EXTERNAL_PREF_DOWNLOAD);
  }

  // Fake an external file from external_extensions.json.
  bool AddPendingExternalPrefFileInstall() {
    Version version("1.0.0.0");

    return service_->OnExternalExtensionFileFound(
        crx_id_, &version, crx_path_, Manifest::EXTERNAL_PREF,
        Extension::NO_FLAGS, false);
  }

  // Fake a request from sync to install an extension.
  bool AddPendingSyncInstall() {
    return service_->pending_extension_manager()->AddFromSync(
        crx_id_, GURL(kGoodUpdateURL), &IsExtension, kGoodInstallSilently);
  }

  // Fake a policy install.
  bool AddPendingPolicyInstall() {
    // Get path to the CRX with id |kGoodId|.
    return service_->OnExternalExtensionUpdateUrlFound(
        crx_id_, GURL(), Manifest::EXTERNAL_POLICY_DOWNLOAD);
  }

  // Get the install source of a pending extension.
  Manifest::Location GetPendingLocation() {
    const extensions::PendingExtensionInfo* info;
    EXPECT_TRUE((info = service_->pending_extension_manager()->
        GetById(crx_id_)));
    return info->install_source();
  }

  // Is an extension pending from a sync request?
  bool GetPendingIsFromSync() {
    const extensions::PendingExtensionInfo* info;
    EXPECT_TRUE((info = service_->pending_extension_manager()->
        GetById(crx_id_)));
    return info->is_from_sync();
  }

  // Is the CRX id these tests use pending?
  bool IsCrxPending() {
    return service_->pending_extension_manager()->IsIdPending(crx_id_);
  }

  // Is an extension installed?
  bool IsCrxInstalled() {
    return (service_->GetExtensionById(crx_id_, true) != NULL);
  }

 protected:
  // All tests use a single extension.  Making the id and path member
  // vars avoids pasing the same argument to every method.
  std::string crx_id_;
  base::FilePath crx_path_;
};

// Test that a pending request for installation of an external CRX from
// an update URL overrides a pending request to install the same extension
// from sync.
TEST_F(ExtensionSourcePriorityTest, PendingExternalFileOverSync) {
  InitializeEmptyExtensionService();

  ASSERT_FALSE(IsCrxInstalled());

  // Install pending extension from sync.
  EXPECT_TRUE(AddPendingSyncInstall());
  ASSERT_EQ(Manifest::INTERNAL, GetPendingLocation());
  EXPECT_TRUE(GetPendingIsFromSync());
  ASSERT_FALSE(IsCrxInstalled());

  // Install pending as external prefs json would.
  AddPendingExternalPrefFileInstall();
  ASSERT_EQ(Manifest::EXTERNAL_PREF, GetPendingLocation());
  ASSERT_FALSE(IsCrxInstalled());

  // Another request from sync should be ignored.
  EXPECT_FALSE(AddPendingSyncInstall());
  ASSERT_EQ(Manifest::EXTERNAL_PREF, GetPendingLocation());
  ASSERT_FALSE(IsCrxInstalled());

  WaitForCrxInstall(crx_path_, INSTALL_NEW);
  ASSERT_TRUE(IsCrxInstalled());
}

// Test that an install of an external CRX from an update overrides
// an install of the same extension from sync.
TEST_F(ExtensionSourcePriorityTest, PendingExternalUrlOverSync) {
  InitializeEmptyExtensionService();
  ASSERT_FALSE(IsCrxInstalled());

  EXPECT_TRUE(AddPendingSyncInstall());
  ASSERT_EQ(Manifest::INTERNAL, GetPendingLocation());
  EXPECT_TRUE(GetPendingIsFromSync());
  ASSERT_FALSE(IsCrxInstalled());

  ASSERT_TRUE(AddPendingExternalPrefUrl());
  ASSERT_EQ(Manifest::EXTERNAL_PREF_DOWNLOAD, GetPendingLocation());
  EXPECT_FALSE(GetPendingIsFromSync());
  ASSERT_FALSE(IsCrxInstalled());

  EXPECT_FALSE(AddPendingSyncInstall());
  ASSERT_EQ(Manifest::EXTERNAL_PREF_DOWNLOAD, GetPendingLocation());
  EXPECT_FALSE(GetPendingIsFromSync());
  ASSERT_FALSE(IsCrxInstalled());
}

// Test that an external install request stops sync from installing
// the same extension.
TEST_F(ExtensionSourcePriorityTest, InstallExternalBlocksSyncRequest) {
  InitializeEmptyExtensionService();
  ASSERT_FALSE(IsCrxInstalled());

  // External prefs starts an install.
  AddPendingExternalPrefFileInstall();

  // Crx installer was made, but has not yet run.
  ASSERT_FALSE(IsCrxInstalled());

  // Before the CRX installer runs, Sync requests that the same extension
  // be installed. Should fail, because an external source is pending.
  ASSERT_FALSE(AddPendingSyncInstall());

  // Wait for the external source to install.
  WaitForCrxInstall(crx_path_, INSTALL_NEW);
  ASSERT_TRUE(IsCrxInstalled());

  // Now that the extension is installed, sync request should fail
  // because the extension is already installed.
  ASSERT_FALSE(AddPendingSyncInstall());
}

// Test that installing an external extension displays a GlobalError.
TEST_F(ExtensionServiceTest, ExternalInstallGlobalError) {
  FeatureSwitch::ScopedOverride prompt(
      FeatureSwitch::prompt_for_external_extensions(), true);

  InitializeEmptyExtensionService();
  MockExtensionProvider* provider =
      new MockExtensionProvider(service_, Manifest::EXTERNAL_PREF);
  AddMockExternalProvider(provider);

  service_->UpdateExternalExtensionAlert();
  // Should return false, meaning there aren't any extensions that the user
  // needs to know about.
  EXPECT_FALSE(extensions::HasExternalInstallError(service_));

  // This is a normal extension, installed normally.
  // This should NOT trigger an alert.
  set_extensions_enabled(true);
  base::FilePath path = data_dir_.AppendASCII("good.crx");
  InstallCRX(path, INSTALL_NEW);

  service_->CheckForExternalUpdates();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(extensions::HasExternalInstallError(service_));

  // A hosted app, installed externally.
  // This should NOT trigger an alert.
  provider->UpdateOrAddExtension(hosted_app, "1.0.0.0",
                                 data_dir_.AppendASCII("hosted_app.crx"));

  service_->CheckForExternalUpdates();
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();
  EXPECT_FALSE(extensions::HasExternalInstallError(service_));

  // Another normal extension, but installed externally.
  // This SHOULD trigger an alert.
  provider->UpdateOrAddExtension(page_action, "1.0.0.0",
                                 data_dir_.AppendASCII("page_action.crx"));

  service_->CheckForExternalUpdates();
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();
  EXPECT_TRUE(extensions::HasExternalInstallError(service_));
}

// Test that external extensions are initially disabled, and that enabling
// them clears the prompt.
TEST_F(ExtensionServiceTest, ExternalInstallInitiallyDisabled) {
  FeatureSwitch::ScopedOverride prompt(
      FeatureSwitch::prompt_for_external_extensions(), true);

  InitializeEmptyExtensionService();
  MockExtensionProvider* provider =
      new MockExtensionProvider(service_, Manifest::EXTERNAL_PREF);
  AddMockExternalProvider(provider);

  provider->UpdateOrAddExtension(page_action, "1.0.0.0",
                                 data_dir_.AppendASCII("page_action.crx"));

  service_->CheckForExternalUpdates();
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();
  EXPECT_TRUE(extensions::HasExternalInstallError(service_));
  EXPECT_FALSE(service_->IsExtensionEnabled(page_action));

  const Extension* extension =
      service_->disabled_extensions()->GetByID(page_action);
  EXPECT_TRUE(extension);
  EXPECT_EQ(page_action, extension->id());

  service_->EnableExtension(page_action);
  EXPECT_FALSE(extensions::HasExternalInstallError(service_));
  EXPECT_TRUE(service_->IsExtensionEnabled(page_action));
}

// Test that installing multiple external extensions works.
TEST_F(ExtensionServiceTest, ExternalInstallMultiple) {
  FeatureSwitch::ScopedOverride prompt(
      FeatureSwitch::prompt_for_external_extensions(), true);

  InitializeEmptyExtensionService();
  MockExtensionProvider* provider =
      new MockExtensionProvider(service_, Manifest::EXTERNAL_PREF);
  AddMockExternalProvider(provider);

  provider->UpdateOrAddExtension(page_action, "1.0.0.0",
                                 data_dir_.AppendASCII("page_action.crx"));
  provider->UpdateOrAddExtension(good_crx, "1.0.0.0",
                                 data_dir_.AppendASCII("good.crx"));
  provider->UpdateOrAddExtension(theme_crx, "2.0",
                                 data_dir_.AppendASCII("theme.crx"));

  service_->CheckForExternalUpdates();
  int count = 3;
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      base::Bind(&WaitForCountNotificationsCallback, &count)).Wait();
  EXPECT_TRUE(extensions::HasExternalInstallError(service_));
  EXPECT_FALSE(service_->IsExtensionEnabled(page_action));
  EXPECT_FALSE(service_->IsExtensionEnabled(good_crx));
  EXPECT_FALSE(service_->IsExtensionEnabled(theme_crx));

  service_->EnableExtension(page_action);
  EXPECT_TRUE(extensions::HasExternalInstallError(service_));
  EXPECT_FALSE(extensions::HasExternalInstallBubble(service_));
  service_->EnableExtension(theme_crx);
  EXPECT_TRUE(extensions::HasExternalInstallError(service_));
  EXPECT_FALSE(extensions::HasExternalInstallBubble(service_));
  service_->EnableExtension(good_crx);
  EXPECT_FALSE(extensions::HasExternalInstallError(service_));
  EXPECT_FALSE(extensions::HasExternalInstallBubble(service_));
}

// Test that there is a bubble for external extensions that update
// from the webstore if the profile is not new.
TEST_F(ExtensionServiceTest, ExternalInstallUpdatesFromWebstoreOldProfile) {
  FeatureSwitch::ScopedOverride prompt(
      FeatureSwitch::prompt_for_external_extensions(), true);

  // This sets up the ExtensionPrefs used by our ExtensionService to be
  // post-first run.
  InitializeExtensionServiceHelper(false, false);

  base::FilePath crx_path = temp_dir_.path().AppendASCII("webstore.crx");
  PackCRX(data_dir_.AppendASCII("update_from_webstore"),
          data_dir_.AppendASCII("update_from_webstore.pem"),
          crx_path);

  MockExtensionProvider* provider =
      new MockExtensionProvider(service_, Manifest::EXTERNAL_PREF);
  AddMockExternalProvider(provider);
  provider->UpdateOrAddExtension(updates_from_webstore, "1", crx_path);

  service_->CheckForExternalUpdates();
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();
  EXPECT_TRUE(extensions::HasExternalInstallError(service_));
  EXPECT_TRUE(extensions::HasExternalInstallBubble(service_));
  EXPECT_FALSE(service_->IsExtensionEnabled(updates_from_webstore));
}

// Test that there is no bubble for external extensions if the profile is new.
TEST_F(ExtensionServiceTest, ExternalInstallUpdatesFromWebstoreNewProfile) {
  FeatureSwitch::ScopedOverride prompt(
      FeatureSwitch::prompt_for_external_extensions(), true);

  InitializeEmptyExtensionService();

  base::FilePath crx_path = temp_dir_.path().AppendASCII("webstore.crx");
  PackCRX(data_dir_.AppendASCII("update_from_webstore"),
          data_dir_.AppendASCII("update_from_webstore.pem"),
          crx_path);

  MockExtensionProvider* provider =
      new MockExtensionProvider(service_, Manifest::EXTERNAL_PREF);
  AddMockExternalProvider(provider);
  provider->UpdateOrAddExtension(updates_from_webstore, "1", crx_path);

  service_->CheckForExternalUpdates();
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();
  EXPECT_TRUE(extensions::HasExternalInstallError(service_));
  EXPECT_FALSE(extensions::HasExternalInstallBubble(service_));
  EXPECT_FALSE(service_->IsExtensionEnabled(updates_from_webstore));
}

TEST_F(ExtensionServiceTest, InstallBlacklistedExtension) {
  InitializeEmptyExtensionService();

  scoped_refptr<Extension> extension = extensions::ExtensionBuilder()
      .SetManifest(extensions::DictionaryBuilder()
          .Set("name", "extension")
          .Set("version", "1.0")
          .Set("manifest_version", 2).Build())
      .Build();
  ASSERT_TRUE(extension.get());
  const std::string& id = extension->id();

  std::set<std::string> id_set;
  id_set.insert(id);
  extensions::ExtensionNotificationObserver notifications(
      content::NotificationService::AllSources(), id_set);

  // Installation should be allowed but the extension should never have been
  // loaded and it should be blacklisted in prefs.
  service_->OnExtensionInstalled(
      extension.get(),
      syncer::StringOrdinal(),
      false /* has requirement errors */,
      extensions::Blacklist::BLACKLISTED,
      false /* wait for idle */);
  base::RunLoop().RunUntilIdle();

  // Extension was installed but not loaded.
  EXPECT_TRUE(notifications.CheckNotifications(
      chrome::NOTIFICATION_EXTENSION_INSTALLED));

  EXPECT_TRUE(service_->GetInstalledExtension(id));
  EXPECT_FALSE(service_->extensions()->Contains(id));
  EXPECT_TRUE(service_->blacklisted_extensions()->Contains(id));
  EXPECT_TRUE(service_->extension_prefs()->IsExtensionBlacklisted(id));
  EXPECT_TRUE(
      service_->extension_prefs()->IsBlacklistedExtensionAcknowledged(id));
}
