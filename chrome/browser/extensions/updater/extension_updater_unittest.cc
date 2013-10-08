// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>
#include <map>
#include <set>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread.h"
#include "base/version.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/blacklist.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/extensions/extension_error_reporter.h"
#include "chrome/browser/extensions/extension_sync_data.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/test_blacklist.h"
#include "chrome/browser/extensions/test_extension_prefs.h"
#include "chrome/browser/extensions/test_extension_service.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/extensions/updater/extension_downloader.h"
#include "chrome/browser/extensions/updater/extension_downloader_delegate.h"
#include "chrome/browser/extensions/updater/extension_updater.h"
#include "chrome/browser/extensions/updater/manifest_fetch_data.h"
#include "chrome/browser/extensions/updater/request_queue_impl.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/prefs/pref_service_syncable.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/common/omaha_query_params/omaha_query_params.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/test_utils.h"
#include "extensions/common/id_util.h"
#include "libxml/globals.h"
#include "net/base/backoff_entry.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "net/url_request/url_request_status.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#endif

using base::Time;
using base::TimeDelta;
using content::BrowserThread;
using testing::DoAll;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::Return;
using testing::SetArgPointee;
using testing::_;

namespace extensions {

typedef ExtensionDownloaderDelegate::Error Error;
typedef ExtensionDownloaderDelegate::PingResult PingResult;

namespace {

const net::BackoffEntry::Policy kNoBackoffPolicy = {
  // Number of initial errors (in sequence) to ignore before applying
  // exponential back-off rules.
  1000,

  // Initial delay for exponential back-off in ms.
  0,

  // Factor by which the waiting time will be multiplied.
  0,

  // Fuzzing percentage. ex: 10% will spread requests randomly
  // between 90%-100% of the calculated time.
  0,

  // Maximum amount of time we are willing to delay our request in ms.
  0,

  // Time to keep an entry from being discarded even when it
  // has no significant state, -1 to never discard.
  -1,

  // Don't use initial delay unless the last request was an error.
  false,
};

const char kEmptyUpdateUrlData[] = "";

int kExpectedLoadFlags =
    net::LOAD_DO_NOT_SEND_COOKIES |
    net::LOAD_DO_NOT_SAVE_COOKIES |
    net::LOAD_DISABLE_CACHE;

const ManifestFetchData::PingData kNeverPingedData(
    ManifestFetchData::kNeverPinged, ManifestFetchData::kNeverPinged, true);

class MockExtensionDownloaderDelegate : public ExtensionDownloaderDelegate {
 public:
  MOCK_METHOD4(OnExtensionDownloadFailed, void(const std::string&,
                                               Error,
                                               const PingResult&,
                                               const std::set<int>&));
  MOCK_METHOD6(OnExtensionDownloadFinished, void(const std::string&,
                                                 const base::FilePath&,
                                                 const GURL&,
                                                 const std::string&,
                                                 const PingResult&,
                                                 const std::set<int>&));
  MOCK_METHOD5(OnBlacklistDownloadFinished, void(const std::string&,
                                                 const std::string&,
                                                 const std::string&,
                                                 const PingResult&,
                                                 const std::set<int>&));
  MOCK_METHOD2(GetPingDataForExtension,
               bool(const std::string&, ManifestFetchData::PingData*));
  MOCK_METHOD1(GetUpdateUrlData, std::string(const std::string&));
  MOCK_METHOD1(IsExtensionPending, bool(const std::string&));
  MOCK_METHOD2(GetExtensionExistingVersion,
               bool(const std::string&, std::string*));

  void Wait() {
    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;
    quit_closure_ = runner->QuitClosure();
    runner->Run();
    quit_closure_.Reset();
  }

  void Quit() {
    quit_closure_.Run();
  }

 private:
  base::Closure quit_closure_;
};

const int kNotificationsObserved[] = {
  chrome::NOTIFICATION_EXTENSION_UPDATING_STARTED,
  chrome::NOTIFICATION_EXTENSION_UPDATE_FOUND,
};

// A class that observes the notifications sent by the ExtensionUpdater and
// the ExtensionDownloader.
class NotificationsObserver : public content::NotificationObserver {
 public:
  NotificationsObserver() {
    for (size_t i = 0; i < arraysize(kNotificationsObserved); ++i) {
      count_[i] = 0;
      registrar_.Add(this,
                     kNotificationsObserved[i],
                     content::NotificationService::AllSources());
    }
  }

  virtual ~NotificationsObserver() {
    for (size_t i = 0; i < arraysize(kNotificationsObserved); ++i) {
      registrar_.Remove(this,
                        kNotificationsObserved[i],
                        content::NotificationService::AllSources());
    }
  }

  size_t StartedCount() { return count_[0]; }
  size_t UpdatedCount() { return count_[1]; }

  bool Updated(const std::string& id) {
    return updated_.find(id) != updated_.end();
  }

  void Wait() {
    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;
    quit_closure_ = runner->QuitClosure();
    runner->Run();
    quit_closure_.Reset();
  }

 private:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    if (!quit_closure_.is_null())
      quit_closure_.Run();
    for (size_t i = 0; i < arraysize(kNotificationsObserved); ++i) {
      if (kNotificationsObserved[i] == type) {
        count_[i]++;
        if (type == chrome::NOTIFICATION_EXTENSION_UPDATE_FOUND) {
          updated_.insert(
              content::Details<UpdateDetails>(details)->id);
        }
        return;
      }
    }
    NOTREACHED();
  }

  content::NotificationRegistrar registrar_;
  size_t count_[arraysize(kNotificationsObserved)];
  std::set<std::string> updated_;
  base::Closure quit_closure_;

  DISALLOW_COPY_AND_ASSIGN(NotificationsObserver);
};

}  // namespace

// Base class for further specialized test classes.
class MockService : public TestExtensionService {
 public:
  explicit MockService(TestExtensionPrefs* prefs)
      : prefs_(prefs),
        pending_extension_manager_(*this),
        blacklist_(prefs_->prefs()) {
  }

  virtual ~MockService() {}

  virtual PendingExtensionManager* pending_extension_manager() OVERRIDE {
    ADD_FAILURE() << "Subclass should override this if it will "
                  << "be accessed by a test.";
    return &pending_extension_manager_;
  }

  Profile* profile() { return &profile_; }

  net::URLRequestContextGetter* request_context() {
    return profile_.GetRequestContext();
  }

  ExtensionPrefs* extension_prefs() { return prefs_->prefs(); }

  PrefService* pref_service() { return prefs_->pref_service(); }

  Blacklist* blacklist() { return &blacklist_; }

  // Creates test extensions and inserts them into list. The name and
  // version are all based on their index. If |update_url| is non-null, it
  // will be used as the update_url for each extension.
  // The |id| is used to distinguish extension names and make sure that
  // no two extensions share the same name.
  void CreateTestExtensions(int id, int count, ExtensionList *list,
                            const std::string* update_url,
                            Manifest::Location location) {
    for (int i = 1; i <= count; i++) {
      DictionaryValue manifest;
      manifest.SetString(extension_manifest_keys::kVersion,
                         base::StringPrintf("%d.0.0.0", i));
      manifest.SetString(extension_manifest_keys::kName,
                         base::StringPrintf("Extension %d.%d", id, i));
      if (update_url)
        manifest.SetString(extension_manifest_keys::kUpdateURL, *update_url);
      scoped_refptr<Extension> e =
          prefs_->AddExtensionWithManifest(manifest, location);
      ASSERT_TRUE(e.get() != NULL);
      list->push_back(e);
    }
  }

 protected:
  TestExtensionPrefs* const prefs_;
  PendingExtensionManager pending_extension_manager_;
  TestingProfile profile_;
  Blacklist blacklist_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MockService);
};


bool ShouldInstallExtensionsOnly(const Extension* extension) {
  return extension->GetType() == Manifest::TYPE_EXTENSION;
}

bool ShouldInstallThemesOnly(const Extension* extension) {
  return extension->is_theme();
}

bool ShouldAlwaysInstall(const Extension* extension) {
  return true;
}

