// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Multiply-included file, hence no include guard.

#include "apps/app_shim/app_shim_messages.h"
#include "chrome/common/benchmarking_messages.h"
#include "chrome/common/chrome_utility_messages.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/one_click_signin_messages.h"
#include "chrome/common/prerender_messages.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/safe_browsing/safebrowsing_messages.h"
#include "chrome/common/service_messages.h"
#include "chrome/common/spellcheck_messages.h"
#include "chrome/common/tts_messages.h"
#include "chrome/common/validation_message_messages.h"
#include "components/nacl/common/nacl_host_messages.h"

#if defined(ENABLE_MDNS)
#include "chrome/common/local_discovery/local_discovery_messages.h"
#endif

#if defined(ENABLE_PRINTING)
#include "chrome/common/print_messages.h"
#endif

#if defined(ENABLE_WEBRTC)
#include "chrome/common/media/webrtc_logging_messages.h"
#endif
