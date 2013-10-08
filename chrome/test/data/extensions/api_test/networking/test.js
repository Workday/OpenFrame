// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var callbackPass = chrome.test.callbackPass;
var callbackFail = chrome.test.callbackFail;
var assertTrue = chrome.test.assertTrue;
var assertEq = chrome.test.assertEq;

// Test properties for the verification API.
var verificationProperties = {
  "certificate": "certificate",
  "publicKey": "public_key",
  "nonce": "nonce",
  "signedData": "signed_data",
  "deviceSerial": "device_serial",
  "deviceSsid": "Device 0123",
  "deviceBssid": "00:01:02:03:04:05"
};

var privateHelpers = {
  // Watches for the states |expectedStates| in reverse order. If all states
  // were observed in the right order, succeeds and calls |done|. If any
  // unexpected state is observed, fails.
  watchForStateChanges: function(network, expectedStates, done) {
    var self = this;
    var collectProperties = function(properties) {
      var finishTest = function() {
        chrome.networkingPrivate.onNetworksChanged.removeListener(
            self.onNetworkChange);
        done();
      };
      if (expectedStates.length > 0) {
        var expectedState = expectedStates.pop();
        assertEq(expectedState, properties.ConnectionState);
        if (expectedStates.length == 0)
          finishTest();
      }
    };
    this.onNetworkChange = function(changes) {
      assertEq([network], changes);
      chrome.networkingPrivate.getProperties(
          network,
          callbackPass(collectProperties));
    };
    chrome.networkingPrivate.onNetworksChanged.addListener(
        this.onNetworkChange);
  },
  listListener: function(expected, done) {
    var self = this;
    this.listenForChanges = function(list) {
      assertEq(expected, list);
      chrome.networkingPrivate.onNetworkListChanged.removeListener(
          self.listenForChanges);
      done();
    };
  }
};