// Loads some pending extension records into a pending extension manager.
void SetupPendingExtensionManagerForTest(
    int count,
    const GURL& update_url,
    PendingExtensionManager* pending_extension_manager) {
  for (int i = 1; i <= count; ++i) {
    PendingExtensionInfo::ShouldAllowInstallPredicate should_allow_install =
        (i % 2 == 0) ? &ShouldInstallThemesOnly : &ShouldInstallExtensionsOnly;
    const bool kIsFromSync = true;
    const bool kInstallSilently = true;
    std::string id = id_util::GenerateId(base::StringPrintf("extension%i", i));

    pending_extension_manager->AddForTesting(
        PendingExtensionInfo(id,
                             update_url,
                             Version(),
                             should_allow_install,
                             kIsFromSync,
                             kInstallSilently,
                             Manifest::INTERNAL));
  }
}

class ServiceForManifestTests : public MockService {
 public:
  explicit ServiceForManifestTests(TestExtensionPrefs* prefs)
      : MockService(prefs) {
  }

  virtual ~ServiceForManifestTests() {}

  virtual const Extension* GetExtensionById(
      const std::string& id, bool include_disabled) const OVERRIDE {
    const Extension* result = extensions_.GetByID(id);
    if (result || !include_disabled)
      return result;
    return disabled_extensions_.GetByID(id);
  }

  virtual const ExtensionSet* extensions() const OVERRIDE {
    return &extensions_;
  }

  virtual const ExtensionSet* disabled_extensions() const OVERRIDE {
    return &disabled_extensions_;
  }

  virtual PendingExtensionManager* pending_extension_manager() OVERRIDE {
    return &pending_extension_manager_;
  }

  virtual const Extension* GetPendingExtensionUpdate(
      const std::string& id) const OVERRIDE {
    return NULL;
  }

  virtual bool IsExtensionEnabled(const std::string& id) const OVERRIDE {
    return !disabled_extensions_.Contains(id);
  }

  void set_extensions(ExtensionList extensions) {
    for (ExtensionList::const_iterator it = extensions.begin();
         it != extensions.end(); ++it) {
      extensions_.Insert(*it);
    }
  }

  void set_disabled_extensions(ExtensionList disabled_extensions) {
    for (ExtensionList::const_iterator it = disabled_extensions.begin();
         it != disabled_extensions.end(); ++it) {
      disabled_extensions_.Insert(*it);
    }
  }

 private:
  ExtensionSet extensions_;
  ExtensionSet disabled_extensions_;
};

class ServiceForDownloadTests : public MockService {
 public:
  explicit ServiceForDownloadTests(TestExtensionPrefs* prefs)
      : MockService(prefs) {
  }

  // Add a fake crx installer to be returned by a call to UpdateExtension()
  // with a specific ID.  Caller keeps ownership of |crx_installer|.
  void AddFakeCrxInstaller(const std::string& id, CrxInstaller* crx_installer) {
    fake_crx_installers_[id] = crx_installer;
  }

  virtual bool UpdateExtension(
      const std::string& id,
      const base::FilePath& extension_path,
      const GURL& download_url,
      CrxInstaller** out_crx_installer) OVERRIDE {
    extension_id_ = id;
    install_path_ = extension_path;
    download_url_ = download_url;

    if (ContainsKey(fake_crx_installers_, id)) {
      *out_crx_installer = fake_crx_installers_[id];
      return true;
    }

    return false;
  }

  virtual PendingExtensionManager* pending_extension_manager() OVERRIDE {
    return &pending_extension_manager_;
  }

  virtual const Extension* GetExtensionById(
      const std::string& id, bool) const OVERRIDE {
    last_inquired_extension_id_ = id;
    return NULL;
  }

  const std::string& extension_id() const { return extension_id_; }
  const base::FilePath& install_path() const { return install_path_; }
  const GURL& download_url() const { return download_url_; }

 private:
  // Hold the set of ids that UpdateExtension() should fake success on.
  // UpdateExtension(id, ...) will return true iff fake_crx_installers_
  // contains key |id|.  |out_install_notification_source| will be set
  // to Source<CrxInstaller(fake_crx_installers_[i]).
  std::map<std::string, CrxInstaller*> fake_crx_installers_;

  std::string extension_id_;
  base::FilePath install_path_;
  GURL download_url_;

  // The last extension ID that GetExtensionById was called with.
  // Mutable because the method that sets it (GetExtensionById) is const
  // in the actual extension service, but must record the last extension
  // ID in this test class.
  mutable std::string last_inquired_extension_id_;
};

static const int kUpdateFrequencySecs = 15;

// Takes a string with KEY=VALUE parameters separated by '&' in |params| and
// puts the key/value pairs into |result|. For keys with no value, the empty
// string is used. So for "a=1&b=foo&c", result would map "a" to "1", "b" to
// "foo", and "c" to "".
static void ExtractParameters(const std::string& params,
                              std::map<std::string, std::string>* result) {
  std::vector<std::string> pairs;
  base::SplitString(params, '&', &pairs);
  for (size_t i = 0; i < pairs.size(); i++) {
    std::vector<std::string> key_val;
    base::SplitString(pairs[i], '=', &key_val);
    if (!key_val.empty()) {
      std::string key = key_val[0];
      EXPECT_TRUE(result->find(key) == result->end());
      (*result)[key] = (key_val.size() == 2) ? key_val[1] : std::string();
    } else {
      NOTREACHED();
    }
  }
}

static void VerifyQueryAndExtractParameters(
    const std::string& query,
    std::map<std::string, std::string>* result) {
  std::map<std::string, std::string> params;
  ExtractParameters(query, &params);

  std::string omaha_params =
      chrome::OmahaQueryParams::Get(chrome::OmahaQueryParams::CRX);
  std::map<std::string, std::string> expected;
  ExtractParameters(omaha_params, &expected);

  for (std::map<std::string, std::string>::iterator it = expected.begin();
       it != expected.end(); ++it) {
    EXPECT_EQ(it->second, params[it->first]);
  }

  EXPECT_EQ(1U, params.count("x"));
  std::string decoded = net::UnescapeURLComponent(
      params["x"], net::UnescapeRule::URL_SPECIAL_CHARS);
  ExtractParameters(decoded, result);
}

// All of our tests that need to use private APIs of ExtensionUpdater live
// inside this class (which is a friend to ExtensionUpdater).
class ExtensionUpdaterTest : public testing::Test {
 public:
  ExtensionUpdaterTest()
      : thread_bundle_(
            content::TestBrowserThreadBundle::IO_MAINLOOP) {
  }

  virtual void SetUp() OVERRIDE {
    prefs_.reset(new TestExtensionPrefs(base::MessageLoopProxy::current()));
    content::RenderProcessHost::SetRunRendererInProcess(true);
  }

  virtual void TearDown() OVERRIDE {
    // Some tests create URLRequestContextGetters, whose destruction must run
    // on the IO thread. Make sure the IO loop spins before shutdown so that
    // those objects are released.
    RunUntilIdle();
    prefs_.reset();
    content::RenderProcessHost::SetRunRendererInProcess(false);
  }

  void RunUntilIdle() {
    prefs_->pref_service()->CommitPendingWrite();
    base::RunLoop().RunUntilIdle();
  }

  void SimulateTimerFired(ExtensionUpdater* updater) {
    EXPECT_TRUE(updater->timer_.IsRunning());
    updater->timer_.Stop();
    updater->TimerFired();
  }

  // Adds a Result with the given data to results.
  void AddParseResult(const std::string& id,
                      const std::string& version,
                      const std::string& url,
                      UpdateManifest::Results* results) {
    UpdateManifest::Result result;
    result.extension_id = id;
    result.version = version;
    result.crx_url = GURL(url);
    results->list.push_back(result);
  }

  void ResetDownloader(ExtensionUpdater* updater,
                       ExtensionDownloader* downloader) {
    EXPECT_FALSE(updater->downloader_.get());
    updater->downloader_.reset(downloader);
  }

  void StartUpdateCheck(ExtensionDownloader* downloader,
                        ManifestFetchData* fetch_data) {
    downloader->StartUpdateCheck(scoped_ptr<ManifestFetchData>(fetch_data));
  }

  size_t ManifestFetchersCount(ExtensionDownloader* downloader) {
    return downloader->manifests_queue_.size() +
           (downloader->manifest_fetcher_.get() ? 1 : 0);
  }

