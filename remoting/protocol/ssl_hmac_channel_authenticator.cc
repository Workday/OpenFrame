// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/ssl_hmac_channel_authenticator.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "crypto/secure_util.h"
#include "net/base/host_port_pair.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/x509_certificate.h"
#include "net/http/transport_security_state.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/ssl_server_socket.h"
#include "net/ssl/ssl_config_service.h"
#include "remoting/base/rsa_key_pair.h"
#include "remoting/protocol/auth_util.h"

namespace remoting {
namespace protocol {

// static
scoped_ptr<SslHmacChannelAuthenticator>
SslHmacChannelAuthenticator::CreateForClient(
      const std::string& remote_cert,
      const std::string& auth_key) {
  scoped_ptr<SslHmacChannelAuthenticator> result(
      new SslHmacChannelAuthenticator(auth_key));
  result->remote_cert_ = remote_cert;
  return result.Pass();
}

scoped_ptr<SslHmacChannelAuthenticator>
SslHmacChannelAuthenticator::CreateForHost(
    const std::string& local_cert,
    scoped_refptr<RsaKeyPair> key_pair,
    const std::string& auth_key) {
  scoped_ptr<SslHmacChannelAuthenticator> result(
      new SslHmacChannelAuthenticator(auth_key));
  result->local_cert_ = local_cert;
  result->local_key_pair_ = key_pair;
  return result.Pass();
}

SslHmacChannelAuthenticator::SslHmacChannelAuthenticator(
    const std::string& auth_key)
    : auth_key_(auth_key) {
}

SslHmacChannelAuthenticator::~SslHmacChannelAuthenticator() {
}

void SslHmacChannelAuthenticator::SecureAndAuthenticate(
    scoped_ptr<net::StreamSocket> socket, const DoneCallback& done_callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(socket->IsConnected());

  done_callback_ = done_callback;

  int result;
  if (is_ssl_server()) {
    scoped_refptr<net::X509Certificate> cert =
        net::X509Certificate::CreateFromBytes(
            local_cert_.data(), local_cert_.length());
    if (!cert.get()) {
      LOG(ERROR) << "Failed to parse X509Certificate";
      NotifyError(net::ERR_FAILED);
      return;
    }

    net::SSLConfig ssl_config;
    net::SSLServerSocket* server_socket =
        net::CreateSSLServerSocket(socket.release(),
                                   cert.get(),
                                   local_key_pair_->private_key(),
                                   ssl_config);
    socket_.reset(server_socket);

    result = server_socket->Handshake(base::Bind(
        &SslHmacChannelAuthenticator::OnConnected, base::Unretained(this)));
  } else {
    cert_verifier_.reset(net::CertVerifier::CreateDefault());
    transport_security_state_.reset(new net::TransportSecurityState);

    net::SSLConfig::CertAndStatus cert_and_status;
    cert_and_status.cert_status = net::CERT_STATUS_AUTHORITY_INVALID;
    cert_and_status.der_cert = remote_cert_;

    net::SSLConfig ssl_config;
    // Certificate verification and revocation checking are not needed
    // because we use self-signed certs. Disable it so that the SSL
    // layer doesn't try to initialize OCSP (OCSP works only on the IO
    // thread).
    ssl_config.cert_io_enabled = false;
    ssl_config.rev_checking_enabled = false;
    ssl_config.allowed_bad_certs.push_back(cert_and_status);

    net::HostPortPair host_and_port(kSslFakeHostName, 0);
    net::SSLClientSocketContext context;
    context.cert_verifier = cert_verifier_.get();
    context.transport_security_state = transport_security_state_.get();
    socket_.reset(
        net::ClientSocketFactory::GetDefaultFactory()->CreateSSLClientSocket(
            socket.release(), host_and_port, ssl_config, context));

    result = socket_->Connect(
        base::Bind(&SslHmacChannelAuthenticator::OnConnected,
                   base::Unretained(this)));
  }

  if (result == net::ERR_IO_PENDING)
    return;

  OnConnected(result);
}

bool SslHmacChannelAuthenticator::is_ssl_server() {
  return local_key_pair_.get() != NULL;
}

void SslHmacChannelAuthenticator::OnConnected(int result) {
  if (result != net::OK) {
    LOG(WARNING) << "Failed to establish SSL connection";
    NotifyError(result);
    return;
  }

  // Generate authentication digest to write to the socket.
  std::string auth_bytes = GetAuthBytes(
      socket_.get(), is_ssl_server() ?
      kHostAuthSslExporterLabel : kClientAuthSslExporterLabel, auth_key_);
  if (auth_bytes.empty()) {
    NotifyError(net::ERR_FAILED);
    return;
  }

  // Allocate a buffer to write the digest.
  auth_write_buf_ = new net::DrainableIOBuffer(
      new net::StringIOBuffer(auth_bytes), auth_bytes.size());

  // Read an incoming token.
  auth_read_buf_ = new net::GrowableIOBuffer();
  auth_read_buf_->SetCapacity(kAuthDigestLength);

  // If WriteAuthenticationBytes() results in |done_callback_| being
  // called then we must not do anything else because this object may
  // be destroyed at that point.
  bool callback_called = false;
  WriteAuthenticationBytes(&callback_called);
  if (!callback_called)
    ReadAuthenticationBytes();
}

void SslHmacChannelAuthenticator::WriteAuthenticationBytes(
    bool* callback_called) {
  while (true) {
    int result = socket_->Write(
        auth_write_buf_.get(),
        auth_write_buf_->BytesRemaining(),
        base::Bind(&SslHmacChannelAuthenticator::OnAuthBytesWritten,
                   base::Unretained(this)));
    if (result == net::ERR_IO_PENDING)
      break;
    if (!HandleAuthBytesWritten(result, callback_called))
      break;
  }
}

void SslHmacChannelAuthenticator::OnAuthBytesWritten(int result) {
  DCHECK(CalledOnValidThread());

  if (HandleAuthBytesWritten(result, NULL))
    WriteAuthenticationBytes(NULL);
}

bool SslHmacChannelAuthenticator::HandleAuthBytesWritten(
    int result, bool* callback_called) {
  if (result <= 0) {
    LOG(ERROR) << "Error writing authentication: " << result;
    if (callback_called)
      *callback_called = false;
    NotifyError(result);
    return false;
  }

  auth_write_buf_->DidConsume(result);
  if (auth_write_buf_->BytesRemaining() > 0)
    return true;

  auth_write_buf_ = NULL;
  CheckDone(callback_called);
  return false;
}

void SslHmacChannelAuthenticator::ReadAuthenticationBytes() {
  while (true) {
    int result =
        socket_->Read(auth_read_buf_.get(),
                      auth_read_buf_->RemainingCapacity(),
                      base::Bind(&SslHmacChannelAuthenticator::OnAuthBytesRead,
                                 base::Unretained(this)));
    if (result == net::ERR_IO_PENDING)
      break;
    if (!HandleAuthBytesRead(result))
      break;
  }
}

void SslHmacChannelAuthenticator::OnAuthBytesRead(int result) {
  DCHECK(CalledOnValidThread());

  if (HandleAuthBytesRead(result))
    ReadAuthenticationBytes();
}

bool SslHmacChannelAuthenticator::HandleAuthBytesRead(int read_result) {
  if (read_result <= 0) {
    NotifyError(read_result);
    return false;
  }

  auth_read_buf_->set_offset(auth_read_buf_->offset() + read_result);
  if (auth_read_buf_->RemainingCapacity() > 0)
    return true;

  if (!VerifyAuthBytes(std::string(
          auth_read_buf_->StartOfBuffer(),
          auth_read_buf_->StartOfBuffer() + kAuthDigestLength))) {
    LOG(WARNING) << "Mismatched authentication";
    NotifyError(net::ERR_FAILED);
    return false;
  }

  auth_read_buf_ = NULL;
  CheckDone(NULL);
  return false;
}

bool SslHmacChannelAuthenticator::VerifyAuthBytes(
    const std::string& received_auth_bytes) {
  DCHECK(received_auth_bytes.length() == kAuthDigestLength);

  // Compute expected auth bytes.
  std::string auth_bytes = GetAuthBytes(
      socket_.get(), is_ssl_server() ?
      kClientAuthSslExporterLabel : kHostAuthSslExporterLabel, auth_key_);
  if (auth_bytes.empty())
    return false;

  return crypto::SecureMemEqual(received_auth_bytes.data(),
                                &(auth_bytes[0]), kAuthDigestLength);
}

void SslHmacChannelAuthenticator::CheckDone(bool* callback_called) {
  if (auth_write_buf_.get() == NULL && auth_read_buf_.get() == NULL) {
    DCHECK(socket_.get() != NULL);
    if (callback_called)
      *callback_called = true;
    done_callback_.Run(net::OK, socket_.PassAs<net::StreamSocket>());
  }
}

void SslHmacChannelAuthenticator::NotifyError(int error) {
  done_callback_.Run(static_cast<net::Error>(error),
                     scoped_ptr<net::StreamSocket>());
}

}  // namespace protocol
}  // namespace remoting
