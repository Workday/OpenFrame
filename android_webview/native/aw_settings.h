// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ANDROID_WEBVIEW_NATIVE_AW_SETTINGS_H_
#define ANDROID_WEBVIEW_NATIVE_AW_SETTINGS_H_

#include <jni.h>

#include "base/android/jni_helper.h"
#include "base/android/scoped_java_ref.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/web_contents_observer.h"

namespace android_webview {

class AwRenderViewHostExt;

class AwSettings : public content::WebContentsObserver {
 public:
  AwSettings(JNIEnv* env, jobject obj, jint web_contents);
  virtual ~AwSettings();

  // Called from Java. Methods with "Locked" suffix require that the settings
  // access lock is held during their execution.
  void Destroy(JNIEnv* env, jobject obj);
  void ResetScrollAndScaleState(JNIEnv* env, jobject obj);
  void UpdateEverythingLocked(JNIEnv* env, jobject obj);
  void UpdateInitialPageScaleLocked(JNIEnv* env, jobject obj);
  void UpdateUserAgentLocked(JNIEnv* env, jobject obj);
  void UpdateWebkitPreferencesLocked(JNIEnv* env, jobject obj);
  void UpdateFormDataPreferencesLocked(JNIEnv* env, jobject obj);

 private:
  AwRenderViewHostExt* GetAwRenderViewHostExt();
  void UpdateEverything();
  void UpdatePreferredSizeMode();

  // WebContentsObserver overrides:
  virtual void RenderViewCreated(
      content::RenderViewHost* render_view_host) OVERRIDE;
  virtual void WebContentsDestroyed(
      content::WebContents* web_contents) OVERRIDE;

  JavaObjectWeakGlobalRef aw_settings_;
};

bool RegisterAwSettings(JNIEnv* env);

}  // namespace android_webview

#endif  // ANDROID_WEBVIEW_NATIVE_AW_SETTINGS_H_
