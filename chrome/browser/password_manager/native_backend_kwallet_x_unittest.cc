// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/pickle.h"
#include "base/prefs/pref_service.h"
#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/browser/password_manager/native_backend_kwallet_x.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread.h"
#include "dbus/message.h"
#include "dbus/mock_bus.h"
#include "dbus/mock_object_proxy.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;
using testing::_;
using testing::Invoke;
using testing::Return;
using content::PasswordForm;

namespace {

// This class implements a very simple version of KWallet in memory.
// We only provide the parts we actually use; the real version has more.
class TestKWallet {
 public:
  typedef std::basic_string<uint8_t> Blob;  // std::string is binary-safe.

  TestKWallet() : reject_local_folders_(false) {}

  void set_reject_local_folders(bool value) { reject_local_folders_ = value; }

  // NOTE: The method names here are the same as the corresponding DBus
  // methods, and therefore have names that don't match our style guide.

  // Check for presence of a given password folder.
  bool hasFolder(const std::string& folder) const {
    return data_.find(folder) != data_.end();
  }

  // Check for presence of a given password in a given password folder.
  bool hasEntry(const std::string& folder, const std::string& key) const {
    Data::const_iterator it = data_.find(folder);
    return it != data_.end() && it->second.find(key) != it->second.end();
  }

  // Get a list of password keys in a given password folder.
  bool entryList(const std::string& folder,
                 std::vector<std::string>* entries) const {
    Data::const_iterator it = data_.find(folder);
    if (it == data_.end()) return false;
    for (Folder::const_iterator fit = it->second.begin();
         fit != it->second.end(); ++fit)
      entries->push_back(fit->first);
    return true;
  }

  // Read the password data for a given password in a given password folder.
  bool readEntry(const std::string& folder, const std::string& key,
                 Blob* value) const {
    Data::const_iterator it = data_.find(folder);
    if (it == data_.end()) return false;
    Folder::const_iterator fit = it->second.find(key);
    if (fit == it->second.end()) return false;
    *value = fit->second;
    return true;
  }

  // Create the given password folder.
  bool createFolder(const std::string& folder) {
    if (reject_local_folders_ && folder.find('(') != std::string::npos)
      return false;
    return data_.insert(make_pair(folder, Folder())).second;
  }

  // Remove the given password from the given password folder.
  bool removeEntry(const std::string& folder, const std::string& key) {
    Data::iterator it = data_.find(folder);
    if (it == data_.end()) return false;
    return it->second.erase(key) > 0;
  }

  // Write the given password data to the given password folder.
  bool writeEntry(const std::string& folder, const std::string& key,
                  const Blob& value) {
    Data::iterator it = data_.find(folder);
    if (it == data_.end()) return false;
    it->second[key] = value;
    return true;
  }

 private:
  typedef std::map<std::string, Blob> Folder;
  typedef std::map<std::string, Folder> Data;

  Data data_;
  // "Local" folders are folders containing local profile IDs in their names. We
  // can reject attempts to create them in order to make it easier to create
  // legacy shared passwords in these tests, for testing the migration code.
  bool reject_local_folders_;

  // No need to disallow copy and assign. This class is safe to copy and assign.
};

}  // anonymous namespace

// Obscure magic: we need to declare storage for this constant because we use it
// in ways that require its address in this test, but not in the actual code.
const int NativeBackendKWallet::kInvalidKWalletHandle;

// Subclass NativeBackendKWallet to promote some members to public for testing.
class NativeBackendKWalletStub : public NativeBackendKWallet {
 public:
  NativeBackendKWalletStub(LocalProfileId id, PrefService* pref_service)
      :  NativeBackendKWallet(id, pref_service) {
  }
  using NativeBackendKWallet::InitWithBus;
  using NativeBackendKWallet::kInvalidKWalletHandle;
  using NativeBackendKWallet::DeserializeValue;
};

// Provide some test forms to avoid having to set them up in each test.
class NativeBackendKWalletTestBase : public testing::Test {
 protected:
  NativeBackendKWalletTestBase() {
    form_google_.origin = GURL("http://www.google.com/");
    form_google_.action = GURL("http://www.google.com/login");
    form_google_.username_element = UTF8ToUTF16("user");
    form_google_.username_value = UTF8ToUTF16("joeschmoe");
    form_google_.password_element = UTF8ToUTF16("pass");
    form_google_.password_value = UTF8ToUTF16("seekrit");
    form_google_.submit_element = UTF8ToUTF16("submit");
    form_google_.signon_realm = "Google";

    form_isc_.origin = GURL("http://www.isc.org/");
    form_isc_.action = GURL("http://www.isc.org/auth");
    form_isc_.username_element = UTF8ToUTF16("id");
    form_isc_.username_value = UTF8ToUTF16("janedoe");
    form_isc_.password_element = UTF8ToUTF16("passwd");
    form_isc_.password_value = UTF8ToUTF16("ihazabukkit");
    form_isc_.submit_element = UTF8ToUTF16("login");
    form_isc_.signon_realm = "ISC";
  }

