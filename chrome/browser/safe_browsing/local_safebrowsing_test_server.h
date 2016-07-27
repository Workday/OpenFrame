// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SAFE_BROWSING_LOCAL_SAFEBROWSING_TEST_SERVER_H_
#define CHROME_BROWSER_SAFE_BROWSING_LOCAL_SAFEBROWSING_TEST_SERVER_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "net/test/spawned_test_server/local_test_server.h"

namespace safe_browsing {

// Runs a Python-based safebrowsing test server on the same machine in which the
// LocalSafeBrowsingTestServer runs.
class LocalSafeBrowsingTestServer : public net::LocalTestServer {
 public:
  // Initialize a safebrowsing server using the given |data_file|.
  explicit LocalSafeBrowsingTestServer(const base::FilePath& data_file);

  ~LocalSafeBrowsingTestServer() override;

  bool SetPythonPath() const override;

  // Returns the path to safe_browsing_testserver.py.
  bool GetTestServerPath(base::FilePath* testserver_path) const override;

 protected:
  // Adds the --data-file switch. Returns true on success.
  bool GenerateAdditionalArguments(
      base::DictionaryValue* arguments) const override;

 private:
  base::FilePath data_file_;

  DISALLOW_COPY_AND_ASSIGN(LocalSafeBrowsingTestServer);
};

}  // namespace safe_browsing

#endif  // CHROME_BROWSER_SAFE_BROWSING_LOCAL_SAFEBROWSING_TEST_SERVER_H_
