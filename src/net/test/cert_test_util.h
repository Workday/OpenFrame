// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TEST_CERT_TEST_UTIL_H_
#define NET_TEST_CERT_TEST_UTIL_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "net/cert/x509_cert_types.h"
#include "net/cert/x509_certificate.h"

namespace base {
class FilePath;
}

namespace net {

class EVRootCAMetadata;

CertificateList CreateCertificateListFromFile(const base::FilePath& certs_dir,
                                              const std::string& cert_file,
                                              int format);

// Imports a certificate file in the directory net::GetTestCertsDirectory()
// returns.
// |certs_dir| represents the test certificates directory. |cert_file| is the
// name of the certificate file. If cert_file contains multiple certificates,
// the first certificate found will be returned.
scoped_refptr<X509Certificate> ImportCertFromFile(const base::FilePath& certs_dir,
                                                  const std::string& cert_file);

// ScopedTestEVPolicy causes certificates marked with |policy|, issued from a
// root with the given fingerprint, to be treated as EV. |policy| is expressed
// as a string of dotted numbers: i.e. "1.2.3.4".
// This should only be used in unittests as adding a CA twice causes a CHECK
// failure.
class ScopedTestEVPolicy {
 public:
  ScopedTestEVPolicy(EVRootCAMetadata* ev_root_ca_metadata,
                     const SHA1HashValue& fingerprint,
                     const char* policy);
  ~ScopedTestEVPolicy();

 private:
  SHA1HashValue fingerprint_;
  EVRootCAMetadata* const ev_root_ca_metadata_;
};

}  // namespace net

#endif  // NET_TEST_CERT_TEST_UTIL_H_
