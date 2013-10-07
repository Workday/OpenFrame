// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/client_cert_util.h"

#include <cert.h>
#include <pk11pub.h>

#include <list>
#include <string>
#include <vector>

#include "base/values.h"
#include "chromeos/network/certificate_pattern.h"
#include "net/base/net_errors.h"
#include "net/cert/cert_database.h"
#include "net/cert/nss_cert_database.h"
#include "net/cert/x509_cert_types.h"
#include "net/cert/x509_certificate.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace client_cert {

namespace {

// Functor to filter out non-matching issuers.
class IssuerFilter {
 public:
  explicit IssuerFilter(const IssuerSubjectPattern& issuer)
    : issuer_(issuer) {}
  bool operator()(const scoped_refptr<net::X509Certificate>& cert) const {
    return !CertPrincipalMatches(issuer_, cert.get()->issuer());
  }
 private:
  const IssuerSubjectPattern& issuer_;
};

// Functor to filter out non-matching subjects.
class SubjectFilter {
 public:
  explicit SubjectFilter(const IssuerSubjectPattern& subject)
    : subject_(subject) {}
  bool operator()(const scoped_refptr<net::X509Certificate>& cert) const {
    return !CertPrincipalMatches(subject_, cert.get()->subject());
  }
 private:
  const IssuerSubjectPattern& subject_;
};

// Functor to filter out certs that don't have private keys, or are invalid.
class PrivateKeyFilter {
 public:
  explicit PrivateKeyFilter(net::CertDatabase* cert_db) : cert_db_(cert_db) {}
  bool operator()(const scoped_refptr<net::X509Certificate>& cert) const {
    return cert_db_->CheckUserCert(cert.get()) != net::OK;
  }
 private:
  net::CertDatabase* cert_db_;
};

// Functor to filter out certs that don't have an issuer in the associated
// IssuerCAPEMs list.
class IssuerCaFilter {
 public:
  explicit IssuerCaFilter(const std::vector<std::string>& issuer_ca_pems)
    : issuer_ca_pems_(issuer_ca_pems) {}
  bool operator()(const scoped_refptr<net::X509Certificate>& cert) const {
    // Find the certificate issuer for each certificate.
    // TODO(gspencer): this functionality should be available from
    // X509Certificate or NSSCertDatabase.
    CERTCertificate* issuer_cert = CERT_FindCertIssuer(
        cert.get()->os_cert_handle(), PR_Now(), certUsageAnyCA);

    if (!issuer_cert)
      return true;

    std::string pem_encoded;
    if (!net::X509Certificate::GetPEMEncoded(issuer_cert, &pem_encoded)) {
      LOG(ERROR) << "Couldn't PEM-encode certificate.";
      return true;
    }

    return (std::find(issuer_ca_pems_.begin(), issuer_ca_pems_.end(),
                      pem_encoded) ==
            issuer_ca_pems_.end());
  }
 private:
  const std::vector<std::string>& issuer_ca_pems_;
};

}  // namespace

// Returns true only if any fields set in this pattern match exactly with
// similar fields in the principal.  If organization_ or organizational_unit_
// are set, then at least one of the organizations or units in the principal
// must match.
bool CertPrincipalMatches(const IssuerSubjectPattern& pattern,
                          const net::CertPrincipal& principal) {
  if (!pattern.common_name().empty() &&
      pattern.common_name() != principal.common_name) {
    return false;
  }

  if (!pattern.locality().empty() &&
      pattern.locality() != principal.locality_name) {
    return false;
  }

  if (!pattern.organization().empty()) {
    if (std::find(principal.organization_names.begin(),
                  principal.organization_names.end(),
                  pattern.organization()) ==
        principal.organization_names.end()) {
      return false;
    }
  }

  if (!pattern.organizational_unit().empty()) {
    if (std::find(principal.organization_unit_names.begin(),
                  principal.organization_unit_names.end(),
                  pattern.organizational_unit()) ==
        principal.organization_unit_names.end()) {
      return false;
    }
  }

  return true;
}