  void CheckPasswordForm(const PasswordForm& expected,
                         const PasswordForm& actual);

  PasswordForm form_google_;
  PasswordForm form_isc_;
};

void NativeBackendKWalletTestBase::CheckPasswordForm(
    const PasswordForm& expected, const PasswordForm& actual) {
  EXPECT_EQ(expected.origin, actual.origin);
  EXPECT_EQ(expected.password_value, actual.password_value);
  EXPECT_EQ(expected.action, actual.action);
  EXPECT_EQ(expected.username_element, actual.username_element);
  EXPECT_EQ(expected.username_value, actual.username_value);
  EXPECT_EQ(expected.password_element, actual.password_element);
  EXPECT_EQ(expected.submit_element, actual.submit_element);
  EXPECT_EQ(expected.signon_realm, actual.signon_realm);
  EXPECT_EQ(expected.ssl_valid, actual.ssl_valid);
  EXPECT_EQ(expected.preferred, actual.preferred);
  // We don't check the date created. It varies.
  EXPECT_EQ(expected.blacklisted_by_user, actual.blacklisted_by_user);
  EXPECT_EQ(expected.scheme, actual.scheme);
}

class NativeBackendKWalletTest : public NativeBackendKWalletTestBase {
 protected:
  NativeBackendKWalletTest()
      : ui_thread_(BrowserThread::UI, &message_loop_),
        db_thread_(BrowserThread::DB), klauncher_ret_(0),
        klauncher_contacted_(false), kwallet_runnable_(true),
        kwallet_running_(true), kwallet_enabled_(true) {
  }

  virtual void SetUp();
  virtual void TearDown();

  // Let the DB thread run to completion of all current tasks.
  void RunDBThread() {
    base::WaitableEvent event(false, false);
    BrowserThread::PostTask(BrowserThread::DB, FROM_HERE,
                            base::Bind(ThreadDone, &event));
    event.Wait();
    // Some of the tests may post messages to the UI thread, but we don't need
    // to run those until after the DB thread is finished. So run it here.
    message_loop_.RunUntilIdle();
  }
  static void ThreadDone(base::WaitableEvent* event) {
    event->Signal();
  }

  // Utilities to help verify sets of expectations.
  typedef std::vector<
              std::pair<std::string,
                        std::vector<const PasswordForm*> > > ExpectationArray;
  void CheckPasswordForms(const std::string& folder,
                          const ExpectationArray& sorted_expected);

  base::MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread db_thread_;
  TestingProfile profile_;

  scoped_refptr<dbus::MockBus> mock_session_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_klauncher_proxy_;
  scoped_refptr<dbus::MockObjectProxy> mock_kwallet_proxy_;

  int klauncher_ret_;
  std::string klauncher_error_;
  bool klauncher_contacted_;

  bool kwallet_runnable_;
  bool kwallet_running_;
  bool kwallet_enabled_;

  TestKWallet wallet_;

 private:
  dbus::Response* KLauncherMethodCall(
      dbus::MethodCall* method_call, testing::Unused);

  dbus::Response* KWalletMethodCall(
      dbus::MethodCall* method_call, testing::Unused);
};

void NativeBackendKWalletTest::SetUp() {
  ASSERT_TRUE(db_thread_.Start());

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SESSION;
  mock_session_bus_ = new dbus::MockBus(options);

  mock_klauncher_proxy_ =
      new dbus::MockObjectProxy(mock_session_bus_.get(),
                                "org.kde.klauncher",
                                dbus::ObjectPath("/KLauncher"));
  EXPECT_CALL(*mock_klauncher_proxy_.get(), MockCallMethodAndBlock(_, _))
      .WillRepeatedly(
           Invoke(this, &NativeBackendKWalletTest::KLauncherMethodCall));

  mock_kwallet_proxy_ =
      new dbus::MockObjectProxy(mock_session_bus_.get(),
                                "org.kde.kwalletd",
                                dbus::ObjectPath("/modules/kwalletd"));
  EXPECT_CALL(*mock_kwallet_proxy_.get(), MockCallMethodAndBlock(_, _))
      .WillRepeatedly(
           Invoke(this, &NativeBackendKWalletTest::KWalletMethodCall));

  EXPECT_CALL(
      *mock_session_bus_.get(),
      GetObjectProxy("org.kde.klauncher", dbus::ObjectPath("/KLauncher")))
      .WillRepeatedly(Return(mock_klauncher_proxy_.get()));
  EXPECT_CALL(
      *mock_session_bus_.get(),
      GetObjectProxy("org.kde.kwalletd", dbus::ObjectPath("/modules/kwalletd")))
      .WillRepeatedly(Return(mock_kwallet_proxy_.get()));

  EXPECT_CALL(*mock_session_bus_.get(), ShutdownAndBlock()).WillOnce(Return())
      .WillRepeatedly(Return());
}

