// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/gcm_driver/crypto/encryption_header_parsers.h"

#include "base/base64url.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "net/http/http_util.h"

namespace gcm {

namespace {

// The default record size in bytes, as defined in section two of
// https://tools.ietf.org/html/draft-thomson-http-encryption-02.
const uint64_t kDefaultRecordSizeBytes = 4096;

// Decodes the string between |begin| and |end| using base64url, and writes the
// decoded value to |*salt|. Returns whether the string could be decoded.
bool ValueToDecodedString(const std::string::const_iterator& begin,
                          const std::string::const_iterator& end,
                          std::string* salt) {
  const base::StringPiece value(begin, end);
  if (value.empty())
    return false;

  return base::Base64UrlDecode(
      value, base::Base64UrlDecodePolicy::IGNORE_PADDING, salt);
}

// Parses the record size between |begin| and |end|, and writes the value to
// |*rs|. The value must be an unsigned, 64-bit integer greater than zero that
// does not start with a plus. Returns whether the record size was valid.
bool RecordSizeToInt(const std::string::const_iterator& begin,
                     const std::string::const_iterator& end,
                     uint64_t* rs) {
  const base::StringPiece value(begin, end);
  if (value.empty())
    return false;

  // Parsing the "rs" parameter uses stricter semantics than parsing rules for
  // normal integers, in that we want to reject values such as "+5" for
  // compatibility with UAs that use other number parsing mechanisms.
  if (value[0] == '+')
    return false;

  if (!base::StringToUint64(value, rs))
    return false;

  // The record size MUST be greater than 1.
  return *rs > 1;
}

// Parses the string between |input_begin| and |input_end| according to the
// extended ABNF syntax for the Encryption HTTP header, per the "parameter"
// rule from RFC 7231 (https://tools.ietf.org/html/rfc7231).
//
// encryption_params = [ parameter *( ";" parameter ) ]
//
// This implementation applies the parameters defined in section 3.1 of the
// HTTP encryption encoding document:
//
// https://tools.ietf.org/html/draft-thomson-http-encryption-02#section-3.1
//
// This means that the three supported parameters are:
//
//     [ "keyid" "=" string ]
//     [ ";" "salt" "=" base64url ]
//     [ ";" "rs" "=" octet-count ]
bool ParseEncryptionHeaderValuesImpl(std::string::const_iterator input_begin,
                                     std::string::const_iterator input_end,
                                     EncryptionHeaderValues* values) {
  net::HttpUtil::NameValuePairsIterator name_value_pairs(
      input_begin, input_end, ';',
      net::HttpUtil::NameValuePairsIterator::VALUES_NOT_OPTIONAL);

  while (name_value_pairs.GetNext()) {
    const base::StringPiece name(name_value_pairs.name_begin(),
                                 name_value_pairs.name_end());

    if (base::LowerCaseEqualsASCII(name, "keyid")) {
      values->keyid.assign(name_value_pairs.value_begin(),
                           name_value_pairs.value_end());
    } else if (base::LowerCaseEqualsASCII(name, "salt")) {
      if (!ValueToDecodedString(name_value_pairs.value_begin(),
                                name_value_pairs.value_end(), &values->salt)) {
        return false;
      }
    } else if (base::LowerCaseEqualsASCII(name, "rs")) {
      if (!RecordSizeToInt(name_value_pairs.value_begin(),
                           name_value_pairs.value_end(), &values->rs)) {
        return false;
      }
    } else {
      // Silently ignore unknown directives for forward compatibility.
    }
  }

  return name_value_pairs.valid();
}

// Parses the string between |input_begin| and |input_end| according to the
// extended ABNF syntax for the Crypto-Key HTTP header, per the "parameter" rule
// from RFC 7231 (https://tools.ietf.org/html/rfc7231).
//
// encryption_params = [ parameter *( ";" parameter ) ]
//
// This implementation applies the parameters defined in section 4 of the
// HTTP encryption encoding document:
//
//https://tools.ietf.org/html/draft-thomson-http-encryption-02#section-4
//
// This means that the three supported parameters are:
//
//     [ "keyid" "=" string ]
//     [ ";" "aesgcm128" "=" base64url ]
//     [ ";" "dh" "=" base64url ]
bool ParseCryptoKeyHeaderValuesImpl(std::string::const_iterator input_begin,
                                    std::string::const_iterator input_end,
                                    CryptoKeyHeaderValues* values) {
  net::HttpUtil::NameValuePairsIterator name_value_pairs(
      input_begin, input_end, ';',
      net::HttpUtil::NameValuePairsIterator::VALUES_NOT_OPTIONAL);

  while (name_value_pairs.GetNext()) {
    const base::StringPiece name(name_value_pairs.name_begin(),
                                 name_value_pairs.name_end());

    if (base::LowerCaseEqualsASCII(name, "keyid")) {
      values->keyid.assign(name_value_pairs.value_begin(),
                           name_value_pairs.value_end());
    } else if (base::LowerCaseEqualsASCII(name, "aesgcm128")) {
      if (!ValueToDecodedString(name_value_pairs.value_begin(),
                                name_value_pairs.value_end(),
                                &values->aesgcm128)) {
        return false;
      }
    } else if (base::LowerCaseEqualsASCII(name, "dh")) {
      if (!ValueToDecodedString(name_value_pairs.value_begin(),
                                name_value_pairs.value_end(), &values->dh)) {
        return false;
      }
    } else {
      // Silently ignore unknown directives for forward compatibility.
    }
  }

  return name_value_pairs.valid();
}

}  // namespace

bool ParseEncryptionHeader(const std::string& input,
                           std::vector<EncryptionHeaderValues>* values) {
  DCHECK(values);

  std::vector<EncryptionHeaderValues> candidate_values;

  net::HttpUtil::ValuesIterator value_iterator(input.begin(), input.end(), ',');
  while (value_iterator.GetNext()) {
    EncryptionHeaderValues candidate_value;
    candidate_value.rs = kDefaultRecordSizeBytes;

    if (!ParseEncryptionHeaderValuesImpl(value_iterator.value_begin(),
                                         value_iterator.value_end(),
                                         &candidate_value)) {
      return false;
    }

    candidate_values.push_back(candidate_value);
  }

  values->swap(candidate_values);
  return true;
}

bool ParseCryptoKeyHeader(const std::string& input,
                          std::vector<CryptoKeyHeaderValues>* values) {
  DCHECK(values);

  std::vector<CryptoKeyHeaderValues> candidate_values;

  net::HttpUtil::ValuesIterator value_iterator(input.begin(), input.end(), ',');
  while (value_iterator.GetNext()) {
    CryptoKeyHeaderValues candidate_value;
    if (!ParseCryptoKeyHeaderValuesImpl(value_iterator.value_begin(),
                                        value_iterator.value_end(),
                                        &candidate_value)) {
      return false;
    }

    candidate_values.push_back(candidate_value);
  }

  values->swap(candidate_values);
  return true;
}

}  // namespace gcm