  void TestExtensionUpdateCheckRequests(bool pending) {
    // Create an extension with an update_url.
    ServiceForManifestTests service(prefs_.get());
    std::string update_url("http://foo.com/bar");
    ExtensionList extensions;
    NotificationsObserver observer;
    PendingExtensionManager* pending_extension_manager =
        service.pending_extension_manager();
    if (pending) {
      SetupPendingExtensionManagerForTest(1, GURL(update_url),
                                          pending_extension_manager);
    } else {
      service.CreateTestExtensions(1, 1, &extensions, &update_url,
                                   Manifest::INTERNAL);
      service.set_extensions(extensions);
    }

    // Set up and start the updater.
    net::TestURLFetcherFactory factory;
    ExtensionUpdater updater(
        &service, service.extension_prefs(), service.pref_service(),
        service.profile(), service.blacklist(), 60*60*24);
    updater.Start();
    // Disable blacklist checks (tested elsewhere) so that we only see the
    // update HTTP request.
    ExtensionUpdater::CheckParams check_params;
    check_params.check_blacklist = false;
    updater.set_default_check_params(check_params);

    // Tell the update that it's time to do update checks.
    EXPECT_EQ(0u, observer.StartedCount());
    SimulateTimerFired(&updater);
    EXPECT_EQ(1u, observer.StartedCount());

    // Get the url our mock fetcher was asked to fetch.
    net::TestURLFetcher* fetcher =
        factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
    const GURL& url = fetcher->GetOriginalURL();
    EXPECT_FALSE(url.is_empty());
    EXPECT_TRUE(url.is_valid());
    EXPECT_TRUE(url.SchemeIs("http"));
    EXPECT_EQ("foo.com", url.host());
    EXPECT_EQ("/bar", url.path());

    // Validate the extension request parameters in the query. It should
    // look something like "x=id%3D<id>%26v%3D<version>%26uc".
    EXPECT_TRUE(url.has_query());
    std::map<std::string, std::string> params;
    VerifyQueryAndExtractParameters(url.query(), &params);
    if (pending) {
      EXPECT_TRUE(pending_extension_manager->IsIdPending(params["id"]));
      EXPECT_EQ("0.0.0.0", params["v"]);
    } else {
      EXPECT_EQ(extensions[0]->id(), params["id"]);
      EXPECT_EQ(extensions[0]->VersionString(), params["v"]);
    }
    EXPECT_EQ("", params["uc"]);
  }

  void TestBlacklistUpdateCheckRequests() {
    // Setup and start the updater.
    ServiceForManifestTests service(prefs_.get());
    NotificationsObserver observer;

    net::TestURLFetcherFactory factory;
    ExtensionUpdater updater(
        &service, service.extension_prefs(), service.pref_service(),
        service.profile(), service.blacklist(), 60*60*24);
    updater.Start();

    // Tell the updater that it's time to do update checks.
    EXPECT_EQ(0u, observer.StartedCount());
    SimulateTimerFired(&updater);
    EXPECT_EQ(1u, observer.StartedCount());

    // Get the url our mock fetcher was asked to fetch.
    net::TestURLFetcher* fetcher =
        factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
    ASSERT_FALSE(fetcher == NULL);
    const GURL& url = fetcher->GetOriginalURL();

    EXPECT_FALSE(url.is_empty());
    EXPECT_TRUE(url.is_valid());
    EXPECT_TRUE(url.SchemeIs("https"));
    EXPECT_EQ("clients2.google.com", url.host());
    EXPECT_EQ("/service/update2/crx", url.path());

    // Validate the extension request parameters in the query. It should
    // look something like "x=id%3D<id>%26v%3D<version>%26uc".
    EXPECT_TRUE(url.has_query());
    std::map<std::string, std::string> params;
    VerifyQueryAndExtractParameters(url.query(), &params);
    EXPECT_EQ("com.google.crx.blacklist", params["id"]);
    EXPECT_EQ("0", params["v"]);
    EXPECT_EQ("", params["uc"]);
    EXPECT_TRUE(ContainsKey(params, "ping"));
  }

  void TestUpdateUrlDataEmpty() {
    const std::string id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const std::string version = "1.0";

    // Make sure that an empty update URL data string does not cause a ap=
    // option to appear in the x= parameter.
    ManifestFetchData fetch_data(GURL("http://localhost/foo"), 0);
    fetch_data.AddExtension(
        id, version, &kNeverPingedData, std::string(), std::string());

    std::map<std::string, std::string> params;
    VerifyQueryAndExtractParameters(fetch_data.full_url().query(), &params);
    EXPECT_EQ(id, params["id"]);
    EXPECT_EQ(version, params["v"]);
    EXPECT_EQ(0U, params.count("ap"));
  }

  void TestUpdateUrlDataSimple() {
    const std::string id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const std::string version = "1.0";

    // Make sure that an update URL data string causes an appropriate ap=
    // option to appear in the x= parameter.
    ManifestFetchData fetch_data(GURL("http://localhost/foo"), 0);
    fetch_data.AddExtension(
        id, version, &kNeverPingedData, "bar", std::string());
    std::map<std::string, std::string> params;
    VerifyQueryAndExtractParameters(fetch_data.full_url().query(), &params);
    EXPECT_EQ(id, params["id"]);
    EXPECT_EQ(version, params["v"]);
    EXPECT_EQ("bar", params["ap"]);
  }

  void TestUpdateUrlDataCompound() {
    const std::string id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const std::string version = "1.0";

    // Make sure that an update URL data string causes an appropriate ap=
    // option to appear in the x= parameter.
    ManifestFetchData fetch_data(GURL("http://localhost/foo"), 0);
    fetch_data.AddExtension(
        id, version, &kNeverPingedData, "a=1&b=2&c", std::string());
    std::map<std::string, std::string> params;
    VerifyQueryAndExtractParameters(fetch_data.full_url().query(), &params);
    EXPECT_EQ(id, params["id"]);
    EXPECT_EQ(version, params["v"]);
    EXPECT_EQ("a%3D1%26b%3D2%26c", params["ap"]);
  }

  void TestUpdateUrlDataFromGallery(const std::string& gallery_url) {
    net::TestURLFetcherFactory factory;

    MockService service(prefs_.get());
    MockExtensionDownloaderDelegate delegate;
    ExtensionDownloader downloader(&delegate, service.request_context());
    ExtensionList extensions;
    std::string url(gallery_url);

    service.CreateTestExtensions(1, 1, &extensions, &url, Manifest::INTERNAL);

    const std::string& id = extensions[0]->id();
    EXPECT_CALL(delegate, GetPingDataForExtension(id, _));

    downloader.AddExtension(*extensions[0].get(), 0);
    downloader.StartAllPending();
    net::TestURLFetcher* fetcher =
        factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
    ASSERT_TRUE(fetcher);
    // Make sure that extensions that update from the gallery ignore any
    // update URL data.
    const std::string& update_url = fetcher->GetOriginalURL().spec();
    std::string::size_type x = update_url.find("x=");
    EXPECT_NE(std::string::npos, x);
    std::string::size_type ap = update_url.find("ap%3D", x);
    EXPECT_EQ(std::string::npos, ap);
  }

  void TestInstallSource() {
    const std::string id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const std::string version = "1.0";
    const std::string install_source = "instally";

    // Make sure that an installsource= appears in the x= parameter.
    ManifestFetchData fetch_data(GURL("http://localhost/foo"), 0);
    fetch_data.AddExtension(id, version, &kNeverPingedData,
                            kEmptyUpdateUrlData, install_source);
    std::map<std::string, std::string> params;
    VerifyQueryAndExtractParameters(fetch_data.full_url().query(), &params);
    EXPECT_EQ(id, params["id"]);
    EXPECT_EQ(version, params["v"]);
    EXPECT_EQ(install_source, params["installsource"]);
  }

