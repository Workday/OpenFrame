// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_WEBDATA_AUTOFILL_ENTRY_H__
#define COMPONENTS_AUTOFILL_CORE_BROWSER_WEBDATA_AUTOFILL_ENTRY_H__

#include <stddef.h>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/strings/string16.h"
#include "base/time/time.h"

namespace autofill {

class AutofillKey {
 public:
  AutofillKey();
  AutofillKey(const base::string16& name, const base::string16& value);
  AutofillKey(const char* name, const char* value);
  AutofillKey(const AutofillKey& key);
  virtual ~AutofillKey();

  const base::string16& name() const { return name_; }
  const base::string16& value() const { return value_; }

  bool operator==(const AutofillKey& key) const;
  bool operator<(const AutofillKey& key) const;

 private:
  base::string16 name_;
  base::string16 value_;
};

class AutofillEntry {
 public:
  AutofillEntry(const AutofillKey& key,
                const std::vector<base::Time>& timestamps);
  ~AutofillEntry();

  const AutofillKey& key() const { return key_; }
  const std::vector<base::Time>& timestamps() const { return timestamps_; }

  bool operator==(const AutofillEntry& entry) const;
  bool operator<(const AutofillEntry& entry) const;

  bool timestamps_culled() const { return timestamps_culled_; }

  // Checks if last of the timestamps are older than ExpirationTime().
  bool IsExpired() const;

  // The entries last accessed before this time should expire.
  static base::Time ExpirationTime();

 private:
  FRIEND_TEST_ALL_PREFIXES(AutofillEntryTest, NoCulling);
  FRIEND_TEST_ALL_PREFIXES(AutofillEntryTest, Culling);
  FRIEND_TEST_ALL_PREFIXES(AutofillEntryTest, CullByTime);

  // Culls the list of timestamps to 2 - the oldest and most recent. This is a
  // precursor to getting rid of the timestamps db altogether.
  // See http://crbug.com/118696.
  // |source| is expected to be sorted from oldest to newest.
  static bool CullTimeStamps(const std::vector<base::Time>& source,
                             std::vector<base::Time>* result);

  AutofillKey key_;
  std::vector<base::Time> timestamps_;
  bool timestamps_culled_;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_WEBDATA_AUTOFILL_ENTRY_H__
