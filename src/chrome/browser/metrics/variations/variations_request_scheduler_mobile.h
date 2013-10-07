// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_METRICS_VARIATIONS_VARIATIONS_REQUEST_SCHEDULER_MOBILE_H_
#define CHROME_BROWSER_METRICS_VARIATIONS_VARIATIONS_REQUEST_SCHEDULER_MOBILE_H_

#include "base/bind.h"
#include "chrome/browser/metrics/variations/variations_request_scheduler.h"

class PrefService;

namespace chrome_variations {

// A specialized VariationsRequestScheduler that manages request cycles for
// VariationsService on mobile platforms.
class VariationsRequestSchedulerMobile : public VariationsRequestScheduler {
 public:
  // |task} is the closure to call when the scheduler deems ready. |local_state|
  // is the PrefService that contains the time of the last fetch.
  explicit VariationsRequestSchedulerMobile(const base::Closure& task,
                                            PrefService* local_state);
  virtual ~VariationsRequestSchedulerMobile();

  // Base class overrides.
  virtual void Start() OVERRIDE;
  virtual void Reset() OVERRIDE;

 private:
  // The local state instance that provides the last fetch time.
  PrefService* local_state_;

  DISALLOW_COPY_AND_ASSIGN(VariationsRequestSchedulerMobile);
};

}  // namespace chrome_variations

#endif  // CHROME_BROWSER_METRICS_VARIATIONS_VARIATIONS_REQUEST_SCHEDULER_MOBILE_H_