  void TestDetermineUpdates() {
    TestingProfile profile;
    MockExtensionDownloaderDelegate delegate;
    ExtensionDownloader downloader(&delegate, profile.GetRequestContext());

    // Check passing an empty list of parse results to DetermineUpdates
    ManifestFetchData fetch_data(GURL("http://localhost/foo"), 0);
    UpdateManifest::Results updates;
    std::vector<int> updateable;
    downloader.DetermineUpdates(fetch_data, updates, &updateable);
    EXPECT_TRUE(updateable.empty());

    // Create two updates - expect that DetermineUpdates will return the first
    // one (v1.0 installed, v1.1 available) but not the second one (both
    // installed and available at v2.0).
    const std::string id1 = id_util::GenerateId("1");
    const std::string id2 = id_util::GenerateId("2");
    fetch_data.AddExtension(
        id1, "1.0.0.0", &kNeverPingedData, kEmptyUpdateUrlData, std::string());
    AddParseResult(id1, "1.1", "http://localhost/e1_1.1.crx", &updates);
    fetch_data.AddExtension(
        id2, "2.0.0.0", &kNeverPingedData, kEmptyUpdateUrlData, std::string());
    AddParseResult(id2, "2.0.0.0", "http://localhost/e2_2.0.crx", &updates);

    EXPECT_CALL(delegate, IsExtensionPending(_)).WillRepeatedly(Return(false));
    EXPECT_CALL(delegate, GetExtensionExistingVersion(id1, _))
        .WillOnce(DoAll(SetArgPointee<1>("1.0.0.0"),
                        Return(true)));
    EXPECT_CALL(delegate, GetExtensionExistingVersion(id2, _))
        .WillOnce(DoAll(SetArgPointee<1>("2.0.0.0"),
                        Return(true)));

    downloader.DetermineUpdates(fetch_data, updates, &updateable);
    EXPECT_EQ(1u, updateable.size());
    EXPECT_EQ(0, updateable[0]);
  }

  void TestDetermineUpdatesPending() {
    // Create a set of test extensions
    ServiceForManifestTests service(prefs_.get());
    PendingExtensionManager* pending_extension_manager =
        service.pending_extension_manager();
    SetupPendingExtensionManagerForTest(3, GURL(), pending_extension_manager);

    TestingProfile profile;
    MockExtensionDownloaderDelegate delegate;
    ExtensionDownloader downloader(&delegate, profile.GetRequestContext());

    ManifestFetchData fetch_data(GURL("http://localhost/foo"), 0);
    UpdateManifest::Results updates;

    std::list<std::string> ids_for_update_check;
    pending_extension_manager->GetPendingIdsForUpdateCheck(
        &ids_for_update_check);

    std::list<std::string>::const_iterator it;
    for (it = ids_for_update_check.begin();
         it != ids_for_update_check.end(); ++it) {
      fetch_data.AddExtension(*it,
                              "1.0.0.0",
                              &kNeverPingedData,
                              kEmptyUpdateUrlData,
                              std::string());
      AddParseResult(*it, "1.1", "http://localhost/e1_1.1.crx", &updates);
    }

    // The delegate will tell the downloader that all the extensions are
    // pending.
    EXPECT_CALL(delegate, IsExtensionPending(_)).WillRepeatedly(Return(true));

    std::vector<int> updateable;
    downloader.DetermineUpdates(fetch_data, updates, &updateable);
    // All the apps should be updateable.
    EXPECT_EQ(3u, updateable.size());
    for (std::vector<int>::size_type i = 0; i < updateable.size(); ++i) {
      EXPECT_EQ(static_cast<int>(i), updateable[i]);
    }
  }

  void TestMultipleManifestDownloading() {
    net::TestURLFetcherFactory factory;
    net::TestURLFetcher* fetcher = NULL;
    NotificationsObserver observer;
    MockService service(prefs_.get());
    MockExtensionDownloaderDelegate delegate;
    ExtensionDownloader downloader(&delegate, service.request_context());
    downloader.manifests_queue_.set_backoff_policy(&kNoBackoffPolicy);

    GURL kUpdateUrl("http://localhost/manifest1");

    scoped_ptr<ManifestFetchData> fetch1(new ManifestFetchData(kUpdateUrl, 0));
    scoped_ptr<ManifestFetchData> fetch2(new ManifestFetchData(kUpdateUrl, 0));
    scoped_ptr<ManifestFetchData> fetch3(new ManifestFetchData(kUpdateUrl, 0));
    scoped_ptr<ManifestFetchData> fetch4(new ManifestFetchData(kUpdateUrl, 0));
    ManifestFetchData::PingData zeroDays(0, 0, true);
    fetch1->AddExtension(
        "1111", "1.0", &zeroDays, kEmptyUpdateUrlData, std::string());
    fetch2->AddExtension(
        "2222", "2.0", &zeroDays, kEmptyUpdateUrlData, std::string());
    fetch3->AddExtension(
        "3333", "3.0", &zeroDays, kEmptyUpdateUrlData, std::string());
    fetch4->AddExtension(
        "4444", "4.0", &zeroDays, kEmptyUpdateUrlData, std::string());

    // This will start the first fetcher and queue the others. The next in queue
    // is started as each fetcher receives its response.
    downloader.StartUpdateCheck(fetch1.Pass());
    downloader.StartUpdateCheck(fetch2.Pass());
    downloader.StartUpdateCheck(fetch3.Pass());
    downloader.StartUpdateCheck(fetch4.Pass());
    RunUntilIdle();

    // The first fetch will fail.
    fetcher = factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
    EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
    EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);
    EXPECT_CALL(delegate, OnExtensionDownloadFailed(
        "1111", ExtensionDownloaderDelegate::MANIFEST_FETCH_FAILED, _, _));
    fetcher->set_url(kUpdateUrl);
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(400);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    RunUntilIdle();
    Mock::VerifyAndClearExpectations(&delegate);

