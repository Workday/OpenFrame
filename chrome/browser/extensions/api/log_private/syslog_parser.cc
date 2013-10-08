// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/log_private/syslog_parser.h"

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/singleton.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_tokenizer.h"
#include "base/time/time.h"
#include "chrome/browser/extensions/api/log_private/filter_handler.h"
#include "chrome/browser/extensions/api/log_private/log_parser.h"
#include "chrome/browser/extensions/api/log_private/log_private_api.h"
#include "chrome/common/extensions/api/log_private.h"

namespace extensions {

namespace {

const int kExpectedTimeTokenNum = 7;
const char kLogEntryDelimiters[] = "-:T";
const char kProcessInfoDelimiters[] = "[]";

}  // namespace

SyslogParser::SyslogParser() {}

SyslogParser::~SyslogParser() {}

SyslogParser::Error SyslogParser::ParseEntry(
    const std::string& input,
    std::vector<linked_ptr<api::log_private::LogEntry> >* output,
    FilterHandler* filter_handler) const {
  linked_ptr<api::log_private::LogEntry> entry(new api::log_private::LogEntry);

  base::StringTokenizer tokenizer(input, " ");
  if (!tokenizer.GetNext()) {
    LOG(ERROR)
        << "Error when parsing data. Expect: At least 3 tokens. Actual: 0";
    return TOKENIZE_ERROR;
  }
  std::string time = tokenizer.token();
  if (ParseTime(time, &(entry->timestamp)) != SyslogParser::SUCCESS) {
    return SyslogParser::PARSE_ERROR;
  }
  // Skips "localhost" field.
  if (!tokenizer.GetNext()) {
    LOG(ERROR)
        << "Error when parsing data. Expect: At least 3 tokens. Actual: 1";
    return TOKENIZE_ERROR;
  }
  if (!tokenizer.GetNext()) {
    LOG(ERROR)
        << "Error when parsing data. Expect: At least 3 tokens. Actual: 2";
    return TOKENIZE_ERROR;
  }
  ParseProcess(tokenizer.token(), entry.get());
  ParseLevel(input, entry.get());
  entry->full_entry = input;

  if (filter_handler->IsValidLogEntry(*(entry.get()))) {
    output->push_back(entry);
  }

  return SyslogParser::SUCCESS;
}

SyslogParser::Error ParseTimeHelper(base::StringTokenizer* tokenizer,
                                    std::string* output) {
  if (!tokenizer->GetNext()) {
    LOG(ERROR) << "Error when parsing time";
    return SyslogParser::PARSE_ERROR;
  }
  *output = tokenizer->token();
  return SyslogParser::SUCCESS;
}

SyslogParser::Error SyslogParser::ParseTime(const std::string& input,
                                            double* output) const {
  base::StringTokenizer tokenizer(input, kLogEntryDelimiters);
  std::string tokens[kExpectedTimeTokenNum];
  for (int i = 0; i < kExpectedTimeTokenNum; i++) {
    if (ParseTimeHelper(&tokenizer, &(tokens[i])) != SyslogParser::SUCCESS)
      return SyslogParser::PARSE_ERROR;
  }

  std::string buffer = tokens[1] + '-' + tokens[2] + '-' + tokens[0] + ' ' +
                       tokens[3] + ':' + tokens[4] + ":00";

  base::Time parsed_time;
  if (!base::Time::FromString(buffer.c_str(), &parsed_time)) {
    LOG(ERROR) << "Error when parsing time";
    return SyslogParser::PARSE_ERROR;
  }

  double seconds;
  base::StringToDouble(tokens[5], &seconds);
  *output = parsed_time.ToJsTime() +
            (seconds * base::Time::kMillisecondsPerSecond);

  return SyslogParser::SUCCESS;
}

SyslogParser::Error SyslogParser::ParseProcess(
    const std::string& input,
    api::log_private::LogEntry* entry) const {
  base::StringTokenizer tokenizer(input, kProcessInfoDelimiters);
  if (!tokenizer.GetNext()) {
    LOG(ERROR)
        << "Error when parsing data. Expect: At least 1 token. Actual: 0";
    return SyslogParser::PARSE_ERROR;
  }
  entry->process = tokenizer.token();
  entry->process_id = "unknown";
  if (tokenizer.GetNext()) {
    std::string token = tokenizer.token();
    int tmp;
    if (base::StringToInt(token, &tmp)) {
      entry->process_id = token;
    }
  }
  return SyslogParser::SUCCESS;
}

void SyslogParser::ParseLevel(const std::string& input,
                              api::log_private::LogEntry* entry) const {
  if (input.find("ERROR") != std::string::npos) {
    entry->level = "error";
  } else if (input.find("WARN") != std::string::npos) {
    entry->level = "warning";
  } else if (input.find("INFO") != std::string::npos) {
    entry->level = "info";
  } else {
    entry->level = "unknown";
  }
}

}  // namespace extensions
