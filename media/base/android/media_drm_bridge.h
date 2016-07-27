// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_ANDROID_MEDIA_DRM_BRIDGE_H_
#define MEDIA_BASE_ANDROID_MEDIA_DRM_BRIDGE_H_

#include <jni.h>
#include <string>
#include <vector>

#include "base/android/scoped_java_ref.h"
#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "media/base/android/provision_fetcher.h"
#include "media/base/cdm_promise_adapter.h"
#include "media/base/media_export.h"
#include "media/base/media_keys.h"
#include "media/base/player_tracker.h"
#include "media/cdm/player_tracker_impl.h"
#include "url/gurl.h"

class GURL;

namespace media {

// Implements a CDM using Android MediaDrm API.
//
// Thread Safety:
//
// This class lives on the thread where it is created. All methods must be
// called on the |task_runner_| except for the PlayerTracker methods and
// SetMediaCryptoReadyCB(), which can be called on any thread.

class MEDIA_EXPORT MediaDrmBridge : public MediaKeys, public PlayerTracker {
 public:
  // TODO(ddorwin): These are specific to Widevine. http://crbug.com/459400
  enum SecurityLevel {
    SECURITY_LEVEL_NONE = 0,
    SECURITY_LEVEL_1 = 1,
    SECURITY_LEVEL_3 = 3,
  };

  using JavaObjectPtr = scoped_ptr<base::android::ScopedJavaGlobalRef<jobject>>;

  using ResetCredentialsCB = base::Callback<void(bool)>;

  // Notification called when MediaCrypto object is ready.
  // Parameters:
  // |media_crypto| - global reference to MediaCrypto object
  // |needs_protected_surface| - true if protected surface is required.
  using MediaCryptoReadyCB = base::Callback<void(JavaObjectPtr media_crypto,
                                                 bool needs_protected_surface)>;

  // Checks whether MediaDRM is available.
  // All other static methods check IsAvailable() internally. There's no need
  // to check IsAvailable() explicitly before calling them.
  static bool IsAvailable();

  static bool RegisterMediaDrmBridge(JNIEnv* env);

  // Checks whether |key_system| is supported.
  static bool IsKeySystemSupported(const std::string& key_system);

  // Checks whether |key_system| is supported with |container_mime_type|.
  // |container_mime_type| must not be empty.
  static bool IsKeySystemSupportedWithType(
      const std::string& key_system,
      const std::string& container_mime_type);

  // Returns the list of the platform-supported key system names that
  // are not handled by Chrome explicitly.
  static std::vector<std::string> GetPlatformKeySystemNames();

  // Returns a MediaDrmBridge instance if |key_system| is supported, or a NULL
  // pointer otherwise.
  // TODO(xhwang): Is it okay not to update session expiration info?
  static scoped_refptr<MediaDrmBridge> Create(
      const std::string& key_system,
      const CreateFetcherCB& create_fetcher_cb,
      const SessionMessageCB& session_message_cb,
      const SessionClosedCB& session_closed_cb,
      const LegacySessionErrorCB& legacy_session_error_cb,
      const SessionKeysChangeCB& session_keys_change_cb,
      const SessionExpirationUpdateCB& session_expiration_update_cb);

  // Returns a MediaDrmBridge instance if |key_system| is supported, or a NULL
  // otherwise. No session callbacks are provided. This is used when we need to
  // use MediaDrmBridge without creating any sessions.
  static scoped_refptr<MediaDrmBridge> CreateWithoutSessionSupport(
      const std::string& key_system,
      const CreateFetcherCB& create_fetcher_cb);