    // The second fetch gets invalid data.
    const std::string kInvalidXml = "invalid xml";
    fetcher = factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
    EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
    EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);
    EXPECT_CALL(delegate, OnExtensionDownloadFailed(
        "2222", ExtensionDownloaderDelegate::MANIFEST_INVALID, _, _))
        .WillOnce(InvokeWithoutArgs(&delegate,
                                    &MockExtensionDownloaderDelegate::Quit));
    fetcher->set_url(kUpdateUrl);
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(200);
    fetcher->SetResponseString(kInvalidXml);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    delegate.Wait();
    Mock::VerifyAndClearExpectations(&delegate);

    // The third fetcher doesn't have an update available.
    const std::string kNoUpdate =
        "<?xml version='1.0' encoding='UTF-8'?>"
        "<gupdate xmlns='http://www.google.com/update2/response'"
        "                protocol='2.0'>"
        " <app appid='3333'>"
        "  <updatecheck codebase='http://example.com/extension_3.0.0.0.crx'"
        "               version='3.0.0.0' prodversionmin='3.0.0.0' />"
        " </app>"
        "</gupdate>";
    fetcher = factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
    EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
    EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);
    EXPECT_CALL(delegate, IsExtensionPending("3333")).WillOnce(Return(false));
    EXPECT_CALL(delegate, GetExtensionExistingVersion("3333", _))
        .WillOnce(DoAll(SetArgPointee<1>("3.0.0.0"),
                        Return(true)));
    EXPECT_CALL(delegate, OnExtensionDownloadFailed(
        "3333", ExtensionDownloaderDelegate::NO_UPDATE_AVAILABLE, _, _))
        .WillOnce(InvokeWithoutArgs(&delegate,
                                    &MockExtensionDownloaderDelegate::Quit));
    fetcher->set_url(kUpdateUrl);
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(200);
    fetcher->SetResponseString(kNoUpdate);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    delegate.Wait();
    Mock::VerifyAndClearExpectations(&delegate);

    // The last fetcher has an update.
    const std::string kUpdateAvailable =
        "<?xml version='1.0' encoding='UTF-8'?>"
        "<gupdate xmlns='http://www.google.com/update2/response'"
        "                protocol='2.0'>"
        " <app appid='4444'>"
        "  <updatecheck codebase='http://example.com/extension_1.2.3.4.crx'"
        "               version='4.0.42.0' prodversionmin='4.0.42.0' />"
        " </app>"
        "</gupdate>";
    fetcher = factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
    EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
    EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);
    EXPECT_CALL(delegate, IsExtensionPending("4444")).WillOnce(Return(false));
    EXPECT_CALL(delegate, GetExtensionExistingVersion("4444", _))
        .WillOnce(DoAll(SetArgPointee<1>("4.0.0.0"),
                        Return(true)));
    fetcher->set_url(kUpdateUrl);
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(200);
    fetcher->SetResponseString(kUpdateAvailable);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    observer.Wait();
    Mock::VerifyAndClearExpectations(&delegate);

    // Verify that the downloader decided to update this extension.
    EXPECT_EQ(1u, observer.UpdatedCount());
    EXPECT_TRUE(observer.Updated("4444"));
  }

  void TestManifestRetryDownloading() {
    net::TestURLFetcherFactory factory;
    net::TestURLFetcher* fetcher = NULL;
    NotificationsObserver observer;
    MockService service(prefs_.get());
    MockExtensionDownloaderDelegate delegate;
    ExtensionDownloader downloader(&delegate, service.request_context());
    downloader.manifests_queue_.set_backoff_policy(&kNoBackoffPolicy);

    GURL kUpdateUrl("http://localhost/manifest1");

    scoped_ptr<ManifestFetchData> fetch(new ManifestFetchData(kUpdateUrl, 0));
    ManifestFetchData::PingData zeroDays(0, 0, true);
    fetch->AddExtension(
        "1111", "1.0", &zeroDays, kEmptyUpdateUrlData, std::string());

    // This will start the first fetcher.
    downloader.StartUpdateCheck(fetch.Pass());
    RunUntilIdle();

    // ExtensionDownloader should retry kMaxRetries times and then fail.
    EXPECT_CALL(delegate, OnExtensionDownloadFailed(
        "1111", ExtensionDownloaderDelegate::MANIFEST_FETCH_FAILED, _, _));
    for (int i = 0; i <= ExtensionDownloader::kMaxRetries; ++i) {
      // All fetches will fail.
      fetcher = factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
      EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
      EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);
      fetcher->set_url(kUpdateUrl);
      fetcher->set_status(net::URLRequestStatus());
      // Code 5xx causes ExtensionDownloader to retry.
      fetcher->set_response_code(500);
      fetcher->delegate()->OnURLFetchComplete(fetcher);
      RunUntilIdle();
    }
    Mock::VerifyAndClearExpectations(&delegate);


    // For response codes that are not in the 5xx range ExtensionDownloader
    // should not retry.
    fetch.reset(new ManifestFetchData(kUpdateUrl, 0));
    fetch->AddExtension(
        "1111", "1.0", &zeroDays, kEmptyUpdateUrlData, std::string());

    // This will start the first fetcher.
    downloader.StartUpdateCheck(fetch.Pass());
    RunUntilIdle();

    EXPECT_CALL(delegate, OnExtensionDownloadFailed(
        "1111", ExtensionDownloaderDelegate::MANIFEST_FETCH_FAILED, _, _));
    // The first fetch will fail, and require retrying.
    fetcher = factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
    EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
    EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);
    fetcher->set_url(kUpdateUrl);
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(500);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    RunUntilIdle();

    // The second fetch will fail with response 400 and should not cause
    // ExtensionDownloader to retry.
    fetcher = factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
    EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
    EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);
    fetcher->set_url(kUpdateUrl);
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(400);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    RunUntilIdle();

    Mock::VerifyAndClearExpectations(&delegate);
  }

  void TestSingleExtensionDownloading(bool pending, bool retry) {
    net::TestURLFetcherFactory factory;
    net::TestURLFetcher* fetcher = NULL;
    scoped_ptr<ServiceForDownloadTests> service(
        new ServiceForDownloadTests(prefs_.get()));
    ExtensionUpdater updater(service.get(), service->extension_prefs(),
                             service->pref_service(),
                             service->profile(),
                             service->blacklist(),
                             kUpdateFrequencySecs);
    updater.Start();
    ResetDownloader(
        &updater,
        new ExtensionDownloader(&updater, service->request_context()));
    updater.downloader_->extensions_queue_.set_backoff_policy(
        &kNoBackoffPolicy);

    GURL test_url("http://localhost/extension.crx");

    std::string id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    std::string hash;
    Version version("0.0.1");
    std::set<int> requests;
    requests.insert(0);
    scoped_ptr<ExtensionDownloader::ExtensionFetch> fetch(
        new ExtensionDownloader::ExtensionFetch(
            id, test_url, hash, version.GetString(), requests));
    updater.downloader_->FetchUpdatedExtension(fetch.Pass());

    if (pending) {
      const bool kIsFromSync = true;
      const bool kInstallSilently = true;
      PendingExtensionManager* pending_extension_manager =
          service->pending_extension_manager();
      pending_extension_manager->AddForTesting(
          PendingExtensionInfo(id, test_url, version,
                               &ShouldAlwaysInstall, kIsFromSync,
                               kInstallSilently,
                               Manifest::INTERNAL));
    }

    // Call back the ExtensionUpdater with a 200 response and some test data
    base::FilePath extension_file_path(FILE_PATH_LITERAL("/whatever"));
    fetcher = factory.GetFetcherByID(ExtensionDownloader::kExtensionFetcherId);
    EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
    EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);

    if (retry) {
      // Reply with response code 500 to cause ExtensionDownloader to retry
      fetcher->set_url(test_url);
      fetcher->set_status(net::URLRequestStatus());
      fetcher->set_response_code(500);
      fetcher->delegate()->OnURLFetchComplete(fetcher);

      RunUntilIdle();
      fetcher = factory.GetFetcherByID(
          ExtensionDownloader::kExtensionFetcherId);
      EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
      EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);
    }

    fetcher->set_url(test_url);
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(200);
    fetcher->SetResponseFilePath(extension_file_path);
    fetcher->delegate()->OnURLFetchComplete(fetcher);

    RunUntilIdle();

    // Expect that ExtensionUpdater asked the mock extensions service to install
    // a file with the test data for the right id.
    EXPECT_EQ(id, service->extension_id());
    base::FilePath tmpfile_path = service->install_path();
    EXPECT_FALSE(tmpfile_path.empty());
    EXPECT_EQ(test_url, service->download_url());
    EXPECT_EQ(extension_file_path, tmpfile_path);
  }

  void TestBlacklistDownloading() {
    net::TestURLFetcherFactory factory;
    net::TestURLFetcher* fetcher = NULL;
    MockService service(prefs_.get());
    TestBlacklist blacklist(service.blacklist());
    ExtensionUpdater updater(
        &service, service.extension_prefs(), service.pref_service(),
        service.profile(), blacklist.blacklist(), kUpdateFrequencySecs);
    updater.Start();
    ResetDownloader(
        &updater,
        new ExtensionDownloader(&updater, service.request_context()));
    updater.downloader_->extensions_queue_.set_backoff_policy(
        &kNoBackoffPolicy);

    GURL test_url("http://localhost/extension.crx");

    std::string id = "com.google.crx.blacklist";

    std::string hash =
        "CCEA231D3CD30A348DA1383ED311EAC11E82360773CB2BA4E2C3A5FF16E337CC";

    std::string version = "0.0.1";
    std::set<int> requests;
    requests.insert(0);
    scoped_ptr<ExtensionDownloader::ExtensionFetch> fetch(
        new ExtensionDownloader::ExtensionFetch(
            id, test_url, hash, version, requests));
    updater.downloader_->FetchUpdatedExtension(fetch.Pass());

    // Call back the ExtensionUpdater with a 200 response and some test data.
    std::string extension_data("aaaabbbbcccceeeeaaaabbbbcccceeee");
    EXPECT_FALSE(blacklist.IsBlacklisted(extension_data));

    fetcher = factory.GetFetcherByID(ExtensionDownloader::kExtensionFetcherId);
    EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
    EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);

    fetcher->set_url(test_url);
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(200);
    fetcher->SetResponseString(extension_data);
    fetcher->delegate()->OnURLFetchComplete(fetcher);

    RunUntilIdle();

    EXPECT_TRUE(blacklist.IsBlacklisted(extension_data));

    EXPECT_EQ(version, service.pref_service()->
      GetString(prefs::kExtensionBlacklistUpdateVersion));
  }

  // Two extensions are updated.  If |updates_start_running| is true, the
  // mock extensions service has UpdateExtension(...) return true, and
  // the test is responsible for creating fake CrxInstallers.  Otherwise,
  // UpdateExtension() returns false, signaling install failures.
  void TestMultipleExtensionDownloading(bool updates_start_running) {
    net::TestURLFetcherFactory factory;
    net::TestURLFetcher* fetcher = NULL;
    ServiceForDownloadTests service(prefs_.get());
    ExtensionUpdater updater(
        &service, service.extension_prefs(), service.pref_service(),
        service.profile(), service.blacklist(), kUpdateFrequencySecs);
    updater.Start();
    ResetDownloader(
        &updater,
        new ExtensionDownloader(&updater, service.request_context()));
    updater.downloader_->extensions_queue_.set_backoff_policy(
        &kNoBackoffPolicy);

    EXPECT_FALSE(updater.crx_install_is_running_);

    GURL url1("http://localhost/extension1.crx");
    GURL url2("http://localhost/extension2.crx");

    std::string id1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    std::string id2 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    std::string hash1;
    std::string hash2;

    std::string version1 = "0.1";
    std::string version2 = "0.1";
    std::set<int> requests;
    requests.insert(0);
    // Start two fetches
    scoped_ptr<ExtensionDownloader::ExtensionFetch> fetch1(
        new ExtensionDownloader::ExtensionFetch(
            id1, url1, hash1, version1, requests));
    scoped_ptr<ExtensionDownloader::ExtensionFetch> fetch2(
        new ExtensionDownloader::ExtensionFetch(
            id2, url2, hash2, version2, requests));
    updater.downloader_->FetchUpdatedExtension(fetch1.Pass());
    updater.downloader_->FetchUpdatedExtension(fetch2.Pass());

    // Make the first fetch complete.
    base::FilePath extension_file_path(FILE_PATH_LITERAL("/whatever"));

    fetcher = factory.GetFetcherByID(ExtensionDownloader::kExtensionFetcherId);
    EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
    EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);

    // We need some CrxInstallers, and CrxInstallers require a real
    // ExtensionService.  Create one on the testing profile.  Any action
    // the CrxInstallers take is on the testing profile's extension
    // service, not on our mock |service|.  This allows us to fake
    // the CrxInstaller actions we want.
    TestingProfile profile;
    static_cast<TestExtensionSystem*>(
        ExtensionSystem::Get(&profile))->
        CreateExtensionService(
            CommandLine::ForCurrentProcess(),
            base::FilePath(),
            false);
    ExtensionService* extension_service =
        ExtensionSystem::Get(&profile)->extension_service();
    extension_service->set_extensions_enabled(true);
    extension_service->set_show_extensions_prompts(false);

    scoped_refptr<CrxInstaller> fake_crx1(
        CrxInstaller::CreateSilent(extension_service));
    scoped_refptr<CrxInstaller> fake_crx2(
        CrxInstaller::CreateSilent(extension_service));

    if (updates_start_running) {
      // Add fake CrxInstaller to be returned by service.UpdateExtension().
      service.AddFakeCrxInstaller(id1, fake_crx1.get());
      service.AddFakeCrxInstaller(id2, fake_crx2.get());
    } else {
      // If we don't add fake CRX installers, the mock service fakes a failure
      // starting the install.
    }

    fetcher->set_url(url1);
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(200);
    fetcher->SetResponseFilePath(extension_file_path);
    fetcher->delegate()->OnURLFetchComplete(fetcher);

    RunUntilIdle();

    // Expect that the service was asked to do an install with the right data.
    base::FilePath tmpfile_path = service.install_path();
    EXPECT_FALSE(tmpfile_path.empty());
    EXPECT_EQ(id1, service.extension_id());
    EXPECT_EQ(url1, service.download_url());
    RunUntilIdle();

    // Make sure the second fetch finished and asked the service to do an
    // update.
    base::FilePath extension_file_path2(FILE_PATH_LITERAL("/whatever2"));
    fetcher = factory.GetFetcherByID(ExtensionDownloader::kExtensionFetcherId);
    EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
    EXPECT_TRUE(fetcher->GetLoadFlags() == kExpectedLoadFlags);

    fetcher->set_url(url2);
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(200);
    fetcher->SetResponseFilePath(extension_file_path2);
    fetcher->delegate()->OnURLFetchComplete(fetcher);
    RunUntilIdle();

    if (updates_start_running) {
      EXPECT_TRUE(updater.crx_install_is_running_);

      // The second install should not have run, because the first has not
      // sent a notification that it finished.
      EXPECT_EQ(id1, service.extension_id());
      EXPECT_EQ(url1, service.download_url());

      // Fake install notice.  This should start the second installation,
      // which will be checked below.
      fake_crx1->NotifyCrxInstallComplete(false);

      EXPECT_TRUE(updater.crx_install_is_running_);
    }

    EXPECT_EQ(id2, service.extension_id());
    EXPECT_EQ(url2, service.download_url());
    EXPECT_FALSE(service.install_path().empty());

    // Make sure the correct crx contents were passed for the update call.
    EXPECT_EQ(extension_file_path2, service.install_path());

    if (updates_start_running) {
      EXPECT_TRUE(updater.crx_install_is_running_);
      fake_crx2->NotifyCrxInstallComplete(false);
    }
    EXPECT_FALSE(updater.crx_install_is_running_);
  }

  void TestGalleryRequestsWithBrand(bool use_organic_brand_code) {
    google_util::BrandForTesting brand_for_testing(
        use_organic_brand_code ? "GGLS" : "TEST");

    // We want to test a variety of combinations of expected ping conditions for
    // rollcall and active pings.
    int ping_cases[] = { ManifestFetchData::kNeverPinged, 0, 1, 5 };

    for (size_t i = 0; i < arraysize(ping_cases); i++) {
      for (size_t j = 0; j < arraysize(ping_cases); j++) {
        for (size_t k = 0; k < 2; k++) {
          int rollcall_ping_days = ping_cases[i];
          int active_ping_days = ping_cases[j];
          // Skip cases where rollcall_ping_days == -1, but
          // active_ping_days > 0, because rollcall_ping_days == -1 means the
          // app was just installed and this is the first update check after
          // installation.
          if (rollcall_ping_days == ManifestFetchData::kNeverPinged &&
              active_ping_days > 0)
            continue;

          bool active_bit = k > 0;
          TestGalleryRequests(rollcall_ping_days, active_ping_days, active_bit,
                              !use_organic_brand_code);
          ASSERT_FALSE(HasFailure()) <<
            " rollcall_ping_days=" << ping_cases[i] <<
            " active_ping_days=" << ping_cases[j] <<
            " active_bit=" << active_bit;
        }
      }
    }
  }

  // Test requests to both a Google server and a non-google server. This allows
  // us to test various combinations of installed (ie roll call) and active
  // (ie app launch) ping scenarios. The invariant is that each type of ping
  // value should be present at most once per day, and can be calculated based
  // on the delta between now and the last ping time (or in the case of active
  // pings, that delta plus whether the app has been active).
  void TestGalleryRequests(int rollcall_ping_days,
                           int active_ping_days,
                           bool active_bit,
                           bool expect_brand_code) {
    net::TestURLFetcherFactory factory;

    // Set up 2 mock extensions, one with a google.com update url and one
    // without.
    prefs_.reset(new TestExtensionPrefs(base::MessageLoopProxy::current()));
    ServiceForManifestTests service(prefs_.get());
    ExtensionList tmp;
    GURL url1("http://clients2.google.com/service/update2/crx");
    GURL url2("http://www.somewebsite.com");
    service.CreateTestExtensions(1, 1, &tmp, &url1.possibly_invalid_spec(),
                                 Manifest::INTERNAL);
    service.CreateTestExtensions(2, 1, &tmp, &url2.possibly_invalid_spec(),
                                 Manifest::INTERNAL);
    EXPECT_EQ(2u, tmp.size());
    service.set_extensions(tmp);

    ExtensionPrefs* prefs = service.extension_prefs();
    const std::string& id = tmp[0]->id();
    Time now = Time::Now();
    if (rollcall_ping_days == 0) {
      prefs->SetLastPingDay(id, now - TimeDelta::FromSeconds(15));
    } else if (rollcall_ping_days > 0) {
      Time last_ping_day = now -
                           TimeDelta::FromDays(rollcall_ping_days) -
                           TimeDelta::FromSeconds(15);
      prefs->SetLastPingDay(id, last_ping_day);
    }

    // Store a value for the last day we sent an active ping.
    if (active_ping_days == 0) {
      prefs->SetLastActivePingDay(id, now - TimeDelta::FromSeconds(15));
    } else if (active_ping_days > 0) {
      Time last_active_ping_day = now -
                                  TimeDelta::FromDays(active_ping_days) -
                                  TimeDelta::FromSeconds(15);
      prefs->SetLastActivePingDay(id, last_active_ping_day);
    }
    if (active_bit)
      prefs->SetActiveBit(id, true);

    ExtensionUpdater updater(
        &service, service.extension_prefs(), service.pref_service(),
        service.profile(), service.blacklist(), kUpdateFrequencySecs);
    ExtensionUpdater::CheckParams params;
    params.check_blacklist = false;
    updater.Start();
    updater.CheckNow(params);

    // Make the updater do manifest fetching, and note the urls it tries to
    // fetch.
    std::vector<GURL> fetched_urls;
    net::TestURLFetcher* fetcher =
      factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
    EXPECT_TRUE(fetcher != NULL && fetcher->delegate() != NULL);
    fetched_urls.push_back(fetcher->GetOriginalURL());

    fetcher->set_url(fetched_urls[0]);
    fetcher->set_status(net::URLRequestStatus());
    fetcher->set_response_code(500);
    fetcher->SetResponseString(std::string());
    fetcher->delegate()->OnURLFetchComplete(fetcher);

    fetcher = factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
    fetched_urls.push_back(fetcher->GetOriginalURL());

    // The urls could have been fetched in either order, so use the host to
    // tell them apart and note the query each used.
    std::string url1_query;
    std::string url2_query;
    if (fetched_urls[0].host() == url1.host()) {
      url1_query = fetched_urls[0].query();
      url2_query = fetched_urls[1].query();
    } else if (fetched_urls[0].host() == url2.host()) {
      url1_query = fetched_urls[1].query();
      url2_query = fetched_urls[0].query();
    } else {
      NOTREACHED();
    }

    // First make sure the non-google query had no ping parameter.
    std::string search_string = "ping%3D";
    EXPECT_TRUE(url2_query.find(search_string) == std::string::npos);

    // Now make sure the google query had the correct ping parameter.
    bool ping_expected = false;
    bool did_rollcall = false;
    if (rollcall_ping_days != 0) {
      search_string += "r%253D" + base::IntToString(rollcall_ping_days);
      did_rollcall = true;
      ping_expected = true;
    }
    if (active_bit && active_ping_days != 0) {
      if (did_rollcall)
        search_string += "%2526";
      search_string += "a%253D" + base::IntToString(active_ping_days);
      ping_expected = true;
    }
    bool ping_found = url1_query.find(search_string) != std::string::npos;
    EXPECT_EQ(ping_expected, ping_found) << "query was: " << url1_query
        << " was looking for " << search_string;

    // Make sure the non-google query has no brand parameter.
    const std::string brand_string = "brand%3D";
    EXPECT_TRUE(url2_query.find(brand_string) == std::string::npos);

#if defined(GOOGLE_CHROME_BUILD)
    // Make sure the google query has a brand parameter, but only if the
    // brand is non-organic.
    if (expect_brand_code) {
      EXPECT_TRUE(url1_query.find(brand_string) != std::string::npos);
    } else {
      EXPECT_TRUE(url1_query.find(brand_string) == std::string::npos);
    }
#else
    // Chromium builds never add the brand to the parameter, even for google
    // queries.
    EXPECT_TRUE(url1_query.find(brand_string) == std::string::npos);
#endif

    RunUntilIdle();
  }

  // This makes sure that the extension updater properly stores the results
  // of a <daystart> tag from a manifest fetch in one of two cases: 1) This is
  // the first time we fetched the extension, or 2) We sent a ping value of
  // >= 1 day for the extension.
  void TestHandleManifestResults() {
    ServiceForManifestTests service(prefs_.get());
    GURL update_url("http://www.google.com/manifest");
    ExtensionList tmp;
    service.CreateTestExtensions(1, 1, &tmp, &update_url.spec(),
                                 Manifest::INTERNAL);
    service.set_extensions(tmp);

    ExtensionUpdater updater(
        &service, service.extension_prefs(), service.pref_service(),
        service.profile(), service.blacklist(), kUpdateFrequencySecs);
    updater.Start();
    ResetDownloader(
        &updater,
        new ExtensionDownloader(&updater, service.request_context()));

    ManifestFetchData fetch_data(update_url, 0);
    const Extension* extension = tmp[0].get();
    fetch_data.AddExtension(extension->id(),
                            extension->VersionString(),
                            &kNeverPingedData,
                            kEmptyUpdateUrlData,
                            std::string());
    UpdateManifest::Results results;
    results.daystart_elapsed_seconds = 750;

    updater.downloader_->HandleManifestResults(fetch_data, &results);
    Time last_ping_day =
        service.extension_prefs()->LastPingDay(extension->id());
    EXPECT_FALSE(last_ping_day.is_null());
    int64 seconds_diff = (Time::Now() - last_ping_day).InSeconds();
    EXPECT_LT(seconds_diff - results.daystart_elapsed_seconds, 5);
  }

 protected:
  scoped_ptr<TestExtensionPrefs> prefs_;

 private:
  content::TestBrowserThreadBundle thread_bundle_;

