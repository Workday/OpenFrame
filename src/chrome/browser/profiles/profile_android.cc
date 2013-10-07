// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/profile_android.h"

#include "base/android/jni_android.h"
#include "chrome/browser/profiles/profile.h"
#include "jni/Profile_jni.h"

using base::android::AttachCurrentThread;

namespace {
const char kProfileAndroidKey[] = "profile_android";
}  // namespace

// static
ProfileAndroid* ProfileAndroid::FromProfile(Profile* profile) {
  if (!profile)
    return NULL;

  ProfileAndroid* profile_android = static_cast<ProfileAndroid*>(
      profile->GetUserData(kProfileAndroidKey));
  if (!profile_android) {
    profile_android = new ProfileAndroid(profile);
    profile->SetUserData(kProfileAndroidKey, profile_android);
  }
  return profile_android;
}

// static
Profile* ProfileAndroid::FromProfileAndroid(jobject obj) {
  if (!obj)
    return NULL;

  ProfileAndroid* profile_android = reinterpret_cast<ProfileAndroid*>(
      Java_Profile_getNativePointer(AttachCurrentThread(), obj));
  if (!profile_android)
    return NULL;
  return profile_android->profile_;
}

// static
bool ProfileAndroid::RegisterProfileAndroid(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

ProfileAndroid::ProfileAndroid(Profile* profile)
    : profile_(profile) {
  JNIEnv* env = AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> jprofile =
      Java_Profile_create(env, reinterpret_cast<int>(this));
  obj_.Reset(env, jprofile.obj());

}

ProfileAndroid::~ProfileAndroid() {
  Java_Profile_destroy(AttachCurrentThread(), obj_.obj());
}

base::android::ScopedJavaLocalRef<jobject> ProfileAndroid::GetJavaObject() {
  return base::android::ScopedJavaLocalRef<jobject>(obj_);
}
