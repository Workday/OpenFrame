// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ssl/ssl_error_info.h"

#include "base/i18n/time_formatting.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/cert_store.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "net/base/escape.h"
#include "net/base/net_errors.h"
#include "net/cert/cert_status_flags.h"
#include "net/ssl/ssl_info.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

SSLErrorInfo::SSLErrorInfo(const string16& title,
                           const string16& details,
                           const string16& short_description,
                           const std::vector<string16>& extra_info)
    : title_(title),
      details_(details),
      short_description_(short_description),
      extra_information_(extra_info) {
}

// static
SSLErrorInfo SSLErrorInfo::CreateError(ErrorType error_type,
                                       net::X509Certificate* cert,
                                       const GURL& request_url) {
  string16 title, details, short_description;
  std::vector<string16> extra_info;
  switch (error_type) {
    case CERT_COMMON_NAME_INVALID: {
      title =
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_COMMON_NAME_INVALID_TITLE);
      // If the certificate contains multiple DNS names, we choose the most
      // representative one -- either the DNS name that's also in the subject
      // field, or the first one.  If this heuristic turns out to be
      // inadequate, we can consider choosing the DNS name that is the
      // "closest match" to the host name in the request URL, or listing all
      // the DNS names with an HTML <ul>.
      std::vector<std::string> dns_names;
      cert->GetDNSNames(&dns_names);
      DCHECK(!dns_names.empty());
      size_t i = 0;
      for (; i < dns_names.size(); ++i) {
        if (dns_names[i] == cert->subject().common_name)
          break;
      }
      if (i == dns_names.size())
        i = 0;
      details =
          l10n_util::GetStringFUTF16(IDS_CERT_ERROR_COMMON_NAME_INVALID_DETAILS,
                                     UTF8ToUTF16(request_url.host()),
                                     net::EscapeForHTML(
                                         UTF8ToUTF16(dns_names[i])),
                                     UTF8ToUTF16(request_url.host()));
      short_description = l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_COMMON_NAME_INVALID_DESCRIPTION);
      extra_info.push_back(
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_EXTRA_INFO_1));
      extra_info.push_back(
          l10n_util::GetStringFUTF16(
              IDS_CERT_ERROR_COMMON_NAME_INVALID_EXTRA_INFO_2,
              net::EscapeForHTML(UTF8ToUTF16(cert->subject().common_name)),
              UTF8ToUTF16(request_url.host())));
      break;
    }
    case CERT_DATE_INVALID:
      extra_info.push_back(
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_EXTRA_INFO_1));
      if (cert->HasExpired()) {
        title = l10n_util::GetStringUTF16(IDS_CERT_ERROR_EXPIRED_TITLE);
        details = l10n_util::GetStringFUTF16(
            IDS_CERT_ERROR_EXPIRED_DETAILS,
            UTF8ToUTF16(request_url.host()),
            UTF8ToUTF16(request_url.host()),
            base::TimeFormatFriendlyDateAndTime(base::Time::Now()));
        short_description =
            l10n_util::GetStringUTF16(IDS_CERT_ERROR_EXPIRED_DESCRIPTION);
        extra_info.push_back(l10n_util::GetStringUTF16(
            IDS_CERT_ERROR_EXPIRED_DETAILS_EXTRA_INFO_2));
      } else {
        // Then it must be not yet valid.  We don't check that it is not yet
        // valid as there is still a very unlikely chance that the cert might
        // have become valid since the error occurred.
        title = l10n_util::GetStringUTF16(IDS_CERT_ERROR_NOT_YET_VALID_TITLE);
        details = l10n_util::GetStringFUTF16(
            IDS_CERT_ERROR_NOT_YET_VALID_DETAILS,
            UTF8ToUTF16(request_url.host()),
            UTF8ToUTF16(request_url.host()),
            base::TimeFormatFriendlyDateAndTime(base::Time::Now()));
        short_description =
            l10n_util::GetStringUTF16(IDS_CERT_ERROR_NOT_YET_VALID_DESCRIPTION);
        extra_info.push_back(
            l10n_util::GetStringUTF16(
                IDS_CERT_ERROR_NOT_YET_VALID_DETAILS_EXTRA_INFO_2));
      }
      break;
    case CERT_AUTHORITY_INVALID:
      title = l10n_util::GetStringUTF16(IDS_CERT_ERROR_AUTHORITY_INVALID_TITLE);
      details = l10n_util::GetStringFUTF16(
          IDS_CERT_ERROR_AUTHORITY_INVALID_DETAILS,
          UTF8ToUTF16(request_url.host()));
      short_description = l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_AUTHORITY_INVALID_DESCRIPTION);
      extra_info.push_back(
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_EXTRA_INFO_1));
      extra_info.push_back(l10n_util::GetStringFUTF16(
          IDS_CERT_ERROR_AUTHORITY_INVALID_EXTRA_INFO_2,
          UTF8ToUTF16(request_url.host()),
          UTF8ToUTF16(request_url.host())));
