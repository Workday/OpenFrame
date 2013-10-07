// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_INTENT_HELPER_H_
#define CHROME_BROWSER_ANDROID_INTENT_HELPER_H_

#include <jni.h>

#include "base/strings/string16.h"

namespace chrome {
namespace android {

// Triggers a send email intent.
void SendEmail(const string16& data_email,
               const string16& data_subject,
               const string16& data_body,
               const string16& data_chooser_title,
               const string16& data_file_to_attach);

bool RegisterIntentHelper(JNIEnv* env);

}  // namespace android
}  // namespace chrome

#endif  // CHROME_BROWSER_ANDROID_INTENT_HELPER_H_
