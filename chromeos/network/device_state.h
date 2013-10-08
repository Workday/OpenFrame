// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_NETWORK_DEVICE_STATE_H_
#define CHROMEOS_NETWORK_DEVICE_STATE_H_

#include "base/values.h"
#include "chromeos/network/managed_state.h"
#include "chromeos/network/network_util.h"

namespace chromeos {

// Simple class to provide device state information. Similar to NetworkState;
// see network_state.h for usage guidelines.
class CHROMEOS_EXPORT DeviceState : public ManagedState {
 public:
  typedef std::vector<CellularScanResult> CellularScanResults;

  explicit DeviceState(const std::string& path);
  virtual ~DeviceState();

  // ManagedState overrides
  virtual bool PropertyChanged(const std::string& key,
                               const base::Value& value) OVERRIDE;

  // Accessors
  const std::string& mac_address() const { return mac_address_; }

  // Cellular specific accessors
  const std::string& home_provider_id() const { return home_provider_id_; }
  bool provider_requires_roaming() const { return provider_requires_roaming_; }
  bool support_network_scan() const { return support_network_scan_; }
  bool scanning() const { return scanning_; }
  const std::string& technology_family() const { return technology_family_; }
  const std::string& carrier() const { return carrier_; }
  const std::string& sim_lock_type() const { return sim_lock_type_; }
  uint32 sim_retries_left() const { return sim_retries_left_; }
  bool sim_lock_enabled() const { return sim_lock_enabled_; }
  const std::string& meid() const { return meid_; }
  const std::string& imei() const { return imei_; }
  const std::string& iccid() const { return iccid_; }
  const std::string& mdn() const { return mdn_; }
  const CellularScanResults& scan_results() const { return scan_results_; }
  const DictionaryValue& properties() const { return properties_; }

  // Returns true if the technology family is GSM and sim_present_ is false.
  bool IsSimAbsent() const;

 private:
  // Common Device Properties
  std::string mac_address_;
  // Cellular specific propeties
  std::string home_provider_id_;
  bool provider_requires_roaming_;
  bool support_network_scan_;
  bool scanning_;
  std::string technology_family_;
  std::string carrier_;
  std::string sim_lock_type_;
  uint32 sim_retries_left_;
  bool sim_lock_enabled_;
  bool sim_present_;
  std::string meid_;
  std::string imei_;
  std::string iccid_;
  std::string mdn_;
  CellularScanResults scan_results_;
  // Keep all Device properties in a dictionary. Devices are limited and should
  // change rarely if ever, so the overhead for this is small.
  DictionaryValue properties_;

  DISALLOW_COPY_AND_ASSIGN(DeviceState);
};

}  // namespace chromeos

#endif  // CHROMEOS_NETWORK_DEVICE_STATE_H_