  // MediaKeys implementation.
  void SetServerCertificate(
      const std::vector<uint8_t>& certificate,
      scoped_ptr<media::SimpleCdmPromise> promise) override;
  void CreateSessionAndGenerateRequest(
      SessionType session_type,
      media::EmeInitDataType init_data_type,
      const std::vector<uint8_t>& init_data,
      scoped_ptr<media::NewSessionCdmPromise> promise) override;
  void LoadSession(SessionType session_type,
                   const std::string& session_id,
                   scoped_ptr<media::NewSessionCdmPromise> promise) override;
  void UpdateSession(const std::string& session_id,
                     const std::vector<uint8_t>& response,
                     scoped_ptr<media::SimpleCdmPromise> promise) override;
  void CloseSession(const std::string& session_id,
                    scoped_ptr<media::SimpleCdmPromise> promise) override;
  void RemoveSession(const std::string& session_id,
                     scoped_ptr<media::SimpleCdmPromise> promise) override;
  void DeleteOnCorrectThread() const override;

  // PlayerTracker implementation. Can be called on any thread.
  // The registered callbacks will be fired on |task_runner_|. The caller
  // should make sure that the callbacks are posted to the correct thread.
  //
  // Note: RegisterPlayer() should be called before SetMediaCryptoReadyCB() to
  // avoid missing any new key notifications.
  int RegisterPlayer(const base::Closure& new_key_cb,
                     const base::Closure& cdm_unset_cb) override;
  void UnregisterPlayer(int registration_id) override;

  // Returns true if |security_level| is successfully set, or false otherwise.
  // Call this function right after Create() and before any other calls.
  // Note:
  // - If this function is not called, the default security level of the device
  //   will be used.
  // - It's recommended to call this function only once on a MediaDrmBridge
  //   object. Calling this function multiples times may cause errors.
  bool SetSecurityLevel(SecurityLevel security_level);

  // Helper function to determine whether a protected surface is needed for the
  // video playback.
  bool IsProtectedSurfaceRequired();

  // Reset the device credentials.
  void ResetDeviceCredentials(const ResetCredentialsCB& callback);

  // Helper functions to resolve promises.
  void ResolvePromise(uint32_t promise_id);
  void ResolvePromiseWithSession(uint32_t promise_id,
                                 const std::string& session_id);
  void RejectPromise(uint32_t promise_id, const std::string& error_message);

  // Returns a MediaCrypto object if it's already created. Returns a null object
  // otherwise.
  base::android::ScopedJavaLocalRef<jobject> GetMediaCrypto();

  // Registers a callback which will be called when MediaCrypto is ready.
  // Can be called on any thread. Only one callback should be registered.
  // The registered callbacks will be fired on |task_runner_|. The caller
  // should make sure that the callbacks are posted to the correct thread.
  // TODO(xhwang): Move this up to be close to RegisterPlayer().
  void SetMediaCryptoReadyCB(const MediaCryptoReadyCB& media_crypto_ready_cb);

  // All the OnXxx functions below are called from Java. The implementation must
  // only do minimal work and then post tasks to avoid reentrancy issues.

  // Called by Java after a MediaCrypto object is created.
  void OnMediaCryptoReady(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& j_media_drm);

  // Called by Java when we need to send a provisioning request,
  void OnStartProvisioning(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& j_media_drm,
      const base::android::JavaParamRef<jstring>& j_default_url,
      const base::android::JavaParamRef<jbyteArray>& j_request_data);

  // Callbacks to resolve the promise for |promise_id|.
  void OnPromiseResolved(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& j_media_drm,
      jint j_promise_id);
  void OnPromiseResolvedWithSession(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& j_media_drm,
      jint j_promise_id,
      const base::android::JavaParamRef<jbyteArray>& j_session_id);

  // Callback to reject the promise for |promise_id| with |error_message|.
  // Note: No |system_error| is available from MediaDrm.
  // TODO(xhwang): Implement Exception code.
  void OnPromiseRejected(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& j_media_drm,
      jint j_promise_id,
      const base::android::JavaParamRef<jstring>& j_error_message);

  // Session event callbacks.

  // TODO(xhwang): Remove |j_legacy_destination_url| when prefixed EME support
  // is removed.
  void OnSessionMessage(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& j_media_drm,
      const base::android::JavaParamRef<jbyteArray>& j_session_id,
      jint j_message_type,
      const base::android::JavaParamRef<jbyteArray>& j_message,
      const base::android::JavaParamRef<jstring>& j_legacy_destination_url);
  void OnSessionClosed(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& j_media_drm,
      const base::android::JavaParamRef<jbyteArray>& j_session_id);