#if defined OS_CHROMEOS
  chromeos::ScopedTestDeviceSettingsService test_device_settings_service_;
  chromeos::ScopedTestCrosSettings test_cros_settings_;
  chromeos::ScopedTestUserManager test_user_manager_;
#endif
};

// Because we test some private methods of ExtensionUpdater, it's easier for the
// actual test code to live in ExtenionUpdaterTest methods instead of TEST_F
// subclasses where friendship with ExtenionUpdater is not inherited.

TEST_F(ExtensionUpdaterTest, TestExtensionUpdateCheckRequests) {
  TestExtensionUpdateCheckRequests(false);
}

TEST_F(ExtensionUpdaterTest, TestExtensionUpdateCheckRequestsPending) {
  TestExtensionUpdateCheckRequests(true);
}

TEST_F(ExtensionUpdaterTest, TestBlacklistUpdateCheckRequests) {
  TestBlacklistUpdateCheckRequests();
}

TEST_F(ExtensionUpdaterTest, TestUpdateUrlData) {
  TestUpdateUrlDataEmpty();
  TestUpdateUrlDataSimple();
  TestUpdateUrlDataCompound();
  TestUpdateUrlDataFromGallery(
      extension_urls::GetWebstoreUpdateUrl().spec());
}

TEST_F(ExtensionUpdaterTest, TestInstallSource) {
  TestInstallSource();
}