void NativeBackendKWalletTest::TearDown() {
  base::MessageLoop::current()->PostTask(FROM_HERE,
                                         base::MessageLoop::QuitClosure());
  base::MessageLoop::current()->Run();
  db_thread_.Stop();
}

dbus::Response* NativeBackendKWalletTest::KLauncherMethodCall(
    dbus::MethodCall* method_call, testing::Unused) {
  EXPECT_EQ("org.kde.KLauncher", method_call->GetInterface());
  EXPECT_EQ("start_service_by_desktop_name", method_call->GetMember());

  klauncher_contacted_ = true;

  dbus::MessageReader reader(method_call);
  std::string service_name;
  std::vector<std::string> urls;
  std::vector<std::string> envs;
  std::string startup_id;
  bool blind = false;

  EXPECT_TRUE(reader.PopString(&service_name));
  EXPECT_TRUE(reader.PopArrayOfStrings(&urls));
  EXPECT_TRUE(reader.PopArrayOfStrings(&envs));
  EXPECT_TRUE(reader.PopString(&startup_id));
  EXPECT_TRUE(reader.PopBool(&blind));

  EXPECT_EQ("kwalletd", service_name);
  EXPECT_TRUE(urls.empty());
  EXPECT_TRUE(envs.empty());
  EXPECT_TRUE(startup_id.empty());
  EXPECT_FALSE(blind);

  if (kwallet_runnable_)
    kwallet_running_ = true;

  scoped_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  writer.AppendInt32(klauncher_ret_);
  writer.AppendString(std::string());  // dbus_name
  writer.AppendString(klauncher_error_);
  writer.AppendInt32(1234);  // pid
  return response.release();
}

dbus::Response* NativeBackendKWalletTest::KWalletMethodCall(
    dbus::MethodCall* method_call, testing::Unused) {
  if (!kwallet_running_)
    return NULL;
  EXPECT_EQ("org.kde.KWallet", method_call->GetInterface());

  scoped_ptr<dbus::Response> response;
  if (method_call->GetMember() == "isEnabled") {
    response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    writer.AppendBool(kwallet_enabled_);
  } else if (method_call->GetMember() == "networkWallet") {
    response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    writer.AppendString("test_wallet");  // Should match |open| below.
  } else if (method_call->GetMember() == "open") {
    dbus::MessageReader reader(method_call);
    std::string wallet_name;
    int64_t wallet_id;
    std::string app_name;
    EXPECT_TRUE(reader.PopString(&wallet_name));
    EXPECT_TRUE(reader.PopInt64(&wallet_id));
    EXPECT_TRUE(reader.PopString(&app_name));
    EXPECT_EQ("test_wallet", wallet_name);  // Should match |networkWallet|.
    response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    writer.AppendInt32(1);  // Can be anything but kInvalidKWalletHandle.
  } else if (method_call->GetMember() == "hasFolder" ||
             method_call->GetMember() == "createFolder") {
    dbus::MessageReader reader(method_call);
    int handle = NativeBackendKWalletStub::kInvalidKWalletHandle;
    std::string folder_name;
    std::string app_name;
    EXPECT_TRUE(reader.PopInt32(&handle));
    EXPECT_TRUE(reader.PopString(&folder_name));
    EXPECT_TRUE(reader.PopString(&app_name));
    EXPECT_NE(NativeBackendKWalletStub::kInvalidKWalletHandle, handle);
    response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    if (method_call->GetMember() == "hasFolder")
      writer.AppendBool(wallet_.hasFolder(folder_name));
    else
      writer.AppendBool(wallet_.createFolder(folder_name));
  } else if (method_call->GetMember() == "hasEntry" ||
             method_call->GetMember() == "removeEntry") {
    dbus::MessageReader reader(method_call);
    int handle = NativeBackendKWalletStub::kInvalidKWalletHandle;
    std::string folder_name;
    std::string key;
    std::string app_name;
    EXPECT_TRUE(reader.PopInt32(&handle));
    EXPECT_TRUE(reader.PopString(&folder_name));
    EXPECT_TRUE(reader.PopString(&key));
    EXPECT_TRUE(reader.PopString(&app_name));
    EXPECT_NE(NativeBackendKWalletStub::kInvalidKWalletHandle, handle);
    response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    if (method_call->GetMember() == "hasEntry")
      writer.AppendBool(wallet_.hasEntry(folder_name, key));
    else
      writer.AppendInt32(wallet_.removeEntry(folder_name, key) ? 0 : 1);
  } else if (method_call->GetMember() == "entryList") {
    dbus::MessageReader reader(method_call);
    int handle = NativeBackendKWalletStub::kInvalidKWalletHandle;
    std::string folder_name;
    std::string app_name;
    EXPECT_TRUE(reader.PopInt32(&handle));
    EXPECT_TRUE(reader.PopString(&folder_name));
    EXPECT_TRUE(reader.PopString(&app_name));
    EXPECT_NE(NativeBackendKWalletStub::kInvalidKWalletHandle, handle);
    std::vector<std::string> entries;
    if (wallet_.entryList(folder_name, &entries)) {
      response = dbus::Response::CreateEmpty();
      dbus::MessageWriter writer(response.get());
      writer.AppendArrayOfStrings(entries);
    }
  } else if (method_call->GetMember() == "readEntry") {
    dbus::MessageReader reader(method_call);
    int handle = NativeBackendKWalletStub::kInvalidKWalletHandle;
    std::string folder_name;
    std::string key;
    std::string app_name;
    EXPECT_TRUE(reader.PopInt32(&handle));
    EXPECT_TRUE(reader.PopString(&folder_name));
    EXPECT_TRUE(reader.PopString(&key));
    EXPECT_TRUE(reader.PopString(&app_name));
    EXPECT_NE(NativeBackendKWalletStub::kInvalidKWalletHandle, handle);
    TestKWallet::Blob value;
    if (wallet_.readEntry(folder_name, key, &value)) {
      response = dbus::Response::CreateEmpty();
      dbus::MessageWriter writer(response.get());
      writer.AppendArrayOfBytes(value.data(), value.size());
    }
  } else if (method_call->GetMember() == "writeEntry") {
    dbus::MessageReader reader(method_call);
    int handle = NativeBackendKWalletStub::kInvalidKWalletHandle;
    std::string folder_name;
    std::string key;
    uint8_t* bytes = NULL;
    size_t length = 0;
    std::string app_name;
    EXPECT_TRUE(reader.PopInt32(&handle));
    EXPECT_TRUE(reader.PopString(&folder_name));
    EXPECT_TRUE(reader.PopString(&key));
    EXPECT_TRUE(reader.PopArrayOfBytes(&bytes, &length));
    EXPECT_TRUE(reader.PopString(&app_name));
    EXPECT_NE(NativeBackendKWalletStub::kInvalidKWalletHandle, handle);
    response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    writer.AppendInt32(
        wallet_.writeEntry(folder_name, key,
                           TestKWallet::Blob(bytes, length)) ? 0 : 1);
  }

  EXPECT_FALSE(response.get() == NULL);
  return response.release();
}

