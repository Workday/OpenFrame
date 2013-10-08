// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/local_discovery/privet_constants.h"

namespace local_discovery {

const char kPrivetKeyError[] = "error";
const char kPrivetInfoKeyToken[] = "x-privet-token";
const char kPrivetKeyDeviceID[] = "device_id";
const char kPrivetKeyClaimURL[] = "claim_url";
const char kPrivetKeyClaimToken[] = "token";
const char kPrivetKeyTimeout[] = "timeout";

const char kPrivetErrorDeviceBusy[] = "device_busy";
const char kPrivetErrorPendingUserAction[] = "pending_user_action";
const char kPrivetErrorInvalidXPrivetToken[] = "invalid_x_privet_token";

const char kPrivetActionStart[] = "start";
const char kPrivetActionGetClaimToken[] = "getClaimToken";
const char kPrivetActionComplete[] = "complete";

const char kPrivetActionNameInfo[] = "info";

extern const char kPrivetDefaultDeviceType[] = "_privet._tcp.local";
extern const char kPrivetSubtypeTemplate[] = "%s._sub._privet._tcp.local";

const char kPrivetTxtKeyName[] = "ty";
const char kPrivetTxtKeyDescription[] = "note";
const char kPrivetTxtKeyURL[] = "url";
const char kPrivetTxtKeyType[] = "type";
const char kPrivetTxtKeyID[] = "id";
const char kPrivetTxtKeyConnectionState[] = "cs";

const char kPrivetConnectionStatusOnline[] = "online";
const char kPrivetConnectionStatusOffline[] = "offline";
const char kPrivetConnectionStatusConnecting[] = "connecting";
const char kPrivetConnectionStatusNotConfigured[] = "not-configured";

}  // namespace local_discovery
