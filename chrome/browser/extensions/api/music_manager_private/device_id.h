// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_MUSIC_MANAGER_PRIVATE_DEVICE_ID_H_
#define CHROME_BROWSER_EXTENSIONS_API_MUSIC_MANAGER_PRIVATE_DEVICE_ID_H_

#include <string>

#include "base/bind.h"

namespace extensions {
namespace api {

class DeviceId {
 public:
  typedef base::Callback<void(const std::string&)> IdCallback;

  // Return a "device" identifier with the following characteristics:
  // 1. The id is shared across users of a device.
  // 2. The id is resilient to device reboots.
  // 3. There is *some* way for the identifier to be reset (e.g. it can *not* be
  //    the MAC address of the device's network card).
  // The specific implementation varies across platforms, some of them requiring
  // a round trip to the IO or FILE thread. "callback" will always be called
  // on the UI thread though (sometimes directly if the implementation allows
  // running on the UI thread).
  // The returned value is HMAC_SHA256(machine_id, |extension_id|), so that the
  // actual machine identifier value is not exposed directly to the caller.
  static void GetDeviceId(const std::string& extension_id,
                          const IdCallback& callback);

 private:
  // Platform specific implementation of "raw" machine ID retrieval.
  static void GetMachineId(const IdCallback& callback);
};

}  // namespace api
}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_MUSIC_MANAGER_PRIVATE_DEVICE_ID_H_
