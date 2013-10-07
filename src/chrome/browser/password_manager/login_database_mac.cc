// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/password_manager/login_database.h"

// On the Mac, the LoginDatabase nulls out passwords, so that we can use the
// rest of the database as a suplemental storage system to complement Keychain,
// providing storage of fields Keychain doesn't allow.

bool LoginDatabase::EncryptedString(const string16& plain_text,
                                    std::string* cipher_text) const {
  *cipher_text = std::string();
  return true;
}

bool LoginDatabase::DecryptedString(const std::string& cipher_text,
                                    string16* plain_text) const {
  *plain_text = string16();
  return true;
}
