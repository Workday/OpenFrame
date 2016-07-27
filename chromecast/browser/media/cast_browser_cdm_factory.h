// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMECAST_BROWSER_MEDIA_CAST_BROWSER_CDM_FACTORY_H_
#define CHROMECAST_BROWSER_MEDIA_CAST_BROWSER_CDM_FACTORY_H_

#include "chromecast/media/base/key_systems_common.h"
#include "media/base/cdm_factory.h"
#include "media/base/media_keys.h"

namespace chromecast {
namespace media {

class BrowserCdmCast;

class CastBrowserCdmFactory : public ::media::CdmFactory {
 public:
  CastBrowserCdmFactory() {}
  ~CastBrowserCdmFactory() override {};

  // ::media::CdmFactory implementation:
  void Create(
      const std::string& key_system,
      const GURL& security_origin,
      const ::media::CdmConfig& cdm_config,
      const ::media::SessionMessageCB& session_message_cb,
      const ::media::SessionClosedCB& session_closed_cb,
      const ::media::LegacySessionErrorCB& legacy_session_error_cb,
      const ::media::SessionKeysChangeCB& session_keys_change_cb,
      const ::media::SessionExpirationUpdateCB& session_expiration_update_cb,
      const ::media::CdmCreatedCB& cdm_created_cb) override;

  // Provides a platform-specific BrowserCdm instance.
  virtual scoped_refptr<BrowserCdmCast> CreatePlatformBrowserCdm(
      const CastKeySystem& cast_key_system);

 private:
  DISALLOW_COPY_AND_ASSIGN(CastBrowserCdmFactory);
};

}  // namespace media
}  // namespace chromecast

#endif  // CHROMECAST_BROWSER_MEDIA_CAST_BROWSER_CDM_FACTORY_H_
