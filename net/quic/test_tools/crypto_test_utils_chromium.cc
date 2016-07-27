// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/crypto_test_utils.h"

#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/stl_util.h"
#include "base/strings/stringprintf.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/base/test_data_directory.h"
#include "net/cert/cert_status_flags.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/cert_verify_result.h"
#include "net/cert/ct_verifier.h"
#include "net/cert/mock_cert_verifier.h"
#include "net/cert/multi_log_ct_verifier.h"
#include "net/cert/test_root_certs.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"
#include "net/http/transport_security_state.h"
#include "net/log/net_log.h"
#include "net/quic/crypto/crypto_utils.h"
#include "net/quic/crypto/proof_source_chromium.h"
#include "net/quic/crypto/proof_verifier_chromium.h"
#include "net/ssl/ssl_config_service.h"
#include "net/test/cert_test_util.h"

using base::StringPiece;
using base::StringPrintf;
using std::string;
using std::vector;

namespace net {

namespace test {

namespace {

class TestProofVerifierChromium : public ProofVerifierChromium {
 public:
  TestProofVerifierChromium(
      scoped_ptr<CertVerifier> cert_verifier,
      scoped_ptr<TransportSecurityState> transport_security_state,
      scoped_ptr<CTVerifier> cert_transparency_verifier,
      const std::string& cert_file)
      : ProofVerifierChromium(cert_verifier.get(),
                              nullptr,
                              transport_security_state.get(),
                              cert_transparency_verifier.get()),
        cert_verifier_(cert_verifier.Pass()),
        transport_security_state_(transport_security_state.Pass()),
        cert_transparency_verifier_(cert_transparency_verifier.Pass()) {
    // Load and install the root for the validated chain.
    scoped_refptr<X509Certificate> root_cert =
        ImportCertFromFile(GetTestCertsDirectory(), cert_file);
    scoped_root_.Reset(root_cert.get());
  }

  ~TestProofVerifierChromium() override {}

  CertVerifier* cert_verifier() { return cert_verifier_.get(); }

 private:
  ScopedTestRoot scoped_root_;
  scoped_ptr<CertVerifier> cert_verifier_;
  scoped_ptr<TransportSecurityState> transport_security_state_;
  scoped_ptr<CTVerifier> cert_transparency_verifier_;
};

const char kSignature[] = "signature";
const char kSCT[] = "CryptoServerTests";

class FakeProofSource : public ProofSource {
 public:
  FakeProofSource() {}
  ~FakeProofSource() override {}

  // ProofSource interface
  bool Initialize(const base::FilePath& cert_path,
                  const base::FilePath& key_path,
                  const base::FilePath& sct_path) {
    std::string cert_data;
    if (!base::ReadFileToString(cert_path, &cert_data)) {
      DLOG(FATAL) << "Unable to read certificates.";
      return false;
    }

    CertificateList certs_in_file =
        X509Certificate::CreateCertificateListFromBytes(
            cert_data.data(), cert_data.size(), X509Certificate::FORMAT_AUTO);

    if (certs_in_file.empty()) {
      DLOG(FATAL) << "No certificates.";
      return false;
    }

    for (const scoped_refptr<X509Certificate>& cert : certs_in_file) {
      std::string der_encoded_cert;
      if (!X509Certificate::GetDEREncoded(cert->os_cert_handle(),
                                          &der_encoded_cert)) {
        return false;
      }
      certificates_.push_back(der_encoded_cert);
    }
    return true;
  }

  bool GetProof(const IPAddressNumber& server_ip,
                const std::string& hostname,
                const std::string& server_config,
                bool ecdsa_ok,
                const std::vector<std::string>** out_certs,
                std::string* out_signature,
                std::string* out_leaf_cert_sct) override {
    out_signature->assign(kSignature);
    *out_certs = &certificates_;
    *out_leaf_cert_sct = kSCT;
    return true;
  }

 private:
  std::vector<std::string> certificates_;

  DISALLOW_COPY_AND_ASSIGN(FakeProofSource);
};

class FakeProofVerifier : public TestProofVerifierChromium {
 public:
  FakeProofVerifier(scoped_ptr<CertVerifier> cert_verifier,
                    scoped_ptr<TransportSecurityState> transport_security_state,
                    scoped_ptr<CTVerifier> cert_transparency_verifier,
                    const std::string& cert_file)
      : TestProofVerifierChromium(cert_verifier.Pass(),
                                  transport_security_state.Pass(),
                                  cert_transparency_verifier.Pass(),
                                  cert_file) {}
  ~FakeProofVerifier() override {}