#if !defined(OS_IOS)
      // The third paragraph advises users to install a private trust anchor,
      // but that is not possible in Chrome for iOS at this time.
      extra_info.push_back(l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_AUTHORITY_INVALID_EXTRA_INFO_3));
#endif
      break;
    case CERT_CONTAINS_ERRORS:
      title = l10n_util::GetStringUTF16(IDS_CERT_ERROR_CONTAINS_ERRORS_TITLE);
      details = l10n_util::GetStringFUTF16(
          IDS_CERT_ERROR_CONTAINS_ERRORS_DETAILS,
          UTF8ToUTF16(request_url.host()));
      short_description =
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_CONTAINS_ERRORS_DESCRIPTION);
      extra_info.push_back(
          l10n_util::GetStringFUTF16(IDS_CERT_ERROR_EXTRA_INFO_1,
                                     UTF8ToUTF16(request_url.host())));
      extra_info.push_back(l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_CONTAINS_ERRORS_EXTRA_INFO_2));
      break;
    case CERT_NO_REVOCATION_MECHANISM:
      title = l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_NO_REVOCATION_MECHANISM_TITLE);
      details = l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_NO_REVOCATION_MECHANISM_DETAILS);
      short_description = l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_NO_REVOCATION_MECHANISM_DESCRIPTION);
      break;
    case CERT_UNABLE_TO_CHECK_REVOCATION:
      title = l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_UNABLE_TO_CHECK_REVOCATION_TITLE);
      details = l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_UNABLE_TO_CHECK_REVOCATION_DETAILS);
      short_description = l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_UNABLE_TO_CHECK_REVOCATION_DESCRIPTION);
      break;
    case CERT_REVOKED:
      title = l10n_util::GetStringUTF16(IDS_CERT_ERROR_REVOKED_CERT_TITLE);
      details = l10n_util::GetStringFUTF16(IDS_CERT_ERROR_REVOKED_CERT_DETAILS,
                                           UTF8ToUTF16(request_url.host()));
      short_description =
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_REVOKED_CERT_DESCRIPTION);
      extra_info.push_back(
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_EXTRA_INFO_1));
      extra_info.push_back(
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_REVOKED_CERT_EXTRA_INFO_2));
      break;
    case CERT_INVALID:
      title = l10n_util::GetStringUTF16(IDS_CERT_ERROR_INVALID_CERT_TITLE);
      details = l10n_util::GetStringFUTF16(
          IDS_CERT_ERROR_INVALID_CERT_DETAILS,
          UTF8ToUTF16(request_url.host()));
      short_description =
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_INVALID_CERT_DESCRIPTION);
      extra_info.push_back(
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_EXTRA_INFO_1));
      extra_info.push_back(l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_INVALID_CERT_EXTRA_INFO_2));
      break;
    case CERT_WEAK_SIGNATURE_ALGORITHM:
      title = l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_WEAK_SIGNATURE_ALGORITHM_TITLE);
      details = l10n_util::GetStringFUTF16(
          IDS_CERT_ERROR_WEAK_SIGNATURE_ALGORITHM_DETAILS,
          UTF8ToUTF16(request_url.host()));
      short_description = l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_WEAK_SIGNATURE_ALGORITHM_DESCRIPTION);
      extra_info.push_back(
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_EXTRA_INFO_1));
      extra_info.push_back(
          l10n_util::GetStringUTF16(
              IDS_CERT_ERROR_WEAK_SIGNATURE_ALGORITHM_EXTRA_INFO_2));
      break;
    case CERT_WEAK_KEY:
      title = l10n_util::GetStringUTF16(IDS_CERT_ERROR_WEAK_KEY_TITLE);
      details = l10n_util::GetStringFUTF16(
          IDS_CERT_ERROR_WEAK_KEY_DETAILS, UTF8ToUTF16(request_url.host()));
      short_description = l10n_util::GetStringUTF16(
          IDS_CERT_ERROR_WEAK_KEY_DESCRIPTION);
      extra_info.push_back(
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_EXTRA_INFO_1));
      extra_info.push_back(
          l10n_util::GetStringUTF16(
              IDS_CERT_ERROR_WEAK_KEY_EXTRA_INFO_2));
      break;
    case UNKNOWN:
      title = l10n_util::GetStringUTF16(IDS_CERT_ERROR_UNKNOWN_ERROR_TITLE);
      details = l10n_util::GetStringUTF16(IDS_CERT_ERROR_UNKNOWN_ERROR_DETAILS);
      short_description =
          l10n_util::GetStringUTF16(IDS_CERT_ERROR_UNKNOWN_ERROR_DESCRIPTION);
      break;
    default:
      NOTREACHED();
  }
  return SSLErrorInfo(title, details, short_description, extra_info);
}