scoped_refptr<net::X509Certificate> GetCertificateMatch(
    const CertificatePattern& pattern) {
  typedef std::list<scoped_refptr<net::X509Certificate> > CertificateStlList;

  // Start with all the certs, and narrow it down from there.
  net::CertificateList all_certs;
  CertificateStlList matching_certs;
  net::NSSCertDatabase::GetInstance()->ListCerts(&all_certs);

  if (all_certs.empty())
    return NULL;

  for (net::CertificateList::iterator iter = all_certs.begin();
       iter != all_certs.end(); ++iter) {
    matching_certs.push_back(*iter);
  }

  // Strip off any certs that don't have the right issuer and/or subject.
  if (!pattern.issuer().Empty()) {
    matching_certs.remove_if(IssuerFilter(pattern.issuer()));
    if (matching_certs.empty())
      return NULL;
  }

  if (!pattern.subject().Empty()) {
    matching_certs.remove_if(SubjectFilter(pattern.subject()));
    if (matching_certs.empty())
      return NULL;
  }

  if (!pattern.issuer_ca_pems().empty()) {
    matching_certs.remove_if(IssuerCaFilter(pattern.issuer_ca_pems()));
    if (matching_certs.empty())
      return NULL;
  }

  // Eliminate any certs that don't have private keys associated with
  // them.  The CheckUserCert call in the filter is a little slow (because of
  // underlying PKCS11 calls), so we do this last to reduce the number of times
  // we have to call it.
  PrivateKeyFilter private_filter(net::CertDatabase::GetInstance());
  matching_certs.remove_if(private_filter);

  if (matching_certs.empty())
    return NULL;

  // We now have a list of certificates that match the pattern we're
  // looking for.  Now we find the one with the latest start date.
  scoped_refptr<net::X509Certificate> latest(NULL);

  // Iterate over the rest looking for the one that was issued latest.
  for (CertificateStlList::iterator iter = matching_certs.begin();
       iter != matching_certs.end(); ++iter) {
    if (!latest.get() || (*iter)->valid_start() > latest->valid_start())
      latest = *iter;
  }

  return latest;
}

void SetShillProperties(const client_cert::ConfigType cert_config_type,
                        const std::string& tpm_slot,
                        const std::string& tpm_pin,
                        const std::string* pkcs11_id,
                        base::DictionaryValue* properties) {
  const char* tpm_pin_property = NULL;
  switch (cert_config_type) {
    case CONFIG_TYPE_NONE: {
      return;
    }
    case CONFIG_TYPE_OPENVPN: {
      tpm_pin_property = flimflam::kOpenVPNPinProperty;
      if (pkcs11_id) {
        properties->SetStringWithoutPathExpansion(
            flimflam::kOpenVPNClientCertIdProperty, *pkcs11_id);
      }
      break;
    }
    case CONFIG_TYPE_IPSEC: {
      tpm_pin_property = flimflam::kL2tpIpsecPinProperty;
      if (!tpm_slot.empty()) {
        properties->SetStringWithoutPathExpansion(
            flimflam::kL2tpIpsecClientCertSlotProperty, tpm_slot);
      }
      if (pkcs11_id) {
        properties->SetStringWithoutPathExpansion(
            flimflam::kL2tpIpsecClientCertIdProperty, *pkcs11_id);
      }
      break;
    }
    case CONFIG_TYPE_EAP: {
      tpm_pin_property = flimflam::kEapPinProperty;
      if (pkcs11_id) {
        // Shill requires both CertID and KeyID for TLS connections, despite the
        // fact that by convention they are the same ID.
        properties->SetStringWithoutPathExpansion(flimflam::kEapCertIdProperty,
                                                  *pkcs11_id);
        properties->SetStringWithoutPathExpansion(flimflam::kEapKeyIdProperty,
                                                  *pkcs11_id);
      }
      break;
    }
  }
  DCHECK(tpm_pin_property);
  if (!tpm_pin.empty())
    properties->SetStringWithoutPathExpansion(tpm_pin_property, tpm_pin);
}

}  // namespace client_cert

}  // namespace chromeos
