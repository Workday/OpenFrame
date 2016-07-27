// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TRANSLATE_STANDALONE_CLD_DATA_HARNESS_H_
#define CHROME_BROWSER_TRANSLATE_STANDALONE_CLD_DATA_HARNESS_H_

#include "base/macros.h"
#include "chrome/browser/translate/cld_data_harness.h"

namespace test {

// Utility class that sets up a test harness suitable for injecting a standalone
// CLD data file into the runtime. See CldDataHarness class for more details.
class StandaloneCldDataHarness : public CldDataHarness {
 public:
  StandaloneCldDataHarness() {}
  ~StandaloneCldDataHarness() override;
  void Init() override;

 private:
  void GetStandaloneDataFileSource(base::FilePath*);
  void GetStandaloneDataFileDestination(base::FilePath*);
  void DeleteStandaloneDataFile();
  void CopyStandaloneDataFile();

  DISALLOW_COPY_AND_ASSIGN(StandaloneCldDataHarness);
};

}  // namespace test

#endif  // CHROME_BROWSER_TRANSLATE_STANDALONE_CLD_DATA_HARNESS_H_