void NativeBackendKWalletTest::CheckPasswordForms(
    const std::string& folder, const ExpectationArray& sorted_expected) {
  EXPECT_TRUE(wallet_.hasFolder(folder));
  std::vector<std::string> entries;
  EXPECT_TRUE(wallet_.entryList(folder, &entries));
  EXPECT_EQ(sorted_expected.size(), entries.size());
  std::sort(entries.begin(), entries.end());
  for (size_t i = 0; i < entries.size() && i < sorted_expected.size(); ++i) {
    EXPECT_EQ(sorted_expected[i].first, entries[i]);
    TestKWallet::Blob value;
    EXPECT_TRUE(wallet_.readEntry(folder, entries[i], &value));
    Pickle pickle(reinterpret_cast<const char*>(value.data()), value.size());
    std::vector<PasswordForm*> forms;
    NativeBackendKWalletStub::DeserializeValue(entries[i], pickle, &forms);
    const std::vector<const PasswordForm*>& expect = sorted_expected[i].second;
    EXPECT_EQ(expect.size(), forms.size());
    for (size_t j = 0; j < forms.size() && j < expect.size(); ++j)
      CheckPasswordForm(*expect[j], *forms[j]);
    STLDeleteElements(&forms);
  }
}

TEST_F(NativeBackendKWalletTest, NotEnabled) {
  NativeBackendKWalletStub kwallet(42, profile_.GetPrefs());
  kwallet_enabled_ = false;
  EXPECT_FALSE(kwallet.InitWithBus(mock_session_bus_));
  EXPECT_FALSE(klauncher_contacted_);
}

TEST_F(NativeBackendKWalletTest, NotRunnable) {
  NativeBackendKWalletStub kwallet(42, profile_.GetPrefs());
  kwallet_runnable_ = false;
  kwallet_running_ = false;
  EXPECT_FALSE(kwallet.InitWithBus(mock_session_bus_));
  EXPECT_TRUE(klauncher_contacted_);
}

TEST_F(NativeBackendKWalletTest, NotRunningOrEnabled) {
  NativeBackendKWalletStub kwallet(42, profile_.GetPrefs());
  kwallet_running_ = false;
  kwallet_enabled_ = false;
  EXPECT_FALSE(kwallet.InitWithBus(mock_session_bus_));
  EXPECT_TRUE(klauncher_contacted_);
}

TEST_F(NativeBackendKWalletTest, NotRunning) {
  NativeBackendKWalletStub kwallet(42, profile_.GetPrefs());
  kwallet_running_ = false;
  EXPECT_TRUE(kwallet.InitWithBus(mock_session_bus_));
  EXPECT_TRUE(klauncher_contacted_);
}

