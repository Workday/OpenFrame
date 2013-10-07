// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/x509_util.h"
#include "net/cert/x509_util_openssl.h"

#include <algorithm>

#include "base/logging.h"
#include "base/strings/string_piece.h"
#include "net/cert/x509_cert_types.h"

namespace net {

namespace x509_util {

bool IsSupportedValidityRange(base::Time not_valid_before,
                              base::Time not_valid_after) {
  if (not_valid_before > not_valid_after)
    return false;

  // The validity field of a certificate can only encode years 1-9999.

  // Compute the base::Time values corresponding to Jan 1st,0001 and
  // Jan 1st, 10000 respectively. Done by using the pre-computed numbers
  // of days between these dates and the Unix epoch, i.e. Jan 1st, 1970,
  // using the following Python script:
  //
  //     from datetime import date as D
  //     print (D(1970,1,1)-D(1,1,1))        # -> 719162 days
  //     print (D(9999,12,31)-D(1970,1,1))   # -> 2932896 days
  //
  // Note: This ignores leap seconds, but should be enough in practice.
  //
  const int64 kDaysFromYear0001ToUnixEpoch = 719162;
  const int64 kDaysFromUnixEpochToYear10000 = 2932896 + 1;
  const base::Time kEpoch = base::Time::UnixEpoch();
  const base::Time kYear0001 = kEpoch -
      base::TimeDelta::FromDays(kDaysFromYear0001ToUnixEpoch);
  const base::Time kYear10000 = kEpoch +
      base::TimeDelta::FromDays(kDaysFromUnixEpochToYear10000);

  if (not_valid_before < kYear0001 || not_valid_before >= kYear10000 ||
      not_valid_after < kYear0001 || not_valid_after >= kYear10000)
    return false;

  return true;
}

bool CreateDomainBoundCertEC(
    crypto::ECPrivateKey* key,
    const std::string& domain,
    uint32 serial_number,
    base::Time not_valid_before,
    base::Time not_valid_after,
    std::string* der_cert) {
  NOTIMPLEMENTED();
  return false;
}

bool CreateSelfSignedCert(crypto::RSAPrivateKey* key,
                          const std::string& common_name,
                          uint32 serial_number,
                          base::Time not_valid_before,
                          base::Time not_valid_after,
                          std::string* der_encoded) {
  NOTIMPLEMENTED();
  return false;
}

bool ParsePrincipalKeyAndValueByIndex(X509_NAME* name,
                                      int index,
                                      std::string* key,
                                      std::string* value) {
  X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, index);
  if (!entry)
    return false;

  if (key) {
    ASN1_OBJECT* object = X509_NAME_ENTRY_get_object(entry);
    key->assign(OBJ_nid2sn(OBJ_obj2nid(object)));
  }

  ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
  if (!data)
    return false;

  unsigned char* buf = NULL;
  int len = ASN1_STRING_to_UTF8(&buf, data);
  if (len <= 0)
    return false;

  value->assign(reinterpret_cast<const char*>(buf), len);
  OPENSSL_free(buf);
  return true;
}

bool ParsePrincipalValueByIndex(X509_NAME* name,
                                int index,
                                std::string* value) {
  return ParsePrincipalKeyAndValueByIndex(name, index, NULL, value);
}

bool ParsePrincipalValueByNID(X509_NAME* name, int nid, std::string* value) {
  int index = X509_NAME_get_index_by_NID(name, nid, -1);
  if (index < 0)
    return false;

  return ParsePrincipalValueByIndex(name, index, value);
}

bool ParseDate(ASN1_TIME* x509_time, base::Time* time) {
  if (!x509_time ||
      (x509_time->type != V_ASN1_UTCTIME &&
       x509_time->type != V_ASN1_GENERALIZEDTIME))
    return false;

  base::StringPiece str_date(reinterpret_cast<const char*>(x509_time->data),
                             x509_time->length);

  CertDateFormat format = x509_time->type == V_ASN1_UTCTIME ?
      CERT_DATE_FORMAT_UTC_TIME : CERT_DATE_FORMAT_GENERALIZED_TIME;
  return ParseCertificateDate(str_date, format, time);
}

}  // namespace x509_util

}  // namespace net
