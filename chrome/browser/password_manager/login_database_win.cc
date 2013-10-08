// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/string_util.h"
#include "chrome/browser/password_manager/login_database.h"
#include "components/webdata/encryptor/encryptor.h"

bool LoginDatabase::EncryptedString(const string16& plain_text,
                                    std::string* cipher_text) const {
  return Encryptor::EncryptString16(plain_text, cipher_text);
}

bool LoginDatabase::DecryptedString(const std::string& cipher_text,
                                    string16* plain_text) const {
  return Encryptor::DecryptString16(cipher_text, plain_text);
}