TEST_F(NativeBackendKWalletTest, BasicStartup) {
  NativeBackendKWalletStub kwallet(42, profile_.GetPrefs());
  EXPECT_TRUE(kwallet.InitWithBus(mock_session_bus_));
  EXPECT_FALSE(klauncher_contacted_);
}

TEST_F(NativeBackendKWalletTest, BasicAddLogin) {
  // Pretend that the migration has already taken place.
  profile_.GetPrefs()->SetBoolean(prefs::kPasswordsUseLocalProfileId, true);

  NativeBackendKWalletStub backend(42, profile_.GetPrefs());
  EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
                 base::Unretained(&backend), form_google_));

  RunDBThread();

  EXPECT_FALSE(wallet_.hasFolder("Chrome Form Data"));

  std::vector<const PasswordForm*> forms;
  forms.push_back(&form_google_);
  ExpectationArray expected;
  expected.push_back(make_pair(std::string(form_google_.signon_realm), forms));
  CheckPasswordForms("Chrome Form Data (42)", expected);
}

TEST_F(NativeBackendKWalletTest, BasicListLogins) {
  // Pretend that the migration has already taken place.
  profile_.GetPrefs()->SetBoolean(prefs::kPasswordsUseLocalProfileId, true);

  NativeBackendKWalletStub backend(42, profile_.GetPrefs());
  EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
                 base::Unretained(&backend), form_google_));

  std::vector<PasswordForm*> form_list;
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(
          base::IgnoreResult(&NativeBackendKWalletStub::GetAutofillableLogins),
          base::Unretained(&backend), &form_list));

  RunDBThread();

  // Quick check that we got something back.
  EXPECT_EQ(1u, form_list.size());
  STLDeleteElements(&form_list);

  EXPECT_FALSE(wallet_.hasFolder("Chrome Form Data"));

  std::vector<const PasswordForm*> forms;
  forms.push_back(&form_google_);
  ExpectationArray expected;
  expected.push_back(make_pair(std::string(form_google_.signon_realm), forms));
  CheckPasswordForms("Chrome Form Data (42)", expected);
}

TEST_F(NativeBackendKWalletTest, BasicRemoveLogin) {
  // Pretend that the migration has already taken place.
  profile_.GetPrefs()->SetBoolean(prefs::kPasswordsUseLocalProfileId, true);

  NativeBackendKWalletStub backend(42, profile_.GetPrefs());
  EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
                 base::Unretained(&backend), form_google_));

  RunDBThread();

  EXPECT_FALSE(wallet_.hasFolder("Chrome Form Data"));

  std::vector<const PasswordForm*> forms;
  forms.push_back(&form_google_);
  ExpectationArray expected;
  expected.push_back(make_pair(std::string(form_google_.signon_realm), forms));
  CheckPasswordForms("Chrome Form Data (42)", expected);

  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::RemoveLogin),
                 base::Unretained(&backend), form_google_));

  RunDBThread();

  expected.clear();
  CheckPasswordForms("Chrome Form Data (42)", expected);
}

TEST_F(NativeBackendKWalletTest, RemoveNonexistentLogin) {
  // Pretend that the migration has already taken place.
  profile_.GetPrefs()->SetBoolean(prefs::kPasswordsUseLocalProfileId, true);

  NativeBackendKWalletStub backend(42, profile_.GetPrefs());
  EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

  // First add an unrelated login.
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
                 base::Unretained(&backend), form_google_));

  RunDBThread();

  EXPECT_FALSE(wallet_.hasFolder("Chrome Form Data"));

  std::vector<const PasswordForm*> forms;
  forms.push_back(&form_google_);
  ExpectationArray expected;
  expected.push_back(make_pair(std::string(form_google_.signon_realm), forms));
  CheckPasswordForms("Chrome Form Data (42)", expected);

  // Attempt to remove a login that doesn't exist.
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::RemoveLogin),
                 base::Unretained(&backend), form_isc_));

  // Make sure we can still get the first form back.
  std::vector<PasswordForm*> form_list;
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(
          base::IgnoreResult(&NativeBackendKWalletStub::GetAutofillableLogins),
          base::Unretained(&backend), &form_list));

  RunDBThread();

  // Quick check that we got something back.
  EXPECT_EQ(1u, form_list.size());
  STLDeleteElements(&form_list);

  CheckPasswordForms("Chrome Form Data (42)", expected);
}

TEST_F(NativeBackendKWalletTest, AddDuplicateLogin) {
  // Pretend that the migration has already taken place.
  profile_.GetPrefs()->SetBoolean(prefs::kPasswordsUseLocalProfileId, true);

  NativeBackendKWalletStub backend(42, profile_.GetPrefs());
  EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
                 base::Unretained(&backend), form_google_));
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
                 base::Unretained(&backend), form_google_));

  RunDBThread();

  EXPECT_FALSE(wallet_.hasFolder("Chrome Form Data"));

  std::vector<const PasswordForm*> forms;
  forms.push_back(&form_google_);
  ExpectationArray expected;
  expected.push_back(make_pair(std::string(form_google_.signon_realm), forms));
  CheckPasswordForms("Chrome Form Data (42)", expected);
}

