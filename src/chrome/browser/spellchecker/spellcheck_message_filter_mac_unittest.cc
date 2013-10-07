// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/spellchecker/spellcheck_message_filter_mac.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/common/spellcheck_messages.h"
#include "chrome/common/spellcheck_result.h"
#include "content/public/browser/browser_thread.h"
#include "ipc/ipc_message.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

TEST(SpellcheckMessageFilterMacTest, CombineResults) {
  std::vector<SpellCheckResult> local_results;
  std::vector<SpellCheckResult> remote_results;
  string16 remote_suggestion = ASCIIToUTF16("remote");
  string16 local_suggestion = ASCIIToUTF16("local");

  // Remote-only result - must be flagged as GRAMMAR after combine
  remote_results.push_back(
      SpellCheckResult(SpellCheckResult::SPELLING, 0, 5));

  // Local-only result - must be discarded after combine
  local_results.push_back(
      SpellCheckResult(SpellCheckResult::SPELLING, 10, 5));

  // local & remote result - must be flagged SPELLING, uses remote suggestion.
  SpellCheckResult result(SpellCheckResult::SPELLING, 20, 5, local_suggestion);
  local_results.push_back(result);
  result.replacement = remote_suggestion;
  remote_results.push_back(result);

  SpellCheckMessageFilterMac::CombineResults(&remote_results, local_results);

  ASSERT_EQ(2U, remote_results.size());
  EXPECT_EQ(SpellCheckResult::GRAMMAR, remote_results[0].type);
  EXPECT_EQ(0, remote_results[0].location);
  EXPECT_EQ(SpellCheckResult::SPELLING, remote_results[1].type);
  EXPECT_EQ(20, remote_results[1].location);
  EXPECT_EQ(remote_suggestion, remote_results[1].replacement);
}

TEST(SpellCheckMessageFilterMacTest, TestOverrideThread) {
  static const uint32 kSpellcheckMessages[] = {
    SpellCheckHostMsg_RequestTextCheck::ID,
  };
  scoped_refptr<SpellCheckMessageFilterMac> filter(
      new SpellCheckMessageFilterMac(0));
  content::BrowserThread::ID thread;
  IPC::Message message;
  for (size_t i = 0; i < arraysize(kSpellcheckMessages); ++i) {
    message.SetHeaderValues(
        0, kSpellcheckMessages[i], IPC::Message::PRIORITY_NORMAL);
    thread = content::BrowserThread::IO;
    filter->OverrideThreadForMessage(message, &thread);
    EXPECT_EQ(content::BrowserThread::UI, thread);
  }
}

}  // namespace
