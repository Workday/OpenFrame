// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pk11pub.h>

#include <map>
#include <string>
#include <vector>

#include "base/at_exit.h"
#include "base/callback.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/lazy_instance.h"
#include "base/path_service.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/cros/network_library_impl_stub.h"
#include "chrome/browser/chromeos/enrollment_dialog_view.h"
#include "chrome/browser/chromeos/login/mock_user_manager.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/google_apis/test_util.h"
#include "chrome/common/chrome_paths.h"
#include "chromeos/network/onc/onc_certificate_importer_impl.h"
#include "chromeos/network/onc/onc_constants.h"
#include "chromeos/network/onc/onc_test_utils.h"
#include "chromeos/network/onc/onc_utils.h"
#include "crypto/nss_util.h"
#include "net/base/crypto_module.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::AnyNumber;
using ::testing::Return;

namespace chromeos {

namespace {

// Have to do a stub here because MOCK can't handle closure arguments.
class StubEnrollmentDelegate : public EnrollmentDelegate {
 public:
  explicit StubEnrollmentDelegate()
      : did_enroll(false),
        correct_args(false) {}

  virtual bool Enroll(const std::vector<std::string>& uri_list,
                      const base::Closure& closure) OVERRIDE {
    std::vector<std::string> expected_uri_list;
    expected_uri_list.push_back("http://youtu.be/dQw4w9WgXcQ");
    expected_uri_list.push_back("chrome-extension://abc/keygen-cert.html");
    if (uri_list == expected_uri_list)
      correct_args = true;

    did_enroll = true;
    closure.Run();
    return true;
  }

  bool did_enroll;
  bool correct_args;
};

void WifiNetworkConnectCallback(NetworkLibrary* cros, WifiNetwork* wifi) {
  cros->ConnectToWifiNetwork(wifi, false);
}

void VirtualNetworkConnectCallback(NetworkLibrary* cros, VirtualNetwork* vpn) {
  cros->ConnectToVirtualNetwork(vpn);
}

}  // namespace

TEST(NetworkLibraryTest, DecodeNonAsciiSSID) {
  // Sets network name.
  {
    std::string wifi_setname = "SSID TEST";
    std::string wifi_setname_result = "SSID TEST";
    WifiNetwork* wifi = new WifiNetwork("fw");
    wifi->SetName(wifi_setname);
    EXPECT_EQ(wifi->name(), wifi_setname_result);
    delete wifi;
  }

  // Truncates invalid UTF-8
  {
    std::string wifi_setname2 = "SSID TEST \x01\xff!";
    std::string wifi_setname2_result = "SSID TEST \xEF\xBF\xBD\xEF\xBF\xBD!";
    WifiNetwork* wifi = new WifiNetwork("fw");
    wifi->SetName(wifi_setname2);
    EXPECT_EQ(wifi->name(), wifi_setname2_result);
    delete wifi;
  }

  // UTF8 SSID
  {
    std::string wifi_utf8 = "UTF-8 \u3042\u3044\u3046";
    std::string wifi_utf8_result = "UTF-8 \xE3\x81\x82\xE3\x81\x84\xE3\x81\x86";
    WifiNetwork* wifi = new WifiNetwork("fw");
    WifiNetwork::TestApi test_wifi(wifi);
    test_wifi.SetSsid(wifi_utf8);
    EXPECT_EQ(wifi->name(), wifi_utf8_result);
    delete wifi;
  }

  // latin1 SSID -> UTF8 SSID
  {
    std::string wifi_latin1 = "latin-1 \xc0\xcb\xcc\xd6\xfb";
    std::string wifi_latin1_result = "latin-1 \u00c0\u00cb\u00cc\u00d6\u00fb";
    WifiNetwork* wifi = new WifiNetwork("fw");
    WifiNetwork::TestApi test_wifi(wifi);
    test_wifi.SetSsid(wifi_latin1);
    EXPECT_EQ(wifi->name(), wifi_latin1_result);
    delete wifi;
  }

  // Hex SSID
  {
    std::string wifi_hex = "5468697320697320484558205353494421";
    std::string wifi_hex_result = "This is HEX SSID!";
    WifiNetwork* wifi = new WifiNetwork("fw");
    WifiNetwork::TestApi test_wifi(wifi);
    test_wifi.SetHexSsid(wifi_hex);
    EXPECT_EQ(wifi->name(), wifi_hex_result);
    delete wifi;
  }
}

// Create a stub libcros for testing NetworkLibrary functionality through
// NetworkLibraryStubImpl.
// NOTE: It would be of little value to test stub functions that simply return
// predefined values, e.g. ethernet_available(). However, many other functions
// such as connected_network() return values which are set indirectly and thus
// we can test the logic of those setters.

class NetworkLibraryStubTest : public ::testing::Test {
 public:
  NetworkLibraryStubTest() : cros_(NULL) {}