TEST_F(NativeBackendKWalletTest, ListLoginsAppends) {
  // Pretend that the migration has already taken place.
  profile_.GetPrefs()->SetBoolean(prefs::kPasswordsUseLocalProfileId, true);

  NativeBackendKWalletStub backend(42, profile_.GetPrefs());
  EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
                 base::Unretained(&backend), form_google_));

  // Send the same request twice with the same list both times.
  std::vector<PasswordForm*> form_list;
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(
          base::IgnoreResult(&NativeBackendKWalletStub::GetAutofillableLogins),
          base::Unretained(&backend), &form_list));
  BrowserThread::PostTask(
      BrowserThread::DB, FROM_HERE,
      base::Bind(
          base::IgnoreResult(&NativeBackendKWalletStub::GetAutofillableLogins),
          base::Unretained(&backend), &form_list));

  RunDBThread();

  // Quick check that we got two results back.
  EXPECT_EQ(2u, form_list.size());
  STLDeleteElements(&form_list);

  EXPECT_FALSE(wallet_.hasFolder("Chrome Form Data"));

  std::vector<const PasswordForm*> forms;
  forms.push_back(&form_google_);
  ExpectationArray expected;
  expected.push_back(make_pair(std::string(form_google_.signon_realm), forms));
  CheckPasswordForms("Chrome Form Data (42)", expected);
}

// TODO(mdm): add more basic (i.e. non-migration) tests here at some point.
// (For example tests for storing >1 password per realm pickle.)

TEST_F(NativeBackendKWalletTest, DISABLED_MigrateOneLogin) {
  // Reject attempts to migrate so we can populate the store.
  wallet_.set_reject_local_folders(true);

  {
    NativeBackendKWalletStub backend(42, profile_.GetPrefs());
    EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
                   base::Unretained(&backend), form_google_));

    // Make sure we can get the form back even when migration is failing.
    std::vector<PasswordForm*> form_list;
    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(
            base::IgnoreResult(
                &NativeBackendKWalletStub::GetAutofillableLogins),
            base::Unretained(&backend), &form_list));

    RunDBThread();

    // Quick check that we got something back.
    EXPECT_EQ(1u, form_list.size());
    STLDeleteElements(&form_list);
  }

  EXPECT_FALSE(wallet_.hasFolder("Chrome Form Data (42)"));

  std::vector<const PasswordForm*> forms;
  forms.push_back(&form_google_);
  ExpectationArray expected;
  expected.push_back(make_pair(std::string(form_google_.signon_realm), forms));
  CheckPasswordForms("Chrome Form Data", expected);

  // Now allow the migration.
  wallet_.set_reject_local_folders(false);

  {
    NativeBackendKWalletStub backend(42, profile_.GetPrefs());
    EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

    // Trigger the migration by looking something up.
    std::vector<PasswordForm*> form_list;
    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(
            base::IgnoreResult(
                &NativeBackendKWalletStub::GetAutofillableLogins),
            base::Unretained(&backend), &form_list));

    RunDBThread();

    // Quick check that we got something back.
    EXPECT_EQ(1u, form_list.size());
    STLDeleteElements(&form_list);
  }

  CheckPasswordForms("Chrome Form Data", expected);
  CheckPasswordForms("Chrome Form Data (42)", expected);

  // Check that we have set the persistent preference.
  EXPECT_TRUE(
      profile_.GetPrefs()->GetBoolean(prefs::kPasswordsUseLocalProfileId));
}

