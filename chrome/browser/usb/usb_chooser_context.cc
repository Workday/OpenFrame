// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/usb/usb_chooser_context.h"

#include <vector>

#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "device/core/device_client.h"
#include "device/usb/usb_device.h"

using device::UsbDevice;

namespace {

const char kDeviceNameKey[] = "name";
const char kGuidKey[] = "ephemeral-guid";
const char kProductIdKey[] = "product-id";
const char kSerialNumberKey[] = "serial-number";
const char kVendorIdKey[] = "vendor-id";

bool CanStorePersistentEntry(const scoped_refptr<const UsbDevice>& device) {
  return !device->serial_number().empty();
}

const base::DictionaryValue* FindForDevice(
    const std::vector<scoped_ptr<base::DictionaryValue>>& device_list,
    const scoped_refptr<const UsbDevice>& device) {
  const std::string utf8_serial_number =
      base::UTF16ToUTF8(device->serial_number());

  for (const scoped_ptr<base::DictionaryValue>& device_dict : device_list) {
    int vendor_id;
    int product_id;
    std::string serial_number;
    if (device_dict->GetInteger(kVendorIdKey, &vendor_id) &&
        device->vendor_id() == vendor_id &&
        device_dict->GetInteger(kProductIdKey, &product_id) &&
        device->product_id() == product_id &&
        device_dict->GetString(kSerialNumberKey, &serial_number) &&
        utf8_serial_number == serial_number) {
      return device_dict.get();
    }
  }
  return nullptr;
}

}  // namespace

UsbChooserContext::UsbChooserContext(Profile* profile)
    : ChooserContextBase(profile, CONTENT_SETTINGS_TYPE_USB_CHOOSER_DATA),
      is_off_the_record_(profile->IsOffTheRecord()),
      observer_(this) {
  usb_service_ = device::DeviceClient::Get()->GetUsbService();
  if (usb_service_)
    observer_.Add(usb_service_);
}

UsbChooserContext::~UsbChooserContext() {}

std::vector<scoped_ptr<base::DictionaryValue>>
UsbChooserContext::GetGrantedObjects(const GURL& requesting_origin,
                                     const GURL& embedding_origin) {
  std::vector<scoped_ptr<base::DictionaryValue>> objects =
      ChooserContextBase::GetGrantedObjects(requesting_origin,
                                            embedding_origin);

  auto it = ephemeral_devices_.find(
      std::make_pair(requesting_origin, embedding_origin));
  if (it != ephemeral_devices_.end()) {
    for (const std::string& guid : it->second) {
      scoped_refptr<UsbDevice> device = usb_service_->GetDevice(guid);
      DCHECK(device);
      scoped_ptr<base::DictionaryValue> object(new base::DictionaryValue());
      object->SetString(kDeviceNameKey, device->product_string());
      object->SetString(kGuidKey, device->guid());
      objects.push_back(object.Pass());
    }
  }

  return objects;
}

std::vector<scoped_ptr<ChooserContextBase::Object>>
UsbChooserContext::GetAllGrantedObjects() {
  std::vector<scoped_ptr<ChooserContextBase::Object>> objects =
      ChooserContextBase::GetAllGrantedObjects();

  for (const auto& map_entry : ephemeral_devices_) {
    const GURL& requesting_origin = map_entry.first.first;
    const GURL& embedding_origin = map_entry.first.second;
    for (const std::string& guid : map_entry.second) {
      scoped_refptr<UsbDevice> device = usb_service_->GetDevice(guid);
      DCHECK(device);
      base::DictionaryValue object;
      object.SetString(kDeviceNameKey, device->product_string());
      object.SetString(kGuidKey, device->guid());
      objects.push_back(make_scoped_ptr(
          new ChooserContextBase::Object(requesting_origin, embedding_origin,
                                         &object, "", is_off_the_record_)));
    }
  }

  return objects;
}

void UsbChooserContext::RevokeObjectPermission(
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    const base::DictionaryValue& object) {
  std::string guid;
  if (object.GetString(kGuidKey, &guid)) {
    RevokeDevicePermission(requesting_origin, embedding_origin, guid);
  } else {
    ChooserContextBase::RevokeObjectPermission(requesting_origin,
                                               embedding_origin, object);
  }
}

void UsbChooserContext::GrantDevicePermission(const GURL& requesting_origin,
                                              const GURL& embedding_origin,
                                              const std::string& guid) {
  scoped_refptr<UsbDevice> device = usb_service_->GetDevice(guid);
  if (!device)
    return;

  if (CanStorePersistentEntry(device)) {
    scoped_ptr<base::DictionaryValue> device_dict(new base::DictionaryValue());
    device_dict->SetString(kDeviceNameKey, device->product_string());
    device_dict->SetInteger(kVendorIdKey, device->vendor_id());
    device_dict->SetInteger(kProductIdKey, device->product_id());
    device_dict->SetString(kSerialNumberKey, device->serial_number());
    GrantObjectPermission(requesting_origin, embedding_origin,
                          device_dict.Pass());
  } else {
    ephemeral_devices_[std::make_pair(requesting_origin, embedding_origin)]
        .insert(guid);
  }
}

void UsbChooserContext::RevokeDevicePermission(const GURL& requesting_origin,
                                               const GURL& embedding_origin,
                                               const std::string& guid) {
  auto it = ephemeral_devices_.find(
      std::make_pair(requesting_origin, embedding_origin));
  if (it != ephemeral_devices_.end()) {
    it->second.erase(guid);
    if (it->second.empty())
      ephemeral_devices_.erase(it);
  }

  scoped_refptr<UsbDevice> device = usb_service_->GetDevice(guid);
  if (!device)
    return;

  std::vector<scoped_ptr<base::DictionaryValue>> device_list =
      GetGrantedObjects(requesting_origin, embedding_origin);
  const base::DictionaryValue* entry = FindForDevice(device_list, device);
  if (entry)
    RevokeObjectPermission(requesting_origin, embedding_origin, *entry);
}

bool UsbChooserContext::HasDevicePermission(const GURL& requesting_origin,
                                            const GURL& embedding_origin,
                                            const std::string& guid) {
  auto it = ephemeral_devices_.find(
      std::make_pair(requesting_origin, embedding_origin));
  if (it != ephemeral_devices_.end())
    return ContainsValue(it->second, guid);

  scoped_refptr<UsbDevice> device = usb_service_->GetDevice(guid);
  if (!device)
    return false;

  std::vector<scoped_ptr<base::DictionaryValue>> device_list =
      GetGrantedObjects(requesting_origin, embedding_origin);
  return FindForDevice(device_list, device) != nullptr;
}

bool UsbChooserContext::IsValidObject(const base::DictionaryValue& object) {
  return object.size() == 4 && object.HasKey(kDeviceNameKey) &&
         object.HasKey(kVendorIdKey) && object.HasKey(kProductIdKey) &&
         object.HasKey(kSerialNumberKey);
}

void UsbChooserContext::OnDeviceRemoved(scoped_refptr<UsbDevice> device) {
  for (auto& map_entry : ephemeral_devices_)
    map_entry.second.erase(device->guid());
}