 protected:
  virtual void SetUp() {
    cros_ = static_cast<NetworkLibraryImplStub*>(NetworkLibrary::Get());
    ASSERT_TRUE(cros_) << "GetNetworkLibrary() Failed!";
  }

  virtual void TearDown() {
    cros_ = NULL;
  }

  // Load the ONC from |onc_file| using NetworkLibrary::LoadOncNetworks. Check
  // that return value matches |expect_successful_import| and the configuration
  // that would be sent to Shill matches |shill_json|.
  void LoadOncAndVerifyNetworks(std::string onc_file,
                                std::string shill_json,
                                onc::ONCSource source,
                                bool expect_successful_import) {
    MockUserManager* mock_user_manager =
        new ::testing::StrictMock<MockUserManager>;
    // Takes ownership of |mock_user_manager|.
    ScopedUserManagerEnabler user_manager_enabler(mock_user_manager);
    mock_user_manager->SetActiveUser("madmax@my.domain.com");
    EXPECT_CALL(*mock_user_manager, IsUserLoggedIn())
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_user_manager, Shutdown());

    scoped_ptr<base::DictionaryValue> onc_blob =
        onc::test_utils::ReadTestDictionary(onc_file);
    base::ListValue* onc_network_configs;
    onc_blob->GetListWithoutPathExpansion(
        onc::toplevel_config::kNetworkConfigurations,
        &onc_network_configs);
    ASSERT_TRUE(onc_network_configs);

    scoped_ptr<base::Value> expected_value =
        google_apis::test_util::LoadJSONFile(shill_json);
    base::DictionaryValue* expected_configs;
    expected_value->GetAsDictionary(&expected_configs);

    cros_->LoadOncNetworks(*onc_network_configs, source);

    const std::map<std::string, base::DictionaryValue*>& configs =
        cros_->GetConfigurations();

    EXPECT_EQ(expected_configs->size(), configs.size());

    for (base::DictionaryValue::Iterator it(*expected_configs); !it.IsAtEnd();
         it.Advance()) {
      const base::DictionaryValue* expected_config;
      it.value().GetAsDictionary(&expected_config);

      std::map<std::string, base::DictionaryValue*>::const_iterator entry =
          configs.find(it.key());
      EXPECT_NE(entry, configs.end());
      base::DictionaryValue* actual_config = entry->second;
      EXPECT_TRUE(onc::test_utils::Equals(expected_config, actual_config));
    }
  }

  ScopedStubNetworkLibraryEnabler cros_stub_;
  NetworkLibraryImplStub* cros_;

 protected:
  scoped_refptr<net::CryptoModule> slot_;
  crypto::ScopedTestNSSDB test_nssdb_;
};

// Default stub state:
// vpn1: disconnected, L2TP/IPsec + PSK
// vpn2: disconnected, L2TP/IPsec + user cert
// vpn3: disconnected, OpenVpn
// eth1: connected (active network)
// wifi1: connected
// wifi2: disconnected
// wifi3: disconnected, WEP
// wifi4: disconnected, 8021x
// wifi5: disconnected
// wifi6: disconnected
// cellular1: connected, activated, not roaming
// cellular2: disconnected, activated, roaming

