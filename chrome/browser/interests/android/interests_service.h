// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_INTERESTS_ANDROID_INTERESTS_SERVICE_H_
#define CHROME_BROWSER_INTERESTS_ANDROID_INTERESTS_SERVICE_H_

#include <jni.h>

#include "base/android/scoped_java_ref.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/interests/interests_fetcher.h"

class Profile;

// Provides a list of user interests to Java
class InterestsService {
 public:
  explicit InterestsService(Profile* profile);
  virtual ~InterestsService();

  void Destroy(JNIEnv* env, jobject obj);
  static bool Register(JNIEnv* env);
  void GetInterests(JNIEnv* env,
                    jobject obj,
                    jobject j_callback);

 private:
  void OnObtainedInterests(
      scoped_ptr<InterestsFetcher> fetcher,
      const base::android::ScopedJavaGlobalRef<jobject>& j_callback,
      scoped_ptr<std::vector<InterestsFetcher::Interest>> interests);

  Profile* profile_;

  base::WeakPtrFactory<InterestsService> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(InterestsService);
};

#endif  // CHROME_BROWSER_INTERESTS_ANDROID_INTERESTS_SERVICE_H_
