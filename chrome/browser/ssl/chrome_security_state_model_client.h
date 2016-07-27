// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SSL_CHROME_SECURITY_STATE_MODEL_CLIENT_H_
#define CHROME_BROWSER_SSL_CHROME_SECURITY_STATE_MODEL_CLIENT_H_

#include "base/macros.h"
#include "chrome/browser/ssl/security_state_model.h"
#include "chrome/browser/ssl/security_state_model_client.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class WebContents;
}  // namespace content

// Uses a WebContents to provide a SecurityStateModel with the
// information that it needs to determine the page's security status.
class ChromeSecurityStateModelClient
    : public SecurityStateModelClient,
      public content::WebContentsUserData<ChromeSecurityStateModelClient> {
 public:
  ~ChromeSecurityStateModelClient() override;

  const SecurityStateModel::SecurityInfo& GetSecurityInfo() const;

  // SecurityStateModelClient:
  bool RetrieveCert(scoped_refptr<net::X509Certificate>* cert) override;
  bool UsedPolicyInstalledCertificate() override;

 private:
  explicit ChromeSecurityStateModelClient(content::WebContents* web_contents);
  friend class content::WebContentsUserData<ChromeSecurityStateModelClient>;

  content::WebContents* web_contents_;
  scoped_ptr<SecurityStateModel> security_state_model_;

  DISALLOW_COPY_AND_ASSIGN(ChromeSecurityStateModelClient);
};

#endif  // CHROME_BROWSER_SSL_CHROME_SECURITY_STATE_MODEL_CLIENT_H_