  void OnSessionKeysChange(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& j_media_drm,
      const base::android::JavaParamRef<jbyteArray>& j_session_id,
      const base::android::JavaParamRef<jobjectArray>& j_keys_info,
      bool has_additional_usable_key);

  // |expiry_time_ms| is the new expiration time for the keys in the session.
  // The time is in milliseconds, relative to the Unix epoch. A time of 0
  // indicates that the keys never expire.
  void OnSessionExpirationUpdate(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& j_media_drm,
      const base::android::JavaParamRef<jbyteArray>& j_session_id,
      jlong expiry_time_ms);

  // Called by the CDM when an error occurred in session |j_session_id|
  // unrelated to one of the MediaKeys calls that accept a |promise|.
  // Note:
  // - This method is only for supporting prefixed EME API.
  // - This method will be ignored by unprefixed EME. All errors reported
  //   in this method should probably also be reported by one of other methods.
  void OnLegacySessionError(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& j_media_drm,
      const base::android::JavaParamRef<jbyteArray>& j_session_id,
      const base::android::JavaParamRef<jstring>& j_error_message);

  // Called by the java object when credential reset is completed.
  void OnResetDeviceCredentialsCompleted(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>&,
      bool success);

 private:
  // For DeleteSoon() in DeleteOnCorrectThread().
  friend class base::DeleteHelper<MediaDrmBridge>;

  MediaDrmBridge(const std::vector<uint8>& scheme_uuid,
                 const CreateFetcherCB& create_fetcher_cb,
                 const SessionMessageCB& session_message_cb,
                 const SessionClosedCB& session_closed_cb,
                 const LegacySessionErrorCB& legacy_session_error_cb,
                 const SessionKeysChangeCB& session_keys_change_cb,
                 const SessionExpirationUpdateCB& session_expiration_update_cb);

  ~MediaDrmBridge() override;

  static bool IsSecureDecoderRequired(SecurityLevel security_level);

  // Get the security level of the media.
  SecurityLevel GetSecurityLevel();

  // A helper method that calculates the |media_crypto_ready_cb_| arguments and
  // run this callback.
  void NotifyMediaCryptoReady(const MediaCryptoReadyCB& cb);

  // Sends HTTP provisioning request to a provisioning server.
  void SendProvisioningRequest(const std::string& default_url,
                               const std::string& request_data);

  // Process the data received by provisioning server.
  void ProcessProvisionResponse(bool success, const std::string& response);

  // UUID of the key system.
  std::vector<uint8> scheme_uuid_;

  // Java MediaDrm instance.
  base::android::ScopedJavaGlobalRef<jobject> j_media_drm_;

  // The callback to create a ProvisionFetcher.
  CreateFetcherCB create_fetcher_cb_;

  // The ProvisionFetcher that requests and receives provisioning data.
  // Non-null iff when a provision request is pending.
  scoped_ptr<ProvisionFetcher> provision_fetcher_;

  // Callbacks for firing session events.
  SessionMessageCB session_message_cb_;
  SessionClosedCB session_closed_cb_;
  LegacySessionErrorCB legacy_session_error_cb_;
  SessionKeysChangeCB session_keys_change_cb_;
  SessionExpirationUpdateCB session_expiration_update_cb_;

  MediaCryptoReadyCB media_crypto_ready_cb_;

  ResetCredentialsCB reset_credentials_cb_;

  PlayerTrackerImpl player_tracker_;

  CdmPromiseAdapter cdm_promise_adapter_;

  // Default task runner.
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  // NOTE: Weak pointers must be invalidated before all other member variables.
  base::WeakPtrFactory<MediaDrmBridge> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(MediaDrmBridge);
};

}  // namespace media

#endif  // MEDIA_BASE_ANDROID_MEDIA_DRM_BRIDGE_H_