TEST_F(NativeBackendKWalletTest, DISABLED_MigrateToMultipleProfiles) {
  // Reject attempts to migrate so we can populate the store.
  wallet_.set_reject_local_folders(true);

  {
    NativeBackendKWalletStub backend(42, profile_.GetPrefs());
    EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
                   base::Unretained(&backend), form_google_));

    RunDBThread();
  }

  EXPECT_FALSE(wallet_.hasFolder("Chrome Form Data (42)"));

  std::vector<const PasswordForm*> forms;
  forms.push_back(&form_google_);
  ExpectationArray expected;
  expected.push_back(make_pair(std::string(form_google_.signon_realm), forms));
  CheckPasswordForms("Chrome Form Data", expected);

  // Now allow the migration.
  wallet_.set_reject_local_folders(false);

  {
    NativeBackendKWalletStub backend(42, profile_.GetPrefs());
    EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

    // Trigger the migration by looking something up.
    std::vector<PasswordForm*> form_list;
    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(
            base::IgnoreResult(
                &NativeBackendKWalletStub::GetAutofillableLogins),
            base::Unretained(&backend), &form_list));

    RunDBThread();

    // Quick check that we got something back.
    EXPECT_EQ(1u, form_list.size());
    STLDeleteElements(&form_list);
  }

  CheckPasswordForms("Chrome Form Data", expected);
  CheckPasswordForms("Chrome Form Data (42)", expected);

  // Check that we have set the persistent preference.
  EXPECT_TRUE(
      profile_.GetPrefs()->GetBoolean(prefs::kPasswordsUseLocalProfileId));

  // Normally we'd actually have a different profile. But in the test just reset
  // the profile's persistent pref; we pass in the local profile id anyway.
  profile_.GetPrefs()->SetBoolean(prefs::kPasswordsUseLocalProfileId, false);

  {
    NativeBackendKWalletStub backend(24, profile_.GetPrefs());
    EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

    // Trigger the migration by looking something up.
    std::vector<PasswordForm*> form_list;
    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(
            base::IgnoreResult(
                &NativeBackendKWalletStub::GetAutofillableLogins),
            base::Unretained(&backend), &form_list));

    RunDBThread();

    // Quick check that we got something back.
    EXPECT_EQ(1u, form_list.size());
    STLDeleteElements(&form_list);
  }

  CheckPasswordForms("Chrome Form Data", expected);
  CheckPasswordForms("Chrome Form Data (42)", expected);
  CheckPasswordForms("Chrome Form Data (24)", expected);
}

TEST_F(NativeBackendKWalletTest, DISABLED_NoMigrationWithPrefSet) {
  // Reject attempts to migrate so we can populate the store.
  wallet_.set_reject_local_folders(true);

  {
    NativeBackendKWalletStub backend(42, profile_.GetPrefs());
    EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
                   base::Unretained(&backend), form_google_));

    RunDBThread();
  }

  EXPECT_FALSE(wallet_.hasFolder("Chrome Form Data (42)"));

  std::vector<const PasswordForm*> forms;
  forms.push_back(&form_google_);
  ExpectationArray expected;
  expected.push_back(make_pair(std::string(form_google_.signon_realm), forms));
  CheckPasswordForms("Chrome Form Data", expected);

  // Now allow migration, but also pretend that the it has already taken place.
  wallet_.set_reject_local_folders(false);
  profile_.GetPrefs()->SetBoolean(prefs::kPasswordsUseLocalProfileId, true);

  {
    NativeBackendKWalletStub backend(42, profile_.GetPrefs());
    EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

    // Trigger the migration by adding a new login.
    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
                   base::Unretained(&backend), form_isc_));

    // Look up all logins; we expect only the one we added.
    std::vector<PasswordForm*> form_list;
    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(
            base::IgnoreResult(
                &NativeBackendKWalletStub::GetAutofillableLogins),
            base::Unretained(&backend), &form_list));

    RunDBThread();

    // Quick check that we got the right thing back.
    EXPECT_EQ(1u, form_list.size());
    if (form_list.size() > 0)
      EXPECT_EQ(form_isc_.signon_realm, form_list[0]->signon_realm);
    STLDeleteElements(&form_list);
  }

  CheckPasswordForms("Chrome Form Data", expected);

  forms[0] = &form_isc_;
  expected.clear();
  expected.push_back(make_pair(std::string(form_isc_.signon_realm), forms));
  CheckPasswordForms("Chrome Form Data (42)", expected);
}

