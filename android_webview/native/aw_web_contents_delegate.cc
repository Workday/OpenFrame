// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/native/aw_web_contents_delegate.h"

#include "android_webview/browser/aw_javascript_dialog_manager.h"
#include "android_webview/browser/find_helper.h"
#include "android_webview/native/aw_contents.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/lazy_instance.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/file_chooser_params.h"
#include "jni/AwWebContentsDelegate_jni.h"
#include "ui/shell_dialogs/selected_file_info.h"

using base::android::AttachCurrentThread;
using base::android::ConvertUTF16ToJavaString;
using base::android::ConvertUTF8ToJavaString;
using base::android::ScopedJavaLocalRef;
using content::FileChooserParams;
using content::WebContents;

namespace android_webview {

namespace {

// WARNING: these constants are exposed in the public interface Java side, so
// must remain in sync with what clients are expecting.
const int kFileChooserModeOpenMultiple = 1 << 0;
const int kFileChooserModeOpenFolder = 1 << 1;

base::LazyInstance<AwJavaScriptDialogManager>::Leaky
    g_javascript_dialog_manager = LAZY_INSTANCE_INITIALIZER;
}

AwWebContentsDelegate::AwWebContentsDelegate(
    JNIEnv* env,
    jobject obj)
    : WebContentsDelegateAndroid(env, obj) {
}

AwWebContentsDelegate::~AwWebContentsDelegate() {
}

content::JavaScriptDialogManager*
AwWebContentsDelegate::GetJavaScriptDialogManager() {
  return g_javascript_dialog_manager.Pointer();
}

void AwWebContentsDelegate::FindReply(WebContents* web_contents,
                                      int request_id,
                                      int number_of_matches,
                                      const gfx::Rect& selection_rect,
                                      int active_match_ordinal,
                                      bool final_update) {
  AwContents* aw_contents = AwContents::FromWebContents(web_contents);
  if (!aw_contents)
    return;

  aw_contents->GetFindHelper()->HandleFindReply(request_id,
                                                number_of_matches,
                                                active_match_ordinal,
                                                final_update);
}

void AwWebContentsDelegate::CanDownload(
    content::RenderViewHost* source,
    int request_id,
    const std::string& request_method,
    const base::Callback<void(bool)>& callback) {
  // Android webview intercepts download in its resource dispatcher host
  // delegate, so should not reach here.
  NOTREACHED();
  callback.Run(false);
}

void AwWebContentsDelegate::RunFileChooser(WebContents* web_contents,
                                           const FileChooserParams& params) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> java_delegate = GetJavaDelegate(env);
  if (!java_delegate.obj())
    return;

  int mode_flags = 0;
  if (params.mode == FileChooserParams::OpenMultiple) {
    mode_flags |= kFileChooserModeOpenMultiple;
  } else if (params.mode == FileChooserParams::UploadFolder) {
    // Folder implies multiple in Chrome.
    mode_flags |= kFileChooserModeOpenMultiple | kFileChooserModeOpenFolder;
  } else if (params.mode == FileChooserParams::Save) {
    // Save not supported, so cancel it.
    web_contents->GetRenderViewHost()->FilesSelectedInChooser(
         std::vector<ui::SelectedFileInfo>(),
         params.mode);
    return;
  } else {
    DCHECK_EQ(FileChooserParams::Open, params.mode);
  }
  Java_AwWebContentsDelegate_runFileChooser(env,
      java_delegate.obj(),
      web_contents->GetRenderProcessHost()->GetID(),
      web_contents->GetRenderViewHost()->GetRoutingID(),
      mode_flags,
      ConvertUTF16ToJavaString(env,
        JoinString(params.accept_types, ',')).obj(),
      params.title.empty() ? NULL :
          ConvertUTF16ToJavaString(env, params.title).obj(),
      params.default_file_name.empty() ? NULL :
          ConvertUTF8ToJavaString(env, params.default_file_name.value()).obj(),
      params.capture);
}

