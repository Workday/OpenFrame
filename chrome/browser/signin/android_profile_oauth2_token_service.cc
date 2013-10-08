// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/android_profile_oauth2_token_service.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/logging.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/sync/profile_sync_service_android.h"
#include "content/public/browser/browser_thread.h"
#include "jni/AndroidProfileOAuth2TokenServiceHelper_jni.h"

using base::android::AttachCurrentThread;
using base::android::CheckException;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::ScopedJavaLocalRef;
using content::BrowserThread;

namespace {

std::string CombineScopes(const OAuth2TokenService::ScopeSet& scopes) {
  // The Android AccountManager supports multiple scopes separated by a space:
  // https://code.google.com/p/google-api-java-client/wiki/OAuth2#Android
  std::string scope;
  for (OAuth2TokenService::ScopeSet::const_iterator it = scopes.begin();
       it != scopes.end(); ++it) {
    if (!scope.empty())
      scope += " ";
    scope += *it;
  }
  return scope;
}

}  // namespace

AndroidProfileOAuth2TokenService::AndroidProfileOAuth2TokenService() {
}

AndroidProfileOAuth2TokenService::~AndroidProfileOAuth2TokenService() {
}

scoped_ptr<OAuth2TokenService::Request>
    AndroidProfileOAuth2TokenService::StartRequest(
        const OAuth2TokenService::ScopeSet& scopes,
        OAuth2TokenService::Consumer* consumer) {
  const std::string& sync_username =
      SigninManagerFactory::GetForProfile(profile())->
          GetAuthenticatedUsername();
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  DCHECK(!sync_username.empty());
  return StartRequestForUsername(sync_username, scopes, consumer);
}

scoped_ptr<OAuth2TokenService::Request>
    AndroidProfileOAuth2TokenService::StartRequestForUsername(
        const std::string& username,
        const OAuth2TokenService::ScopeSet& scopes,
        OAuth2TokenService::Consumer* consumer) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  scoped_ptr<RequestImpl> request(new RequestImpl(consumer));
  FetchOAuth2Token(
      username,
      CombineScopes(scopes),
      base::Bind(&RequestImpl::InformConsumer, request->AsWeakPtr()));
  return request.PassAs<Request>();
}

bool AndroidProfileOAuth2TokenService::RefreshTokenIsAvailable() {
  SigninManagerBase* signin_manager =
      SigninManagerFactory::GetForProfile(profile());
  return !signin_manager->GetAuthenticatedUsername().empty();
}

void AndroidProfileOAuth2TokenService::InvalidateToken(
    const ScopeSet& scopes,
    const std::string& invalid_token) {
  OAuth2TokenService::InvalidateToken(scopes, invalid_token);

  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jstring> j_invalid_token =
      ConvertUTF8ToJavaString(env, invalid_token);
  Java_AndroidProfileOAuth2TokenServiceHelper_invalidateOAuth2AuthToken(
      env, base::android::GetApplicationContext(),
      j_invalid_token.obj());
}

void AndroidProfileOAuth2TokenService::FetchOAuth2Token(
    const std::string& username,
    const std::string& scope,
    const FetchOAuth2TokenCallback& callback) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jstring> j_username =
      ConvertUTF8ToJavaString(env, username);
  ScopedJavaLocalRef<jstring> j_scope =
      ConvertUTF8ToJavaString(env, scope);

  // Allocate a copy of the callback on the heap, because the callback
  // needs to be passed through JNI as an int.
  // It will be passed back to OAuth2TokenFetched(), where it will be freed.
  scoped_ptr<FetchOAuth2TokenCallback> heap_callback(
      new FetchOAuth2TokenCallback(callback));

  // Call into Java to get a new token.
  Java_AndroidProfileOAuth2TokenServiceHelper_getOAuth2AuthToken(
      env, base::android::GetApplicationContext(),
      j_username.obj(),
      j_scope.obj(),
      reinterpret_cast<int>(heap_callback.release()));
}

// Called from Java when fetching of an OAuth2 token is finished. The
// |authToken| param is only valid when |result| is true.
void OAuth2TokenFetched(JNIEnv* env, jclass clazz,
    jstring authToken,
    jboolean result,
    jint nativeCallback) {
  std::string token = ConvertJavaStringToUTF8(env, authToken);
  scoped_ptr<AndroidProfileOAuth2TokenService::FetchOAuth2TokenCallback>
      heap_callback(
          reinterpret_cast<
              AndroidProfileOAuth2TokenService::FetchOAuth2TokenCallback*>(
                  nativeCallback));
  GoogleServiceAuthError err(result ?
                             GoogleServiceAuthError::NONE :
                             GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS);
  heap_callback->Run(err, token, base::Time());
}

// static
bool AndroidProfileOAuth2TokenService::Register(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