TEST_F(NetworkLibraryStubTest, NetworkLibraryAccessors) {
  // Set up state.
  // Set wifi2->connecting for these tests.
  WifiNetwork* wifi2 = cros_->FindWifiNetworkByPath("wifi2");
  ASSERT_NE(static_cast<const Network*>(NULL), wifi2);
  Network::TestApi test_wifi2(wifi2);
  test_wifi2.SetConnecting();
  // Set cellular1->connecting for these tests.
  CellularNetwork* cellular1 = cros_->FindCellularNetworkByPath("cellular1");
  ASSERT_NE(static_cast<const Network*>(NULL), cellular1);
  Network::TestApi test_cellular1(cellular1);
  test_cellular1.SetConnecting();

  // Ethernet
  ASSERT_NE(static_cast<const EthernetNetwork*>(NULL),
            cros_->ethernet_network());
  EXPECT_EQ("eth1", cros_->ethernet_network()->service_path());
  EXPECT_NE(static_cast<const Network*>(NULL),
            cros_->FindNetworkByPath("eth1"));
  EXPECT_TRUE(cros_->ethernet_connected());
  EXPECT_FALSE(cros_->ethernet_connecting());

  // Wifi
  ASSERT_NE(static_cast<const WifiNetwork*>(NULL), cros_->wifi_network());
  EXPECT_EQ("wifi1", cros_->wifi_network()->service_path());
  EXPECT_NE(static_cast<const WifiNetwork*>(NULL),
            cros_->FindWifiNetworkByPath("wifi1"));
  EXPECT_TRUE(cros_->wifi_connected());
  EXPECT_FALSE(cros_->wifi_connecting());  // Only true for active wifi.
  EXPECT_GE(cros_->wifi_networks().size(), 1U);

  // Cellular
  ASSERT_NE(static_cast<const CellularNetwork*>(NULL),
            cros_->cellular_network());
  EXPECT_EQ("cellular1", cros_->cellular_network()->service_path());
  EXPECT_NE(static_cast<const CellularNetwork*>(NULL),
            cros_->FindCellularNetworkByPath("cellular1"));
  EXPECT_FALSE(cros_->cellular_connected());
  EXPECT_TRUE(cros_->cellular_connecting());
  EXPECT_GE(cros_->cellular_networks().size(), 1U);

  // VPN
  ASSERT_EQ(static_cast<const VirtualNetwork*>(NULL), cros_->virtual_network());
  EXPECT_GE(cros_->virtual_networks().size(), 1U);

  // Active network and global state
  EXPECT_TRUE(cros_->Connected());
  ASSERT_NE(static_cast<const Network*>(NULL), cros_->active_network());
  EXPECT_EQ("eth1", cros_->active_network()->service_path());
  ASSERT_NE(static_cast<const Network*>(NULL), cros_->connected_network());
  EXPECT_EQ("eth1", cros_->connected_network()->service_path());
  // The "wifi1" is connected, so we do not return "wifi2" for the connecting
  // network. There is no conencted cellular network, so "cellular1" is
  // returned by connecting_network().
  EXPECT_TRUE(cros_->Connecting());
  ASSERT_NE(static_cast<const Network*>(NULL), cros_->connecting_network());
  EXPECT_EQ("cellular1", cros_->connecting_network()->service_path());
}

TEST_F(NetworkLibraryStubTest, NetworkConnectWifi) {
  WifiNetwork* wifi1 = cros_->FindWifiNetworkByPath("wifi1");
  ASSERT_NE(static_cast<const WifiNetwork*>(NULL), wifi1);
  EXPECT_TRUE(wifi1->connected());
  cros_->DisconnectFromNetwork(wifi1);
  EXPECT_FALSE(wifi1->connected());
  EXPECT_TRUE(cros_->CanConnectToNetwork(wifi1));
  cros_->ConnectToWifiNetwork(wifi1);
  EXPECT_TRUE(wifi1->connected());
}