void AwWebContentsDelegate::AddNewContents(WebContents* source,
                                           WebContents* new_contents,
                                           WindowOpenDisposition disposition,
                                           const gfx::Rect& initial_pos,
                                           bool user_gesture,
                                           bool* was_blocked) {
  JNIEnv* env = AttachCurrentThread();

  bool is_dialog = disposition == NEW_POPUP;
  ScopedJavaLocalRef<jobject> java_delegate = GetJavaDelegate(env);
  bool create_popup = false;

  if (java_delegate.obj()) {
    create_popup = Java_AwWebContentsDelegate_addNewContents(env,
        java_delegate.obj(), is_dialog, user_gesture);
  }

  if (create_popup) {
    // The embedder would like to display the popup and we will receive
    // a callback from them later with an AwContents to use to display
    // it. The source AwContents takes ownership of the new WebContents
    // until then, and when the callback is made we will swap the WebContents
    // out into the new AwContents.
    AwContents::FromWebContents(source)->SetPendingWebContentsForPopup(
        make_scoped_ptr(new_contents));
    // Hide the WebContents for the pop up now, we will show it again
    // when the user calls us back with an AwContents to use to show it.
    new_contents->WasHidden();
  } else {
    // The embedder has forgone their chance to display this popup
    // window, so we're done with the WebContents now. We use
    // DeleteSoon as WebContentsImpl may call methods on |new_contents|
    // after this method returns.
    base::MessageLoop::current()->DeleteSoon(FROM_HERE, new_contents);
  }

  if (was_blocked) {
    *was_blocked = !create_popup;
  }
}

void AwWebContentsDelegate::CloseContents(WebContents* source) {
  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jobject> java_delegate = GetJavaDelegate(env);
  if (java_delegate.obj()) {
    Java_AwWebContentsDelegate_closeContents(env, java_delegate.obj());
  }
}

void AwWebContentsDelegate::ActivateContents(WebContents* contents) {
  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jobject> java_delegate = GetJavaDelegate(env);
  if (java_delegate.obj()) {
    Java_AwWebContentsDelegate_activateContents(env, java_delegate.obj());
  }
}

void AwWebContentsDelegate::UpdatePreferredSize(
    WebContents* web_contents,
    const gfx::Size& pref_size) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = GetJavaDelegate(env);
  if (obj.is_null())
    return;
  return Java_AwWebContentsDelegate_updatePreferredSize(
      env, obj.obj(), pref_size.width(), pref_size.height());
}

static void FilesSelectedInChooser(
    JNIEnv* env, jclass clazz,
    jint process_id, jint render_id, jint mode_flags,
    jobjectArray file_paths) {
  content::RenderViewHost* rvh = content::RenderViewHost::FromID(process_id,
                                                                 render_id);
  if (!rvh)
    return;

  std::vector<std::string> file_path_str;
  // Note file_paths maybe NULL, but this will just yield a zero-length vector.
  base::android::AppendJavaStringArrayToStringVector(env, file_paths,
                                                     &file_path_str);
  std::vector<ui::SelectedFileInfo> files;
  files.reserve(file_path_str.size());
  for (size_t i = 0; i < file_path_str.size(); ++i) {
    files.push_back(ui::SelectedFileInfo(base::FilePath(file_path_str[i]),
                                         base::FilePath()));
  }
  FileChooserParams::Mode mode;
  if (mode_flags & kFileChooserModeOpenFolder) {
    mode = FileChooserParams::UploadFolder;
  } else if (mode_flags & kFileChooserModeOpenMultiple) {
    mode = FileChooserParams::OpenMultiple;
  } else {
    mode = FileChooserParams::Open;
  }
  LOG(INFO) << "File Chooser result: mode = " << mode
            << ", file paths = " << JoinString(file_path_str, ":");
  rvh->FilesSelectedInChooser(files, mode);
}

bool RegisterAwWebContentsDelegate(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace android_webview
