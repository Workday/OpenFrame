// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DIAGNOSTICS_DIAGNOSTICS_CONTROLLER_H_
#define CHROME_BROWSER_DIAGNOSTICS_DIAGNOSTICS_CONTROLLER_H_

#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"

class CommandLine;

namespace diagnostics {

class DiagnosticsWriter;
class DiagnosticsModel;

class DiagnosticsController {
 public:
  static DiagnosticsController* GetInstance();

  // Entry point for the diagnostics mode. Returns zero if able to run
  // diagnostics successfully, regardless of the results of the diagnostics.
  int Run(const CommandLine& command_line, DiagnosticsWriter* writer);

  // Entry point for running recovery based on diagnostics that have already
  // been run. In order for this to do anything, Run() must be executed first.
  int RunRecovery(const CommandLine& command_line, DiagnosticsWriter* writer);

  // Returns a model with the results that have accumulated. They can then be
  // queried for their attributes for human consumption later.
  const DiagnosticsModel& GetResults() const;

  // Returns true if there are any results available.
  bool HasResults();

  // Clears any results that have accumulated. After calling this, do not call
  // GetResults until after Run is called again.
  void ClearResults();

 private:
  friend struct DefaultSingletonTraits<DiagnosticsController>;

  DiagnosticsController();
  ~DiagnosticsController();

  scoped_ptr<DiagnosticsModel> model_;
  DiagnosticsWriter* writer_;

  DISALLOW_COPY_AND_ASSIGN(DiagnosticsController);
};

}  // namespace diagnostics

#endif  // CHROME_BROWSER_DIAGNOSTICS_DIAGNOSTICS_CONTROLLER_H_