TEST_F(NativeBackendKWalletTest, DISABLED_DeleteMigratedPasswordIsIsolated) {
  // Reject attempts to migrate so we can populate the store.
  wallet_.set_reject_local_folders(true);

  {
    NativeBackendKWalletStub backend(42, profile_.GetPrefs());
    EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(
            base::IgnoreResult(&NativeBackendKWalletStub::AddLogin),
            base::Unretained(&backend), form_google_));

    RunDBThread();
  }

  EXPECT_FALSE(wallet_.hasFolder("Chrome Form Data (42)"));

  std::vector<const PasswordForm*> forms;
  forms.push_back(&form_google_);
  ExpectationArray expected;
  expected.push_back(make_pair(std::string(form_google_.signon_realm), forms));
  CheckPasswordForms("Chrome Form Data", expected);

  // Now allow the migration.
  wallet_.set_reject_local_folders(false);

  {
    NativeBackendKWalletStub backend(42, profile_.GetPrefs());
    EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

    // Trigger the migration by looking something up.
    std::vector<PasswordForm*> form_list;
    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(
            base::IgnoreResult(
                &NativeBackendKWalletStub::GetAutofillableLogins),
            base::Unretained(&backend), &form_list));

    RunDBThread();

    // Quick check that we got something back.
    EXPECT_EQ(1u, form_list.size());
    STLDeleteElements(&form_list);
  }

  CheckPasswordForms("Chrome Form Data", expected);
  CheckPasswordForms("Chrome Form Data (42)", expected);

  // Check that we have set the persistent preference.
  EXPECT_TRUE(
      profile_.GetPrefs()->GetBoolean(prefs::kPasswordsUseLocalProfileId));

  // Normally we'd actually have a different profile. But in the test just reset
  // the profile's persistent pref; we pass in the local profile id anyway.
  profile_.GetPrefs()->SetBoolean(prefs::kPasswordsUseLocalProfileId, false);

  {
    NativeBackendKWalletStub backend(24, profile_.GetPrefs());
    EXPECT_TRUE(backend.InitWithBus(mock_session_bus_));

    // Trigger the migration by looking something up.
    std::vector<PasswordForm*> form_list;
    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(
            base::IgnoreResult(
                &NativeBackendKWalletStub::GetAutofillableLogins),
            base::Unretained(&backend), &form_list));

    RunDBThread();

    // Quick check that we got something back.
    EXPECT_EQ(1u, form_list.size());
    STLDeleteElements(&form_list);

    // There should be three passwords now.
    CheckPasswordForms("Chrome Form Data", expected);
    CheckPasswordForms("Chrome Form Data (42)", expected);
    CheckPasswordForms("Chrome Form Data (24)", expected);

    // Now delete the password from this second profile.
    BrowserThread::PostTask(
        BrowserThread::DB, FROM_HERE,
        base::Bind(
            base::IgnoreResult(&NativeBackendKWalletStub::RemoveLogin),
            base::Unretained(&backend), form_google_));

    RunDBThread();

    // The other two copies of the password in different profiles should remain.
    CheckPasswordForms("Chrome Form Data", expected);
    CheckPasswordForms("Chrome Form Data (42)", expected);
    expected.clear();
    CheckPasswordForms("Chrome Form Data (24)", expected);
  }
}

class NativeBackendKWalletPickleTest : public NativeBackendKWalletTestBase {
 protected:
  void CreateVersion0Pickle(bool size_32,
                            const PasswordForm& form,
                            Pickle* pickle);
  void CheckVersion0Pickle(bool size_32, PasswordForm::Scheme scheme);
};

void NativeBackendKWalletPickleTest::CreateVersion0Pickle(
    bool size_32, const PasswordForm& form, Pickle* pickle) {
  const int kPickleVersion0 = 0;
  pickle->WriteInt(kPickleVersion0);
  if (size_32)
    pickle->WriteUInt32(1);  // Size of form list. 32 bits.
  else
    pickle->WriteUInt64(1);  // Size of form list. 64 bits.
  pickle->WriteInt(form.scheme);
  pickle->WriteString(form.origin.spec());
  pickle->WriteString(form.action.spec());
  pickle->WriteString16(form.username_element);
  pickle->WriteString16(form.username_value);
  pickle->WriteString16(form.password_element);
  pickle->WriteString16(form.password_value);
  pickle->WriteString16(form.submit_element);
  pickle->WriteBool(form.ssl_valid);
  pickle->WriteBool(form.preferred);
  pickle->WriteBool(form.blacklisted_by_user);
  pickle->WriteInt64(form.date_created.ToTimeT());
}

void NativeBackendKWalletPickleTest::CheckVersion0Pickle(
    bool size_32, PasswordForm::Scheme scheme) {
  Pickle pickle;
  PasswordForm form = form_google_;
  form.scheme = scheme;
  CreateVersion0Pickle(size_32, form, &pickle);
  std::vector<PasswordForm*> form_list;
  NativeBackendKWalletStub::DeserializeValue(form.signon_realm,
                                             pickle, &form_list);
  EXPECT_EQ(1u, form_list.size());
  if (form_list.size() > 0)
    CheckPasswordForm(form, *form_list[0]);
  STLDeleteElements(&form_list);
}

// We try both SCHEME_HTML and SCHEME_BASIC since the scheme is stored right
// after the size in the pickle, so it's what gets read as part of the count
// when reading 32-bit pickles on 64-bit systems. SCHEME_HTML is 0 (so we'll
// detect errors later) while SCHEME_BASIC is 1 (so we'll detect it then). We
// try both 32-bit and 64-bit pickles since only one will be the "other" size
// for whatever architecture we're running on, but we want to make sure we can
// read all combinations in any event.

TEST_F(NativeBackendKWalletPickleTest, ReadsOld32BitHTMLPickles) {
  CheckVersion0Pickle(true, PasswordForm::SCHEME_HTML);
}

TEST_F(NativeBackendKWalletPickleTest, ReadsOld32BitHTTPPickles) {
  CheckVersion0Pickle(true, PasswordForm::SCHEME_BASIC);
}

TEST_F(NativeBackendKWalletPickleTest, ReadsOld64BitHTMLPickles) {
  CheckVersion0Pickle(false, PasswordForm::SCHEME_HTML);
}

TEST_F(NativeBackendKWalletPickleTest, ReadsOld64BitHTTPPickles) {
  CheckVersion0Pickle(false, PasswordForm::SCHEME_BASIC);
}