TEST_F(ExtensionUpdaterTest, TestDetermineUpdates) {
  TestDetermineUpdates();
}

TEST_F(ExtensionUpdaterTest, TestDetermineUpdatesPending) {
  TestDetermineUpdatesPending();
}

TEST_F(ExtensionUpdaterTest, TestMultipleManifestDownloading) {
  TestMultipleManifestDownloading();
}

TEST_F(ExtensionUpdaterTest, TestSingleExtensionDownloading) {
  TestSingleExtensionDownloading(false, false);
}

TEST_F(ExtensionUpdaterTest, TestSingleExtensionDownloadingPending) {
  TestSingleExtensionDownloading(true, false);
}

TEST_F(ExtensionUpdaterTest, TestSingleExtensionDownloadingWithRetry) {
  TestSingleExtensionDownloading(false, true);
}

TEST_F(ExtensionUpdaterTest, TestSingleExtensionDownloadingPendingWithRetry) {
  TestSingleExtensionDownloading(true, true);
}

TEST_F(ExtensionUpdaterTest, TestBlacklistDownloading) {
  TestBlacklistDownloading();
}

TEST_F(ExtensionUpdaterTest, TestMultipleExtensionDownloadingUpdatesFail) {
  TestMultipleExtensionDownloading(false);
}
TEST_F(ExtensionUpdaterTest, TestMultipleExtensionDownloadingUpdatesSucceed) {
  TestMultipleExtensionDownloading(true);
}

TEST_F(ExtensionUpdaterTest, TestManifestRetryDownloading) {
  TestManifestRetryDownloading();
}

TEST_F(ExtensionUpdaterTest, TestGalleryRequestsWithOrganicBrand) {
  TestGalleryRequestsWithBrand(true);
}

TEST_F(ExtensionUpdaterTest, TestGalleryRequestsWithNonOrganicBrand) {
  TestGalleryRequestsWithBrand(false);
}