  // ProofVerifier interface
  QuicAsyncStatus VerifyProof(const std::string& hostname,
                              const std::string& server_config,
                              const std::vector<std::string>& certs,
                              const std::string& cert_sct,
                              const std::string& signature,
                              const ProofVerifyContext* verify_context,
                              std::string* error_details,
                              scoped_ptr<ProofVerifyDetails>* verify_details,
                              ProofVerifierCallback* callback) override {
    error_details->clear();
    scoped_ptr<ProofVerifyDetailsChromium> verify_details_chromium(
        new ProofVerifyDetailsChromium);
    DCHECK(!certs.empty());
    // Convert certs to X509Certificate.
    vector<StringPiece> cert_pieces(certs.size());
    for (unsigned i = 0; i < certs.size(); i++) {
      cert_pieces[i] = base::StringPiece(certs[i]);
    }
    scoped_refptr<X509Certificate> x509_cert =
        X509Certificate::CreateFromDERCertChain(cert_pieces);

    if (!x509_cert.get()) {
      *error_details = "Failed to create certificate chain";
      verify_details_chromium->cert_verify_result.cert_status =
          CERT_STATUS_INVALID;
      *verify_details = verify_details_chromium.Pass();
      return QUIC_FAILURE;
    }

    const ProofVerifyContextChromium* chromium_context =
        reinterpret_cast<const ProofVerifyContextChromium*>(verify_context);
    scoped_ptr<CertVerifier::Request> cert_verifier_request_;
    TestCompletionCallback test_callback;
    int result = cert_verifier()->Verify(
        x509_cert.get(), hostname, std::string(),
        chromium_context->cert_verify_flags,
        SSLConfigService::GetCRLSet().get(),
        &verify_details_chromium->cert_verify_result, test_callback.callback(),
        &cert_verifier_request_, chromium_context->net_log);
    if (result != OK) {
      std::string error_string = ErrorToString(result);
      *error_details = StringPrintf("Failed to verify certificate chain: %s",
                                    error_string.c_str());
      verify_details_chromium->cert_verify_result.cert_status =
          CERT_STATUS_INVALID;
      *verify_details = verify_details_chromium.Pass();
      return QUIC_FAILURE;
    }
    if (signature != kSignature) {
      *error_details = "Invalid proof";
      verify_details_chromium->cert_verify_result.cert_status =
          CERT_STATUS_INVALID;
      *verify_details = verify_details_chromium.Pass();
      return QUIC_FAILURE;
    }
    *verify_details = verify_details_chromium.Pass();
    return QUIC_SUCCESS;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeProofVerifier);
};

}  // namespace

// static
ProofSource* CryptoTestUtils::ProofSourceForTesting() {
#if defined(USE_OPENSSL)
  ProofSourceChromium* source = new ProofSourceChromium();
#else
  FakeProofSource* source = new FakeProofSource();
#endif
  base::FilePath certs_dir = GetTestCertsDirectory();
  CHECK(source->Initialize(
      certs_dir.AppendASCII("quic_test.example.com.crt"),
      certs_dir.AppendASCII("quic_test.example.com.key.pkcs8"),
      certs_dir.AppendASCII("quic_test.example.com.key.sct")));
  return source;
}

// static
ProofVerifier* ProofVerifierForTestingInternal(bool use_real_proof_verifier) {
  // TODO(rch): use a real cert verifier?
  scoped_ptr<MockCertVerifier> cert_verifier(new MockCertVerifier());
  net::CertVerifyResult verify_result;
  verify_result.verified_cert =
      ImportCertFromFile(GetTestCertsDirectory(), "quic_test.example.com.crt");
  cert_verifier->AddResultForCertAndHost(verify_result.verified_cert.get(),
                                         "test.example.com", verify_result, OK);
  verify_result.verified_cert = ImportCertFromFile(
      GetTestCertsDirectory(), "quic_test_ecc.example.com.crt");
  cert_verifier->AddResultForCertAndHost(verify_result.verified_cert.get(),
                                         "test.example.com", verify_result, OK);
  if (use_real_proof_verifier) {
    return new TestProofVerifierChromium(
        cert_verifier.Pass(), make_scoped_ptr(new TransportSecurityState),
        make_scoped_ptr(new MultiLogCTVerifier), "quic_root.crt");
  }
#if defined(USE_OPENSSL)
  return new TestProofVerifierChromium(
      cert_verifier.Pass(), make_scoped_ptr(new TransportSecurityState),
      make_scoped_ptr(new MultiLogCTVerifier), "quic_root.crt");
#else
  return new FakeProofVerifier(
      cert_verifier.Pass(), make_scoped_ptr(new TransportSecurityState),
      make_scoped_ptr(new MultiLogCTVerifier), "quic_root.crt");
#endif
}

// static
ProofVerifier* CryptoTestUtils::ProofVerifierForTesting() {
  return ProofVerifierForTestingInternal(/*use_real_proof_verifier=*/false);
}

// static
ProofVerifier* CryptoTestUtils::RealProofVerifierForTesting() {
  return ProofVerifierForTestingInternal(/*use_real_proof_verifier=*/true);
}

// static
ProofVerifyContext* CryptoTestUtils::ProofVerifyContextForTesting() {
  return new ProofVerifyContextChromium(/*cert_verify_flags=*/0, BoundNetLog());
}

}  // namespace test

}  // namespace net