SSLErrorInfo::~SSLErrorInfo() {
}

// static
SSLErrorInfo::ErrorType SSLErrorInfo::NetErrorToErrorType(int net_error) {
  switch (net_error) {
    case net::ERR_CERT_COMMON_NAME_INVALID:
      return CERT_COMMON_NAME_INVALID;
    case net::ERR_CERT_DATE_INVALID:
      return CERT_DATE_INVALID;
    case net::ERR_CERT_AUTHORITY_INVALID:
      return CERT_AUTHORITY_INVALID;
    case net::ERR_CERT_CONTAINS_ERRORS:
      return CERT_CONTAINS_ERRORS;
    case net::ERR_CERT_NO_REVOCATION_MECHANISM:
      return CERT_NO_REVOCATION_MECHANISM;
    case net::ERR_CERT_UNABLE_TO_CHECK_REVOCATION:
      return CERT_UNABLE_TO_CHECK_REVOCATION;
    case net::ERR_CERT_REVOKED:
      return CERT_REVOKED;
    case net::ERR_CERT_INVALID:
      return CERT_INVALID;
    case net::ERR_CERT_WEAK_SIGNATURE_ALGORITHM:
      return CERT_WEAK_SIGNATURE_ALGORITHM;
    case net::ERR_CERT_WEAK_KEY:
      return CERT_WEAK_KEY;
    default:
      NOTREACHED();
      return UNKNOWN;
    }
}

// static
int SSLErrorInfo::GetErrorsForCertStatus(int cert_id,
                                         net::CertStatus cert_status,
                                         const GURL& url,
                                         std::vector<SSLErrorInfo>* errors) {
  const net::CertStatus kErrorFlags[] = {
    net::CERT_STATUS_COMMON_NAME_INVALID,
    net::CERT_STATUS_DATE_INVALID,
    net::CERT_STATUS_AUTHORITY_INVALID,
    net::CERT_STATUS_NO_REVOCATION_MECHANISM,
    net::CERT_STATUS_UNABLE_TO_CHECK_REVOCATION,
    net::CERT_STATUS_REVOKED,
    net::CERT_STATUS_INVALID,
    net::CERT_STATUS_WEAK_SIGNATURE_ALGORITHM,
    net::CERT_STATUS_WEAK_KEY
  };

  const ErrorType kErrorTypes[] = {
    CERT_COMMON_NAME_INVALID,
    CERT_DATE_INVALID,
    CERT_AUTHORITY_INVALID,
    CERT_NO_REVOCATION_MECHANISM,
    CERT_UNABLE_TO_CHECK_REVOCATION,
    CERT_REVOKED,
    CERT_INVALID,
    CERT_WEAK_SIGNATURE_ALGORITHM,
    CERT_WEAK_KEY
  };
  DCHECK(arraysize(kErrorFlags) == arraysize(kErrorTypes));

  scoped_refptr<net::X509Certificate> cert = NULL;
  int count = 0;
  for (size_t i = 0; i < arraysize(kErrorFlags); ++i) {
    if (cert_status & kErrorFlags[i]) {
      count++;
      if (!cert.get()) {
        bool r = content::CertStore::GetInstance()->RetrieveCert(
            cert_id, &cert);
        DCHECK(r);
      }
      if (errors)
        errors->push_back(
            SSLErrorInfo::CreateError(kErrorTypes[i], cert.get(), url));
    }
  }
  return count;
}