TEST_F(ExtensionUpdaterTest, TestHandleManifestResults) {
  TestHandleManifestResults();
}

TEST_F(ExtensionUpdaterTest, TestNonAutoUpdateableLocations) {
  net::TestURLFetcherFactory factory;
  ServiceForManifestTests service(prefs_.get());
  ExtensionUpdater updater(&service, service.extension_prefs(),
                           service.pref_service(), service.profile(),
                           service.blacklist(), kUpdateFrequencySecs);
  MockExtensionDownloaderDelegate delegate;
  // Set the downloader directly, so that all its events end up in the mock
  // |delegate|.
  ExtensionDownloader* downloader =
      new ExtensionDownloader(&delegate, service.request_context());
  ResetDownloader(&updater, downloader);

  // Non-internal non-external extensions should be rejected.
  ExtensionList extensions;
  service.CreateTestExtensions(1, 1, &extensions, NULL,
                               Manifest::INVALID_LOCATION);
  service.CreateTestExtensions(2, 1, &extensions, NULL, Manifest::INTERNAL);
  ASSERT_EQ(2u, extensions.size());
  const std::string& updateable_id = extensions[1]->id();

  // These expectations fail if the delegate's methods are invoked for the
  // first extension, which has a non-matching id.
  EXPECT_CALL(delegate, GetUpdateUrlData(updateable_id)).WillOnce(Return(""));
  EXPECT_CALL(delegate, GetPingDataForExtension(updateable_id, _));

  service.set_extensions(extensions);
  ExtensionUpdater::CheckParams params;
  params.check_blacklist = false;
  updater.Start();
  updater.CheckNow(params);
}

TEST_F(ExtensionUpdaterTest, TestUpdatingDisabledExtensions) {
  net::TestURLFetcherFactory factory;
  ServiceForManifestTests service(prefs_.get());
  ExtensionUpdater updater(&service, service.extension_prefs(),
                           service.pref_service(), service.profile(),
                           service.blacklist(), kUpdateFrequencySecs);
  MockExtensionDownloaderDelegate delegate;
  // Set the downloader directly, so that all its events end up in the mock
  // |delegate|.
  ExtensionDownloader* downloader =
      new ExtensionDownloader(&delegate, service.request_context());
  ResetDownloader(&updater, downloader);

  // Non-internal non-external extensions should be rejected.
  ExtensionList enabled_extensions;
  ExtensionList disabled_extensions;
  service.CreateTestExtensions(1, 1, &enabled_extensions, NULL,
      Manifest::INTERNAL);
  service.CreateTestExtensions(2, 1, &disabled_extensions, NULL,
      Manifest::INTERNAL);
  ASSERT_EQ(1u, enabled_extensions.size());
  ASSERT_EQ(1u, disabled_extensions.size());
  const std::string& enabled_id = enabled_extensions[0]->id();
  const std::string& disabled_id = disabled_extensions[0]->id();

  // We expect that both enabled and disabled extensions are auto-updated.
  EXPECT_CALL(delegate, GetUpdateUrlData(enabled_id)).WillOnce(Return(""));
  EXPECT_CALL(delegate, GetPingDataForExtension(enabled_id, _));
  EXPECT_CALL(delegate, GetUpdateUrlData(disabled_id)).WillOnce(Return(""));
  EXPECT_CALL(delegate, GetPingDataForExtension(disabled_id, _));

  service.set_extensions(enabled_extensions);
  service.set_disabled_extensions(disabled_extensions);
  ExtensionUpdater::CheckParams params;
  params.check_blacklist = false;
  updater.Start();
  updater.CheckNow(params);
}

TEST_F(ExtensionUpdaterTest, TestManifestFetchesBuilderAddExtension) {
  net::TestURLFetcherFactory factory;
  MockService service(prefs_.get());
  MockExtensionDownloaderDelegate delegate;
  scoped_ptr<ExtensionDownloader> downloader(
      new ExtensionDownloader(&delegate, service.request_context()));
  EXPECT_EQ(0u, ManifestFetchersCount(downloader.get()));

  // First, verify that adding valid extensions does invoke the callbacks on
  // the delegate.
  std::string id = id_util::GenerateId("foo");
  EXPECT_CALL(delegate, GetPingDataForExtension(id, _)).WillOnce(Return(false));
  EXPECT_TRUE(
      downloader->AddPendingExtension(id, GURL("http://example.com/update"),
                                      0));
  downloader->StartAllPending();
  Mock::VerifyAndClearExpectations(&delegate);
  EXPECT_EQ(1u, ManifestFetchersCount(downloader.get()));

  // Extensions with invalid update URLs should be rejected.
  id = id_util::GenerateId("foo2");
  EXPECT_FALSE(
      downloader->AddPendingExtension(id, GURL("http:google.com:foo"), 0));
  downloader->StartAllPending();
  EXPECT_EQ(1u, ManifestFetchersCount(downloader.get()));

  // Extensions with empty IDs should be rejected.
  EXPECT_FALSE(downloader->AddPendingExtension(std::string(), GURL(), 0));
  downloader->StartAllPending();
  EXPECT_EQ(1u, ManifestFetchersCount(downloader.get()));

  // TODO(akalin): Test that extensions with empty update URLs
  // converted from user scripts are rejected.

  // Reset the ExtensionDownloader so that it drops the current fetcher.
  downloader.reset(
      new ExtensionDownloader(&delegate, service.request_context()));
  EXPECT_EQ(0u, ManifestFetchersCount(downloader.get()));

  // Extensions with empty update URLs should have a default one
  // filled in.
  id = id_util::GenerateId("foo3");
  EXPECT_CALL(delegate, GetPingDataForExtension(id, _)).WillOnce(Return(false));
  EXPECT_TRUE(downloader->AddPendingExtension(id, GURL(), 0));
  downloader->StartAllPending();
  EXPECT_EQ(1u, ManifestFetchersCount(downloader.get()));

  net::TestURLFetcher* fetcher =
      factory.GetFetcherByID(ExtensionDownloader::kManifestFetcherId);
  ASSERT_TRUE(fetcher);
  EXPECT_FALSE(fetcher->GetOriginalURL().is_empty());
}

TEST_F(ExtensionUpdaterTest, TestStartUpdateCheckMemory) {
  net::TestURLFetcherFactory factory;
  MockService service(prefs_.get());
  MockExtensionDownloaderDelegate delegate;
  ExtensionDownloader downloader(&delegate, service.request_context());

  StartUpdateCheck(&downloader, new ManifestFetchData(GURL(), 0));
  // This should delete the newly-created ManifestFetchData.
  StartUpdateCheck(&downloader, new ManifestFetchData(GURL(), 0));
  // This should add into |manifests_pending_|.
  StartUpdateCheck(&downloader, new ManifestFetchData(GURL(
      GURL("http://www.google.com")), 0));
  // The dtor of |downloader| should delete the pending fetchers.
}

TEST_F(ExtensionUpdaterTest, TestCheckSoon) {
  ServiceForManifestTests service(prefs_.get());
  net::TestURLFetcherFactory factory;
  ExtensionUpdater updater(
      &service, service.extension_prefs(), service.pref_service(),
      service.profile(), service.blacklist(), kUpdateFrequencySecs);
  EXPECT_FALSE(updater.WillCheckSoon());
  updater.Start();
  EXPECT_FALSE(updater.WillCheckSoon());
  updater.CheckSoon();
  EXPECT_TRUE(updater.WillCheckSoon());
  updater.CheckSoon();
  EXPECT_TRUE(updater.WillCheckSoon());
  RunUntilIdle();
  EXPECT_FALSE(updater.WillCheckSoon());
  updater.CheckSoon();
  EXPECT_TRUE(updater.WillCheckSoon());
  updater.Stop();
  EXPECT_FALSE(updater.WillCheckSoon());
}

// TODO(asargent) - (http://crbug.com/12780) add tests for:
// -prodversionmin (shouldn't update if browser version too old)
// -manifests & updates arriving out of order / interleaved
// -malformed update url (empty, file://, has query, has a # fragment, etc.)
// -An extension gets uninstalled while updates are in progress (so it doesn't
//  "come back from the dead")
// -An extension gets manually updated to v3 while we're downloading v2 (ie
//  you don't get downgraded accidentally)
// -An update manifest mentions multiple updates

}  // namespace extensions