TEST_F(NetworkLibraryStubTest, NetworkConnectWifiWithCertPattern) {
  scoped_ptr<base::DictionaryValue> onc_root =
      onc::test_utils::ReadTestDictionary("certificate-client.onc");
  base::ListValue* certificates;
  onc_root->GetListWithoutPathExpansion(onc::toplevel_config::kCertificates,
                                        &certificates);

  onc::CertificateImporterImpl importer;
  ASSERT_TRUE(importer.ImportCertificates(
      *certificates, onc::ONC_SOURCE_USER_IMPORT, NULL));

  WifiNetwork* wifi = cros_->FindWifiNetworkByPath("wifi_cert_pattern");

  StubEnrollmentDelegate* enrollment_delegate = new StubEnrollmentDelegate();
  wifi->SetEnrollmentDelegate(enrollment_delegate);
  EXPECT_FALSE(enrollment_delegate->did_enroll);
  EXPECT_FALSE(enrollment_delegate->correct_args);

  ASSERT_NE(static_cast<const WifiNetwork*>(NULL), wifi);
  EXPECT_FALSE(wifi->connected());
  EXPECT_TRUE(cros_->CanConnectToNetwork(wifi));
  EXPECT_FALSE(wifi->connected());
  wifi->AttemptConnection(
      base::Bind(&WifiNetworkConnectCallback, cros_, wifi));
  EXPECT_TRUE(wifi->connected());
  EXPECT_TRUE(enrollment_delegate->did_enroll);
  EXPECT_TRUE(enrollment_delegate->correct_args);
}

TEST_F(NetworkLibraryStubTest, NetworkConnectVPNWithCertPattern) {
  scoped_ptr<base::DictionaryValue> onc_root =
      onc::test_utils::ReadTestDictionary("certificate-client.onc");
  base::ListValue* certificates;
  onc_root->GetListWithoutPathExpansion(onc::toplevel_config::kCertificates,
                                        &certificates);

  onc::CertificateImporterImpl importer;
  ASSERT_TRUE(importer.ImportCertificates(
      *certificates, onc::ONC_SOURCE_USER_IMPORT, NULL));

  VirtualNetwork* vpn = cros_->FindVirtualNetworkByPath("vpn_cert_pattern");

  StubEnrollmentDelegate* enrollment_delegate = new StubEnrollmentDelegate();
  vpn->SetEnrollmentDelegate(enrollment_delegate);
  EXPECT_FALSE(enrollment_delegate->did_enroll);
  EXPECT_FALSE(enrollment_delegate->correct_args);

  ASSERT_NE(static_cast<const VirtualNetwork*>(NULL), vpn);
  EXPECT_FALSE(vpn->connected());
  EXPECT_TRUE(cros_->CanConnectToNetwork(vpn));
  EXPECT_FALSE(vpn->connected());
  vpn->AttemptConnection(
      base::Bind(&VirtualNetworkConnectCallback, cros_, vpn));
  EXPECT_TRUE(vpn->connected());
  EXPECT_TRUE(enrollment_delegate->did_enroll);
  EXPECT_TRUE(enrollment_delegate->correct_args);
}

TEST_F(NetworkLibraryStubTest, NetworkConnectVPN) {
  VirtualNetwork* vpn1 = cros_->FindVirtualNetworkByPath("vpn1");
  EXPECT_NE(static_cast<const VirtualNetwork*>(NULL), vpn1);
  EXPECT_FALSE(vpn1->connected());
  EXPECT_TRUE(cros_->CanConnectToNetwork(vpn1));
  cros_->ConnectToVirtualNetwork(vpn1);
  EXPECT_TRUE(vpn1->connected());
  ASSERT_NE(static_cast<const VirtualNetwork*>(NULL), cros_->virtual_network());
  EXPECT_EQ("vpn1", cros_->virtual_network()->service_path());
}

