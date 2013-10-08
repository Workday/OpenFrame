// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines a set of user experience metrics data recorded by
// the MetricsService.  This is the unit of data that is sent to the server.

#ifndef CHROME_COMMON_METRICS_METRICS_LOG_BASE_H_
#define CHROME_COMMON_METRICS_METRICS_LOG_BASE_H_

#include <string>

#include "base/basictypes.h"
#include "base/metrics/histogram.h"
#include "base/time/time.h"
#include "chrome/common/metrics/proto/chrome_user_metrics_extension.pb.h"
#include "content/public/common/page_transition_types.h"

class GURL;

namespace base {
class HistogramSamples;
}  // namespace base

// This class provides base functionality for logging metrics data.
class MetricsLogBase {
 public:
  // Creates a new metrics log
  // client_id is the identifier for this profile on this installation
  // session_id is an integer that's incremented on each application launch
  MetricsLogBase(const std::string& client_id,
                 int session_id,
                 const std::string& version_string);
  virtual ~MetricsLogBase();

  // Computes the MD5 hash of the given string, and returns the first 8 bytes of
  // the hash.
  static uint64 Hash(const std::string& value);

  // Get the GMT buildtime for the current binary, expressed in seconds since
  // Januray 1, 1970 GMT.
  // The value is used to identify when a new build is run, so that previous
  // reliability stats, from other builds, can be abandoned.
  static int64 GetBuildTime();

  // Convenience function to return the current time at a resolution in seconds.
  // This wraps base::TimeTicks, and hence provides an abstract time that is
  // always incrementing for use in measuring time durations.
  static int64 GetCurrentTime();

  // Records a user-initiated action.
  void RecordUserAction(const char* key);

  // Record any changes in a given histogram for transmission.
  void RecordHistogramDelta(const std::string& histogram_name,
                            const base::HistogramSamples& snapshot);

  // Stop writing to this record and generate the encoded representation.
  // None of the Record* methods can be called after this is called.
  void CloseLog();

  // Fills |encoded_log| with the serialized protobuf representation of the
  // record.  Must only be called after CloseLog() has been called.
  void GetEncodedLog(std::string* encoded_log);

  int num_events() { return num_events_; }

  void set_hardware_class(const std::string& hardware_class) {
    uma_proto_.mutable_system_profile()->mutable_hardware()->set_hardware_class(
        hardware_class);
  }

 protected:
  bool locked() const { return locked_; }

  metrics::ChromeUserMetricsExtension* uma_proto() { return &uma_proto_; }
  const metrics::ChromeUserMetricsExtension* uma_proto() const {
    return &uma_proto_;
  }

  // TODO(isherman): Remove this once the XML pipeline is outta here.
  int num_events_;  // the number of events recorded in this log

 private:
  // locked_ is true when record has been packed up for sending, and should
  // no longer be written to.  It is only used for sanity checking and is
  // not a real lock.
  bool locked_;

  // Stores the protocol buffer representation for this log.
  metrics::ChromeUserMetricsExtension uma_proto_;

  DISALLOW_COPY_AND_ASSIGN(MetricsLogBase);
};

#endif  // CHROME_COMMON_METRICS_METRICS_LOG_BASE_H_
