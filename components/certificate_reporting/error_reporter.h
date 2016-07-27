// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_CERTIFICATE_REPORTING_CERTIFICATE_ERROR_REPORTER_H_
#define COMPONENTS_CERTIFICATE_REPORTING_CERTIFICATE_ERROR_REPORTER_H_

#include <set>
#include <string>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "net/url_request/certificate_report_sender.h"
#include "url/gurl.h"

namespace net {
class URLRequestContext;
class SSLInfo;
}

namespace certificate_reporting {

class EncryptedCertLoggerRequest;

// Provides functionality for sending reports about invalid SSL
// certificate chains to a report collection server.
class ErrorReporter {
 public:
  // Creates a certificate error reporter that will send certificate
  // error reports to |upload_url|, using |request_context| as the
  // context for the reports. |cookies_preference| controls whether
  // cookies will be sent along with the reports.
  ErrorReporter(
      net::URLRequestContext* request_context,
      const GURL& upload_url,
      net::CertificateReportSender::CookiesPreference cookies_preference);

  // Allows tests to use a server public key with known private key and
  // a mock CertificateReportSender. |server_public_key| must outlive
  // the ErrorReporter.
  ErrorReporter(
      const GURL& upload_url,
      const uint8 server_public_key[/* 32 */],
      const uint32 server_public_key_version,
      scoped_ptr<net::CertificateReportSender> certificate_report_sender);

  virtual ~ErrorReporter();

  // Sends a certificate report to the report collection server. The
  // |serialized_report| is expected to be a serialized protobuf
  // containing information about the hostname, certificate chain, and
  // certificate errors encountered when validating the chain.
  //
  // |SendReport| actually sends the report over the network; callers are
  // responsible for enforcing any preconditions (such as obtaining user
  // opt-in, only sending reports for certain hostnames, checking for
  // incognito mode, etc.).
  //
  // On some platforms (but not all), ErrorReporter can use
  // an HTTP endpoint to send encrypted extended reporting reports. On
  // unsupported platforms, callers must send extended reporting reports
  // over SSL.
  virtual void SendExtendedReportingReport(
      const std::string& serialized_report);

  // Whether sending reports over HTTP is supported.
  static bool IsHttpUploadUrlSupported();

#if defined(USE_OPENSSL)
  // Used by tests.
  static bool DecryptErrorReport(
      const uint8 server_private_key[32],
      const EncryptedCertLoggerRequest& encrypted_report,
      std::string* decrypted_serialized_report);
#endif

 private:
  scoped_ptr<net::CertificateReportSender> certificate_report_sender_;

  const GURL upload_url_;

  const uint8* server_public_key_;
  const uint32 server_public_key_version_;

  DISALLOW_COPY_AND_ASSIGN(ErrorReporter);
};

}  // namespace certificate_reporting

#endif  // COMPONENTS_CERTIFICATE_REPORTING_CERTIFICATE_ERROR_REPORTER_H_