namespace {

struct ImportParams {
  // |onc_file|: Filename of source ONC, relative to
  //             chromeos/test/data/network.
  // |shill_file|: Filename of expected Shill config, relative to
  //               chrome/test/data).
  // |onc_source|: The source of the ONC.
  // |expect_import_result|: The expected return value of LoadOncNetworks.
  ImportParams(const std::string& onc_file,
               const std::string& shill_file,
               onc::ONCSource onc_source,
               bool expect_import_result = true)
      : onc_file(onc_file),
        shill_file(shill_file),
        onc_source(onc_source),
        expect_import_result(expect_import_result) {
  }

  std::string onc_file, shill_file;
  onc::ONCSource onc_source;
  bool expect_import_result;
};

::std::ostream& operator<<(::std::ostream& os, const ImportParams& params) {
  return os << "(" << params.onc_file << ", " << params.shill_file << ", "
            << onc::GetSourceAsString(params.onc_source) << ", "
            << (params.expect_import_result ? "valid" : "invalid") << ")";
}

}  // namespace

class LoadOncNetworksTest
    : public NetworkLibraryStubTest,
      public ::testing::WithParamInterface<ImportParams> {
};

TEST_P(LoadOncNetworksTest, VerifyNetworksAndCertificates) {
  LoadOncAndVerifyNetworks(GetParam().onc_file,
                           GetParam().shill_file,
                           GetParam().onc_source,
                           GetParam().expect_import_result);
}

INSTANTIATE_TEST_CASE_P(
    LoadOncNetworksTest,
    LoadOncNetworksTest,
    ::testing::Values(
         ImportParams("managed_toplevel1_with_cert_pems.onc",
                      "chromeos/net/shill_for_managed_toplevel1.json",
                      onc::ONC_SOURCE_USER_POLICY),
         ImportParams("managed_toplevel2_with_cert_pems.onc",
                      "chromeos/net/shill_for_managed_toplevel2.json",
                      onc::ONC_SOURCE_USER_POLICY),
         ImportParams("managed_toplevel_l2tpipsec.onc",
                      "chromeos/net/shill_for_managed_toplevel_l2tpipsec.json",
                      onc::ONC_SOURCE_USER_POLICY),
         ImportParams("managed_toplevel_wifi_peap.onc",
                      "chromeos/net/shill_for_managed_toplevel_wifi_peap.json",
                      onc::ONC_SOURCE_DEVICE_POLICY),
         ImportParams("toplevel_wifi_open.onc",
                      "chromeos/net/shill_for_toplevel_wifi_open.json",
                      onc::ONC_SOURCE_DEVICE_POLICY),
         ImportParams("toplevel_wifi_wep_proxy.onc",
                      "chromeos/net/shill_for_toplevel_wifi_wep_proxy.json",
                      onc::ONC_SOURCE_USER_POLICY),
         ImportParams("toplevel_wifi_wpa_psk.onc",
                      "chromeos/net/shill_for_toplevel_wifi_wpa_psk.json",
                      onc::ONC_SOURCE_USER_POLICY),
         ImportParams("toplevel_wifi_leap.onc",
                      "chromeos/net/shill_for_toplevel_wifi_leap.json",
                      onc::ONC_SOURCE_USER_POLICY),
         ImportParams(
            "toplevel_wifi_eap_clientcert_with_cert_pems.onc",
            "chromeos/net/shill_for_toplevel_wifi_eap_clientcert.json",
            onc::ONC_SOURCE_USER_POLICY),
         ImportParams("toplevel_openvpn_clientcert_with_cert_pems.onc",
                      "chromeos/net/shill_for_toplevel_openvpn_clientcert.json",
                      onc::ONC_SOURCE_USER_POLICY),
         ImportParams("toplevel_wifi_remove.onc",
                      "chromeos/net/shill_for_toplevel_wifi_remove.json",
                      onc::ONC_SOURCE_USER_POLICY)));

// TODO(stevenjb): Test remembered networks.

// TODO(stevenjb): Test network profiles.

// TODO(stevenjb): Test network devices.

// TODO(stevenjb): Test data plans.

// TODO(stevenjb): Test monitor network / device.

}  // namespace chromeos
