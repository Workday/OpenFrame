// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cdm/aes_decryptor.h"

#include <vector>

#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/encryptor.h"
#include "crypto/symmetric_key.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/decoder_buffer.h"
#include "media/base/decrypt_config.h"
#include "media/base/video_decoder_config.h"
#include "media/base/video_frame.h"

namespace media {

uint32 AesDecryptor::next_session_id_ = 1;

enum ClearBytesBufferSel {
  kSrcContainsClearBytes,
  kDstContainsClearBytes
};

static void CopySubsamples(const std::vector<SubsampleEntry>& subsamples,
                           const ClearBytesBufferSel sel,
                           const uint8* src,
                           uint8* dst) {
  for (size_t i = 0; i < subsamples.size(); i++) {
    const SubsampleEntry& subsample = subsamples[i];
    if (sel == kSrcContainsClearBytes) {
      src += subsample.clear_bytes;
    } else {
      dst += subsample.clear_bytes;
    }
    memcpy(dst, src, subsample.cypher_bytes);
    src += subsample.cypher_bytes;
    dst += subsample.cypher_bytes;
  }
}

// Decrypts |input| using |key|.  Returns a DecoderBuffer with the decrypted
// data if decryption succeeded or NULL if decryption failed.
static scoped_refptr<DecoderBuffer> DecryptData(const DecoderBuffer& input,
                                                crypto::SymmetricKey* key) {
  CHECK(input.data_size());
  CHECK(input.decrypt_config());
  CHECK(key);

  crypto::Encryptor encryptor;
  if (!encryptor.Init(key, crypto::Encryptor::CTR, "")) {
    DVLOG(1) << "Could not initialize decryptor.";
    return NULL;
  }

  DCHECK_EQ(input.decrypt_config()->iv().size(),
            static_cast<size_t>(DecryptConfig::kDecryptionKeySize));
  if (!encryptor.SetCounter(input.decrypt_config()->iv())) {
    DVLOG(1) << "Could not set counter block.";
    return NULL;
  }

  const int data_offset = input.decrypt_config()->data_offset();
  const char* sample =
      reinterpret_cast<const char*>(input.data() + data_offset);
  int sample_size = input.data_size() - data_offset;

  DCHECK_GT(sample_size, 0) << "No sample data to be decrypted.";
  if (sample_size <= 0)
    return NULL;

  if (input.decrypt_config()->subsamples().empty()) {
    std::string decrypted_text;
    base::StringPiece encrypted_text(sample, sample_size);
    if (!encryptor.Decrypt(encrypted_text, &decrypted_text)) {
      DVLOG(1) << "Could not decrypt data.";
      return NULL;
    }

    // TODO(xhwang): Find a way to avoid this data copy.
    return DecoderBuffer::CopyFrom(
        reinterpret_cast<const uint8*>(decrypted_text.data()),
        decrypted_text.size());
  }

  const std::vector<SubsampleEntry>& subsamples =
      input.decrypt_config()->subsamples();

  int total_clear_size = 0;
  int total_encrypted_size = 0;
  for (size_t i = 0; i < subsamples.size(); i++) {
    total_clear_size += subsamples[i].clear_bytes;
    total_encrypted_size += subsamples[i].cypher_bytes;
  }
  if (total_clear_size + total_encrypted_size != sample_size) {
    DVLOG(1) << "Subsample sizes do not equal input size";
    return NULL;
  }

  // No need to decrypt if there is no encrypted data.
  if (total_encrypted_size <= 0) {
    return DecoderBuffer::CopyFrom(reinterpret_cast<const uint8*>(sample),
                                   sample_size);
  }

  // The encrypted portions of all subsamples must form a contiguous block,
  // such that an encrypted subsample that ends away from a block boundary is
  // immediately followed by the start of the next encrypted subsample. We
  // copy all encrypted subsamples to a contiguous buffer, decrypt them, then
  // copy the decrypted bytes over the encrypted bytes in the output.
  // TODO(strobe): attempt to reduce number of memory copies
  scoped_ptr<uint8[]> encrypted_bytes(new uint8[total_encrypted_size]);
  CopySubsamples(subsamples, kSrcContainsClearBytes,
                 reinterpret_cast<const uint8*>(sample), encrypted_bytes.get());

  base::StringPiece encrypted_text(
      reinterpret_cast<const char*>(encrypted_bytes.get()),
      total_encrypted_size);
  std::string decrypted_text;
  if (!encryptor.Decrypt(encrypted_text, &decrypted_text)) {
    DVLOG(1) << "Could not decrypt data.";
    return NULL;
  }
  DCHECK_EQ(decrypted_text.size(), encrypted_text.size());

  scoped_refptr<DecoderBuffer> output = DecoderBuffer::CopyFrom(
      reinterpret_cast<const uint8*>(sample), sample_size);
  CopySubsamples(subsamples, kDstContainsClearBytes,
                 reinterpret_cast<const uint8*>(decrypted_text.data()),
                 output->writable_data());
  return output;
}

AesDecryptor::AesDecryptor(const KeyAddedCB& key_added_cb,
                           const KeyErrorCB& key_error_cb,
                           const KeyMessageCB& key_message_cb)
    : key_added_cb_(key_added_cb),
      key_error_cb_(key_error_cb),
      key_message_cb_(key_message_cb) {
}

AesDecryptor::~AesDecryptor() {
  STLDeleteValues(&key_map_);
}

bool AesDecryptor::GenerateKeyRequest(const std::string& type,
                                      const uint8* init_data,
                                      int init_data_length) {
  std::string session_id_string(base::UintToString(next_session_id_++));

  // For now, the AesDecryptor does not care about |type|;
  // just fire the event with the |init_data| as the request.
  std::vector<uint8> message;
  if (init_data && init_data_length)
    message.assign(init_data, init_data + init_data_length);

  key_message_cb_.Run(session_id_string, message, std::string());
  return true;
}

void AesDecryptor::AddKey(const uint8* key,
                          int key_length,
                          const uint8* init_data,
                          int init_data_length,
                          const std::string& session_id) {
  CHECK(key);
  CHECK_GT(key_length, 0);

  // TODO(xhwang): Add |session_id| check after we figure out how:
  // https://www.w3.org/Bugs/Public/show_bug.cgi?id=16550
  if (key_length != DecryptConfig::kDecryptionKeySize) {
    DVLOG(1) << "Invalid key length: " << key_length;
    key_error_cb_.Run(session_id, MediaKeys::kUnknownError, 0);
    return;
  }

  // TODO(xhwang): Fix the decryptor to accept no |init_data|. See
  // http://crbug.com/123265. Until then, ensure a non-empty value is passed.
  static const uint8 kDummyInitData[1] = { 0 };
  if (!init_data) {
    init_data = kDummyInitData;
    init_data_length = arraysize(kDummyInitData);
  }

  // TODO(xhwang): For now, use |init_data| for key ID. Make this more spec
  // compliant later (http://crbug.com/123262, http://crbug.com/123265).
  std::string key_id_string(reinterpret_cast<const char*>(init_data),
                            init_data_length);
  std::string key_string(reinterpret_cast<const char*>(key) , key_length);
  scoped_ptr<DecryptionKey> decryption_key(new DecryptionKey(key_string));
  if (!decryption_key) {
    DVLOG(1) << "Could not create key.";
    key_error_cb_.Run(session_id, MediaKeys::kUnknownError, 0);
    return;
  }

  if (!decryption_key->Init()) {
    DVLOG(1) << "Could not initialize decryption key.";
    key_error_cb_.Run(session_id, MediaKeys::kUnknownError, 0);
    return;
  }

  SetKey(key_id_string, decryption_key.Pass());

  if (!new_audio_key_cb_.is_null())
    new_audio_key_cb_.Run();

  if (!new_video_key_cb_.is_null())
    new_video_key_cb_.Run();

  key_added_cb_.Run(session_id);
}

void AesDecryptor::CancelKeyRequest(const std::string& session_id) {
}

Decryptor* AesDecryptor::GetDecryptor() {
  return this;
}

void AesDecryptor::RegisterNewKeyCB(StreamType stream_type,
                                    const NewKeyCB& new_key_cb) {
  switch (stream_type) {
    case kAudio:
      new_audio_key_cb_ = new_key_cb;
      break;
    case kVideo:
      new_video_key_cb_ = new_key_cb;
      break;
    default:
      NOTREACHED();
  }
}

void AesDecryptor::Decrypt(StreamType stream_type,
                           const scoped_refptr<DecoderBuffer>& encrypted,
                           const DecryptCB& decrypt_cb) {
  CHECK(encrypted->decrypt_config());

  scoped_refptr<DecoderBuffer> decrypted;
  // An empty iv string signals that the frame is unencrypted.
  if (encrypted->decrypt_config()->iv().empty()) {
    int data_offset = encrypted->decrypt_config()->data_offset();
    decrypted = DecoderBuffer::CopyFrom(encrypted->data() + data_offset,
                                        encrypted->data_size() - data_offset);
  } else {
    const std::string& key_id = encrypted->decrypt_config()->key_id();
    DecryptionKey* key = GetKey(key_id);
    if (!key) {
      DVLOG(1) << "Could not find a matching key for the given key ID.";
      decrypt_cb.Run(kNoKey, NULL);
      return;
    }

    crypto::SymmetricKey* decryption_key = key->decryption_key();
    decrypted = DecryptData(*encrypted.get(), decryption_key);
    if (!decrypted.get()) {
      DVLOG(1) << "Decryption failed.";
      decrypt_cb.Run(kError, NULL);
      return;
    }
  }

  decrypted->set_timestamp(encrypted->timestamp());
  decrypted->set_duration(encrypted->duration());
  decrypt_cb.Run(kSuccess, decrypted);
}

void AesDecryptor::CancelDecrypt(StreamType stream_type) {
  // Decrypt() calls the DecryptCB synchronously so there's nothing to cancel.
}

void AesDecryptor::InitializeAudioDecoder(const AudioDecoderConfig& config,
                                          const DecoderInitCB& init_cb) {
  // AesDecryptor does not support audio decoding.
  init_cb.Run(false);
}

void AesDecryptor::InitializeVideoDecoder(const VideoDecoderConfig& config,
                                          const DecoderInitCB& init_cb) {
  // AesDecryptor does not support video decoding.
  init_cb.Run(false);
}

void AesDecryptor::DecryptAndDecodeAudio(
    const scoped_refptr<DecoderBuffer>& encrypted,
    const AudioDecodeCB& audio_decode_cb) {
  NOTREACHED() << "AesDecryptor does not support audio decoding";
}

void AesDecryptor::DecryptAndDecodeVideo(
    const scoped_refptr<DecoderBuffer>& encrypted,
    const VideoDecodeCB& video_decode_cb) {
  NOTREACHED() << "AesDecryptor does not support video decoding";
}

void AesDecryptor::ResetDecoder(StreamType stream_type) {
  NOTREACHED() << "AesDecryptor does not support audio/video decoding";
}

void AesDecryptor::DeinitializeDecoder(StreamType stream_type) {
  NOTREACHED() << "AesDecryptor does not support audio/video decoding";
}

void AesDecryptor::SetKey(const std::string& key_id,
                          scoped_ptr<DecryptionKey> decryption_key) {
  base::AutoLock auto_lock(key_map_lock_);
  KeyMap::iterator found = key_map_.find(key_id);
  if (found != key_map_.end()) {
    delete found->second;
    key_map_.erase(found);
  }
  key_map_[key_id] = decryption_key.release();
}

AesDecryptor::DecryptionKey* AesDecryptor::GetKey(
    const std::string& key_id) const {
  base::AutoLock auto_lock(key_map_lock_);
  KeyMap::const_iterator found = key_map_.find(key_id);
  if (found == key_map_.end())
    return NULL;

  return found->second;
}

AesDecryptor::DecryptionKey::DecryptionKey(const std::string& secret)
    : secret_(secret) {
}

AesDecryptor::DecryptionKey::~DecryptionKey() {}

bool AesDecryptor::DecryptionKey::Init() {
  CHECK(!secret_.empty());
  decryption_key_.reset(crypto::SymmetricKey::Import(
      crypto::SymmetricKey::AES, secret_));
  if (!decryption_key_)
    return false;
  return true;
}

}  // namespace media
