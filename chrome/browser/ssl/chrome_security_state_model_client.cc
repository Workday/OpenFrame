// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ssl/chrome_security_state_model_client.h"

#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "chrome/browser/chromeos/policy/policy_cert_service.h"
#include "chrome/browser/chromeos/policy/policy_cert_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/cert_store.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/ssl_status.h"
#include "net/cert/x509_certificate.h"

DEFINE_WEB_CONTENTS_USER_DATA_KEY(ChromeSecurityStateModelClient);

ChromeSecurityStateModelClient::ChromeSecurityStateModelClient(
    content::WebContents* web_contents)
    : web_contents_(web_contents),
      security_state_model_(new SecurityStateModel(web_contents)) {
  security_state_model_->SetClient(this);
}

ChromeSecurityStateModelClient::~ChromeSecurityStateModelClient() {}

const SecurityStateModel::SecurityInfo&
ChromeSecurityStateModelClient::GetSecurityInfo() const {
  return security_state_model_->GetSecurityInfo();
}

bool ChromeSecurityStateModelClient::RetrieveCert(
    scoped_refptr<net::X509Certificate>* cert) {
  content::NavigationEntry* entry =
      web_contents_->GetController().GetVisibleEntry();
  if (!entry)
    return false;
  return content::CertStore::GetInstance()->RetrieveCert(
      entry->GetSSL().cert_id, cert);
}

bool ChromeSecurityStateModelClient::UsedPolicyInstalledCertificate() {
#if defined(OS_CHROMEOS)
  policy::PolicyCertService* service =
      policy::PolicyCertServiceFactory::GetForProfile(
          Profile::FromBrowserContext(web_contents_->GetBrowserContext()));
  if (service && service->UsedPolicyCertificates())
    return true;
#endif
  return false;
}