var availableTests = [
  function startConnect() {
    chrome.networkingPrivate.startConnect("stub_wifi2", callbackPass());
  },
  function startDisconnect() {
    // Must connect to a network before we can disconnect from it.
    chrome.networkingPrivate.startConnect("stub_wifi2", callbackPass(
      function() {
        chrome.networkingPrivate.startDisconnect("stub_wifi2", callbackPass());
      }));
  },
  function startConnectNonexistent() {
    chrome.networkingPrivate.startConnect(
      "nonexistent_path",
      callbackFail("configure-failed"));
  },
  function startDisconnectNonexistent() {
    chrome.networkingPrivate.startDisconnect(
      "nonexistent_path",
      callbackFail("not-found"));
  },
  function startGetPropertiesNonexistent() {
    chrome.networkingPrivate.getProperties(
      "nonexistent_path",
      callbackFail("Error.DBusFailed"));
  },
  function getVisibleNetworks() {
    chrome.networkingPrivate.getVisibleNetworks(
      "All",
      callbackPass(function(result) {
        assertEq([{
                    "ConnectionState": "Connected",
                    "GUID": "stub_ethernet",
                    "Name": "eth0",
                    "Type": "Ethernet"
                  },
                  {
                    "ConnectionState": "Connected",
                    "GUID": "stub_wifi1",
                    "Name": "wifi1",
                    "Type": "WiFi",
                    "WiFi": {
                      "AutoConnect": false,
                      "Security": "WEP-PSK",
                      "SignalStrength": 0
                    }
                  },
                  {
                    "ConnectionState": "Connected",
                    "GUID": "stub_vpn1",
                    "Name": "vpn1",
                    "Type": "VPN",
                    "VPN": {
                      "AutoConnect": false
                    }
                  },
                  {
                    "ConnectionState": "NotConnected",
                    "GUID": "stub_wifi2",
                    "Name": "wifi2_PSK",
                    "Type": "WiFi",
                    "WiFi": {
                      "AutoConnect": false,
                      "Security": "WPA-PSK",
                      "SignalStrength": 80
                    }
                  },
                  {
                    "Cellular": {
                      "ActivateOverNonCellularNetwork": false,
                      "ActivationState": "not-activated",
                      "NetworkTechnology": "GSM",
                      "RoamingState": "home"
                    },
                    "ConnectionState": "NotConnected",
                    "GUID": "stub_cellular1",
                    "Name": "cellular1",
                    "Type": "Cellular"
                  }], result);
      }));
  },
  function getVisibleNetworksWifi() {
    chrome.networkingPrivate.getVisibleNetworks(
      "WiFi",
      callbackPass(function(result) {
        assertEq([{
                    "ConnectionState": "Connected",
                    "GUID": "stub_wifi1",
                    "Name": "wifi1",
                    "Type": "WiFi",
                    "WiFi": {
                      "AutoConnect": false,
                      "Security": "WEP-PSK",
                      "SignalStrength": 0
                    }
                  },
                  {
                    "ConnectionState": "NotConnected",
                    "GUID": "stub_wifi2",
                    "Name": "wifi2_PSK",
                    "Type": "WiFi",
                    "WiFi": {
                      "AutoConnect": false,
                      "Security": "WPA-PSK",
                      "SignalStrength": 80
                    }
                  }
                  ], result);
      }));
  },
  function requestNetworkScan() {
    // Connected or Connecting networks should be listed first, sorted by type.
    var expected = ["stub_ethernet",
                    "stub_wifi1",
                    "stub_vpn1",
                    "stub_wifi2",
                    "stub_cellular1"];
    var done = chrome.test.callbackAdded();
    var listener = new privateHelpers.listListener(expected, done);
    chrome.networkingPrivate.onNetworkListChanged.addListener(
      listener.listenForChanges);
    chrome.networkingPrivate.requestNetworkScan();
  },
  function getProperties() {
    chrome.networkingPrivate.getProperties(
      "stub_wifi2",
      callbackPass(function(result) {
        assertEq({
                   "ConnectionState": "NotConnected",
                   "GUID": "stub_wifi2",
                   "Name": "wifi2_PSK",
                   "Type": "WiFi",
                   "WiFi": {
                     "Frequency": 5000,
                     "FrequencyList": [2400, 5000],
                     "SSID": "stub_wifi2",
                     "Security": "WPA-PSK",
                     "SignalStrength": 80
                   }
                 }, result);
      }));
  },
  function getManagedProperties() {
    chrome.networkingPrivate.getManagedProperties(
      "stub_wifi2",
      callbackPass(function(result) {
        assertEq({
                   "ConnectionState": {
                     "Active": "NotConnected",
                     "Effective": "Unmanaged"
                   },
                   "GUID": "stub_wifi2",
                   "Name": {
                     "Active": "wifi2_PSK",
                     "Effective": "UserPolicy",
                     "UserPolicy": "My WiFi Network"
                   },
                   "Type": {
                     "Active": "WiFi",
                     "Effective": "UserPolicy",
                     "UserPolicy": "WiFi"
                   },
                   "WiFi": {
                     "AutoConnect": {
                       "Active": false,
                       "UserEditable": true
                     },
                     "Frequency" : {
                       "Active": 5000,
                       "Effective": "Unmanaged"
                     },
                     "FrequencyList" : {
                       "Active": [2400, 5000],
                       "Effective": "Unmanaged"
                     },
                     "Passphrase": {
                       "Effective": "UserSetting",
                       "UserEditable": true,
                       "UserSetting": "FAKE_CREDENTIAL_VPaJDV9x"
                     },
                     "SSID": {
                       "Active": "stub_wifi2",
                       "Effective": "UserPolicy",
                       "UserPolicy": "stub_wifi2"
                     },
                     "Security": {
                       "Active": "WPA-PSK",
                       "Effective": "UserPolicy",
                       "UserPolicy": "WPA-PSK"
                     },
                     "SignalStrength": {
                       "Active": 80,
                       "Effective": "Unmanaged"
                     }
                   }
                 }, result);
      }));
  },
  function setProperties() {
    var done = chrome.test.callbackAdded();
    chrome.networkingPrivate.getProperties(
      "stub_wifi2",
      callbackPass(function(result) {
        result.WiFi.Security = "WEP-PSK";
        chrome.networkingPrivate.setProperties("stub_wifi2", result,
          callbackPass(function() {
            chrome.networkingPrivate.getProperties(
              "stub_wifi2",
              callbackPass(function(result) {
                assertEq("WEP-PSK", result.WiFi.Security);
                done();
              }));
          }));
      }));
  },
  function getState() {
    chrome.networkingPrivate.getState(
      "stub_wifi2",
      callbackPass(function(result) {
        assertEq({
          "ConnectionState": "NotConnected",
          "GUID": "stub_wifi2",
          "Name": "wifi2_PSK",
          "Type": "WiFi",
          "WiFi": {
            "AutoConnect": false,
            "Security": "WPA-PSK",
            "SignalStrength": 80
          }
        }, result);
      }));
  },
  function getStateNonExistent() {
    chrome.networkingPrivate.getState(
      'non_existent',
      callbackFail('Error.InvalidParameter'));
  },
  function onNetworksChangedEventConnect() {
    var network = "stub_wifi2";
    var done = chrome.test.callbackAdded();
    var expectedStates = ["Connected"];
    var listener =
        new privateHelpers.watchForStateChanges(network, expectedStates, done);
    chrome.networkingPrivate.startConnect(network, callbackPass());
  },
  function onNetworksChangedEventDisconnect() {
    var network = "stub_wifi1";
    var done = chrome.test.callbackAdded();
    var expectedStates = ["NotConnected"];
    var listener =
        new privateHelpers.watchForStateChanges(network, expectedStates, done);
    chrome.networkingPrivate.startDisconnect(network, callbackPass());
  },
  function onNetworkListChangedEvent() {
    // Connecting to wifi2 should set wifi1 to offline. Connected or Connecting
    // networks should be listed first, sorted by type.
    var expected = ["stub_ethernet",
                    "stub_wifi2",
                    "stub_vpn1",
                    "stub_wifi1",
                    "stub_cellular1"];
    var done = chrome.test.callbackAdded();
    var listener = new privateHelpers.listListener(expected, done);
    chrome.networkingPrivate.onNetworkListChanged.addListener(
      listener.listenForChanges);
    var network = "stub_wifi2";
    chrome.networkingPrivate.startConnect(network, callbackPass());
  },
  function verifyDestination() {
    chrome.networkingPrivate.verifyDestination(
      verificationProperties,
      callbackPass(function(isValid) {
        assertTrue(isValid);
      }));
  },
  function verifyAndEncryptCredentials() {
    chrome.networkingPrivate.verifyAndEncryptCredentials(
      verificationProperties,
      "guid",
      callbackPass(function(result) {
        assertEq("encrypted_credentials", result);
      }));
  },
  function verifyAndEncryptData() {
    chrome.networkingPrivate.verifyAndEncryptData(
      verificationProperties,
      "data",
      callbackPass(function(result) {
        assertEq("encrypted_data", result);
      }));
  }
];

var testToRun = window.location.search.substring(1);
chrome.test.runTests(availableTests.filter(function(op) {
  return op.name == testToRun;
}));
