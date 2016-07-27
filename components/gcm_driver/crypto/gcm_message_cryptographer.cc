// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/gcm_driver/crypto/gcm_message_cryptographer.h"

#include <algorithm>
#include <sstream>

#include "base/logging.h"
#include "base/sys_byteorder.h"
#include "crypto/hkdf.h"

namespace gcm {
namespace {

// Size, in bytes, of the nonce for a record. This must be at least the size
// of a uint64_t, which is used to indicate the record sequence number.
const uint64_t kNonceSize = 12;

// The default record size as defined by draft-thomson-http-encryption.
const size_t kDefaultRecordSize = 4096;

// Key size, in bytes, of a valid AEAD_AES_128_GCM key.
const size_t kContentEncryptionKeySize = 16;

// Creates the info parameter for an HKDF value for the given |content_encoding|
// in accordance with draft-thomson-http-encryption.
//
// cek_info = "Content-Encoding: aesgcm128" || 0x00 || context
// nonce_info = "Content-Encoding: nonce" || 0x00 || context
//
// context = label || 0x00 ||
//           length(recipient_public) || recipient_public ||
//           length(sender_public) || sender_public
//
// The length of the public keys must be written as a two octet unsigned integer
// in network byte order (big endian).
std::string InfoForContentEncoding(
    const char* content_encoding,
    GCMMessageCryptographer::Label label,
    const base::StringPiece& recipient_public_key,
    const base::StringPiece& sender_public_key) {
  DCHECK(GCMMessageCryptographer::Label::P256 == label);
  DCHECK_EQ(recipient_public_key.size(), 65u);
  DCHECK_EQ(sender_public_key.size(), 65u);

  std::stringstream info_stream;
  info_stream << "Content-Encoding: " << content_encoding << '\x00';

  switch (label) {
    case GCMMessageCryptographer::Label::P256:
      info_stream << "P-256" << '\x00';
      break;
  }

  uint16_t local_len = base::HostToNet16(recipient_public_key.size());
  info_stream.write(reinterpret_cast<char*>(&local_len), sizeof(local_len));
  info_stream << recipient_public_key;

  uint16_t peer_len = base::HostToNet16(sender_public_key.size());
  info_stream.write(reinterpret_cast<char*>(&peer_len), sizeof(peer_len));
  info_stream << sender_public_key;

  return info_stream.str();
}

}  // namespace

const size_t GCMMessageCryptographer::kAuthenticationTagBytes = 16;
const size_t GCMMessageCryptographer::kSaltSize = 16;

GCMMessageCryptographer::GCMMessageCryptographer(
    Label label,
    const base::StringPiece& recipient_public_key,
    const base::StringPiece& sender_public_key,
    const std::string& auth_secret)
    : content_encryption_key_info_(
          InfoForContentEncoding("aesgcm128", label, recipient_public_key,
                                 sender_public_key)),
      nonce_info_(
          InfoForContentEncoding("nonce", label, recipient_public_key,
                                 sender_public_key)),
      auth_secret_(auth_secret) {
}

GCMMessageCryptographer::~GCMMessageCryptographer() {}

bool GCMMessageCryptographer::Encrypt(const base::StringPiece& plaintext,
                                      const base::StringPiece& key,
                                      const base::StringPiece& salt,
                                      size_t* record_size,
                                      std::string* ciphertext) const {
  DCHECK(ciphertext);
  DCHECK(record_size);

  if (salt.size() != kSaltSize)
    return false;

  std::string ikm = DeriveInputKeyingMaterial(key);

  std::string content_encryption_key = DeriveContentEncryptionKey(ikm, salt);
  std::string nonce = DeriveNonce(ikm, salt);

  // draft-thomson-http-encryption allows between 0 and 255 octets of padding to
  // be inserted before the enciphered content, with the length of the padding
  // stored in the first octet of the payload. Since there is no necessity for
  // payloads to contain padding, don't add any.
  std::string record;
  record.reserve(plaintext.size() + 1);
  record.append(1, '\0');
  plaintext.AppendToString(&record);

  std::string encrypted_record;
  if (!EncryptDecryptRecordInternal(ENCRYPT, record, content_encryption_key,
                                    nonce, &encrypted_record)) {
    return false;
  }

  // The advertised record size must be at least one more than the padded
  // plaintext to ensure only one record.
  *record_size = std::max(kDefaultRecordSize, record.size() + 1);

  ciphertext->swap(encrypted_record);
  return true;
}

bool GCMMessageCryptographer::Decrypt(const base::StringPiece& ciphertext,
                                      const base::StringPiece& key,
                                      const base::StringPiece& salt,
                                      size_t record_size,
                                      std::string* plaintext) const {
  DCHECK(plaintext);

  if (salt.size() != kSaltSize || record_size <= 1)
    return false;

  // The |ciphertext| must be at least kAuthenticationTagBytes + 1 bytes, which
  // would be used for an empty message. Per
  // https://tools.ietf.org/html/draft-thomson-http-encryption-02#section-3, the
  // |record_size| parameter must be large enough to use only one record.
  if (ciphertext.size() < kAuthenticationTagBytes + 1 ||
      ciphertext.size() >= record_size + kAuthenticationTagBytes + 1) {
    return false;
  }

  std::string ikm = DeriveInputKeyingMaterial(key);

  std::string content_encryption_key = DeriveContentEncryptionKey(ikm, salt);
  std::string nonce = DeriveNonce(ikm, salt);

  std::string decrypted_record;
  if (!EncryptDecryptRecordInternal(DECRYPT, ciphertext, content_encryption_key,
                                    nonce, &decrypted_record)) {
    return false;
  }

  DCHECK(!decrypted_record.empty());

  // Records can contain between 0 and 255 octets of padding, indicated by the
  // first octet of the decrypted message. Padding bytes that are not set to
  // zero are considered a fatal decryption failure as well. Since AES-GCM
  // includes an authentication check, neither verification nor removing the
  // padding have to be done in constant time.
  size_t padding_length = static_cast<size_t>(decrypted_record[0]);
  if (padding_length >= decrypted_record.size())
    return false;

  for (size_t i = 1; i <= padding_length; ++i) {
    if (decrypted_record[i] != 0)
      return false;
  }

  base::StringPiece decoded_record_string_piece(decrypted_record);
  decoded_record_string_piece.remove_prefix(1 + padding_length);
  decoded_record_string_piece.CopyToString(plaintext);

  return true;
}

std::string GCMMessageCryptographer::DeriveInputKeyingMaterial(
    const base::StringPiece& key) const {
  if (allow_empty_auth_secret_for_tests_ && auth_secret_.empty())
    return key.as_string();

  CHECK(!auth_secret_.empty());

  crypto::HKDF hkdf(key, auth_secret_,
                    "Content-Encoding: auth",
                    32, /* key_bytes_to_generate */
                    0,  /* iv_bytes_to_generate */
                    0   /* subkey_secret_bytes_to_generate */);

  return hkdf.client_write_key().as_string();
}

std::string GCMMessageCryptographer::DeriveContentEncryptionKey(
    const base::StringPiece& key,
    const base::StringPiece& salt) const {
  crypto::HKDF hkdf(key, salt,
                    content_encryption_key_info_,
                    kContentEncryptionKeySize,
                    0,  /* iv_bytes_to_generate */
                    0   /* subkey_secret_bytes_to_generate */);

  return hkdf.client_write_key().as_string();
}

std::string GCMMessageCryptographer::DeriveNonce(
    const base::StringPiece& key,
    const base::StringPiece& salt) const {
  crypto::HKDF hkdf(key, salt,
                    nonce_info_,
                    kNonceSize,
                    0,  /* iv_bytes_to_generate */
                    0   /* subkey_secret_bytes_to_generate */);

  // draft-thomson-http-encryption defines that the result should be XOR'ed with
  // the record's sequence number, however, Web Push encryption is limited to a
  // single record per draft-ietf-webpush-encryption.

  return hkdf.client_write_key().as_string();
}

}  // namespace gcm
