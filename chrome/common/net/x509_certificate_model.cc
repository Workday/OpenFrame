// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/net/x509_certificate_model.h"

#include <unicode/uidna.h>

#include "base/strings/utf_string_conversions.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace x509_certificate_model {

std::string ProcessIDN(const std::string& input) {
  // Convert the ASCII input to a string16 for ICU.
  string16 input16;
  input16.reserve(input.length());
  input16.insert(input16.end(), input.begin(), input.end());

  string16 output16;
  output16.resize(input.length());

  UErrorCode status = U_ZERO_ERROR;
  int output_chars = uidna_IDNToUnicode(input16.data(), input.length(),
                                        &output16[0], output16.length(),
                                        UIDNA_DEFAULT, NULL, &status);
  if (status == U_ZERO_ERROR) {
    output16.resize(output_chars);
  } else if (status != U_BUFFER_OVERFLOW_ERROR) {
    return input;
  } else {
    output16.resize(output_chars);
    output_chars = uidna_IDNToUnicode(input16.data(), input.length(),
                                      &output16[0], output16.length(),
                                      UIDNA_DEFAULT, NULL, &status);
    if (status != U_ZERO_ERROR)
      return input;
    DCHECK_EQ(static_cast<size_t>(output_chars), output16.length());
    output16.resize(output_chars);  // Just to be safe.
  }

  if (input16 == output16)
    return input;  // Input did not contain any encoded data.

  // Input contained encoded data, return formatted string showing original and
  // decoded forms.
  return l10n_util::GetStringFUTF8(IDS_CERT_INFO_IDN_VALUE_FORMAT,
                                   input16, output16);
}

std::string ProcessRawBytesWithSeparators(const unsigned char* data,
                                          size_t data_length,
                                          char hex_separator,
                                          char line_separator) {
  static const char kHexChars[] = "0123456789ABCDEF";

  // Each input byte creates two output hex characters + a space or newline,
  // except for the last byte.
  std::string ret;
  size_t kMin = 0U;

  if (!data_length)
    return std::string();

  ret.reserve(std::max(kMin, data_length * 3 - 1));

  for (size_t i = 0; i < data_length; ++i) {
    unsigned char b = data[i];
    ret.push_back(kHexChars[(b >> 4) & 0xf]);
    ret.push_back(kHexChars[b & 0xf]);
    if (i + 1 < data_length) {
      if ((i + 1) % 16 == 0)
        ret.push_back(line_separator);
      else
        ret.push_back(hex_separator);
    }
  }
  return ret;
}

std::string ProcessRawBytes(const unsigned char* data, size_t data_length) {
  return ProcessRawBytesWithSeparators(data, data_length, ' ', '\n');
}

#if defined(USE_NSS)
std::string ProcessRawBits(const unsigned char* data, size_t data_length) {
  return ProcessRawBytes(data, (data_length + 7) / 8);
}
#endif  // USE_NSS

}  // x509_certificate_model

