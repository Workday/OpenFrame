// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_METRICS_METRICS_LOG_SERIALIZER_H_
#define CHROME_BROWSER_METRICS_METRICS_LOG_SERIALIZER_H_

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "chrome/common/metrics/metrics_log_manager.h"

namespace base {
class ListValue;
}

// Serializer for persisting metrics logs to prefs.
class MetricsLogSerializer : public MetricsLogManager::LogSerializer {
 public:
  // Used to produce a histogram that keeps track of the status of recalling
  // persisted per logs.
  enum LogReadStatus {
    RECALL_SUCCESS,         // We were able to correctly recall a persisted log.
    LIST_EMPTY,             // Attempting to recall from an empty list.
    LIST_SIZE_MISSING,      // Failed to recover list size using GetAsInteger().
    LIST_SIZE_TOO_SMALL,    // Too few elements in the list (less than 3).
    LIST_SIZE_CORRUPTION,   // List size is not as expected.
    LOG_STRING_CORRUPTION,  // Failed to recover log string using GetAsString().
    CHECKSUM_CORRUPTION,    // Failed to verify checksum.
    CHECKSUM_STRING_CORRUPTION,  // Failed to recover checksum string using
                                 // GetAsString().
    DECODE_FAIL,            // Failed to decode log.
    DEPRECATED_XML_PROTO_MISMATCH,  // The XML and protobuf logs have
                                    // inconsistent data.
    END_RECALL_STATUS       // Number of bins to use to create the histogram.
  };

  MetricsLogSerializer();
  virtual ~MetricsLogSerializer();

  // Implementation of MetricsLogManager::LogSerializer
  virtual void SerializeLogs(const std::vector<std::string>& logs,
                             MetricsLogManager::LogType log_type) OVERRIDE;
  virtual void DeserializeLogs(MetricsLogManager::LogType log_type,
                               std::vector<std::string>* logs) OVERRIDE;

 private:
  // Encodes the textual log data from |local_list| and writes it to the given
  // pref list, along with list size and checksum.  Logs will be stored starting
  // with the most recent, and working backward until at least
  // |list_length_limit| logs and |byte_limit| bytes of logs have been
  // stored. At least one of those two arguments must be non-zero.
  static void WriteLogsToPrefList(const std::vector<std::string>& local_list,
                                  size_t list_length_limit,
                                  size_t byte_limit,
                                  base::ListValue* list);

  // Decodes and verifies the textual log data from |list|, populating
  // |local_list| and returning a status code.
  static LogReadStatus ReadLogsFromPrefList(
      const base::ListValue& list,
      std::vector<std::string>* local_list);

  FRIEND_TEST_ALL_PREFIXES(MetricsLogSerializerTest, EmptyLogList);
  FRIEND_TEST_ALL_PREFIXES(MetricsLogSerializerTest, SingleElementLogList);
  FRIEND_TEST_ALL_PREFIXES(MetricsLogSerializerTest, LongButTinyLogList);
  FRIEND_TEST_ALL_PREFIXES(MetricsLogSerializerTest, LongButSmallLogList);
  FRIEND_TEST_ALL_PREFIXES(MetricsLogSerializerTest, ShortButLargeLogList);
  FRIEND_TEST_ALL_PREFIXES(MetricsLogSerializerTest, LongAndLargeLogList);
  FRIEND_TEST_ALL_PREFIXES(MetricsLogSerializerTest, SmallRecoveredListSize);
  FRIEND_TEST_ALL_PREFIXES(MetricsLogSerializerTest, RemoveSizeFromLogList);
  FRIEND_TEST_ALL_PREFIXES(MetricsLogSerializerTest, CorruptSizeOfLogList);
  FRIEND_TEST_ALL_PREFIXES(MetricsLogSerializerTest, CorruptChecksumOfLogList);

  DISALLOW_COPY_AND_ASSIGN(MetricsLogSerializer);
};

#endif  // CHROME_BROWSER_METRICS_METRICS_LOG_SERIALIZER_H_
