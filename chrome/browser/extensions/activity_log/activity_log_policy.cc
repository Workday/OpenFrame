// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/activity_log/activity_log_policy.h"

#include <stdint.h>

#include "base/files/file_path.h"
#include "base/json/json_string_value_serializer.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "base/time/clock.h"
#include "base/time/time.h"
#include "chrome/browser/extensions/activity_log/activity_action_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/browser_thread.h"
#include "url/gurl.h"

using content::BrowserThread;

namespace constants = activity_log_constants;

namespace {
// Obsolete database tables: these should be dropped from the database if
// found.
const char* kObsoleteTables[] = {"activitylog_apis", "activitylog_blocked",
                                 "activitylog_urls"};
}  // namespace

namespace extensions {

ActivityLogPolicy::ActivityLogPolicy(Profile* profile) {}

ActivityLogPolicy::~ActivityLogPolicy() {}

void ActivityLogPolicy::SetClockForTesting(scoped_ptr<base::Clock> clock) {
  testing_clock_.reset(clock.release());
}

base::Time ActivityLogPolicy::Now() const {
  if (testing_clock_)
    return testing_clock_->Now();
  else
    return base::Time::Now();
}

ActivityLogDatabasePolicy::ActivityLogDatabasePolicy(
    Profile* profile,
    const base::FilePath& database_name)
    : ActivityLogPolicy(profile) {
  CHECK(profile);
  base::FilePath profile_base_path = profile->GetPath();
  db_ = new ActivityDatabase(this);
  base::FilePath database_path = profile_base_path.Append(database_name);
  ScheduleAndForget(db_, &ActivityDatabase::Init, database_path);
}

sql::Connection* ActivityLogDatabasePolicy::GetDatabaseConnection() const {
  return db_->GetSqlConnection();
}

// static
std::string ActivityLogPolicy::Util::Serialize(const base::Value* value) {
  std::string value_as_text;
  if (!value) {
    value_as_text = "";
  } else {
    JSONStringValueSerializer serializer(&value_as_text);
    serializer.SerializeAndOmitBinaryValues(*value);
  }
  return value_as_text;
}

// static
void ActivityLogPolicy::Util::StripPrivacySensitiveFields(
    scoped_refptr<Action> action) {
  // Clear incognito URLs/titles.
  if (action->page_incognito()) {
    action->set_page_url(GURL());
    action->set_page_title("");
  }
  if (action->arg_incognito()) {
    action->set_arg_url(GURL());
  }

  // Strip query parameters, username/password, etc., from URLs.
  if (action->page_url().is_valid() || action->arg_url().is_valid()) {
    url_canon::Replacements<char> url_sanitizer;
    url_sanitizer.ClearUsername();
    url_sanitizer.ClearPassword();
    url_sanitizer.ClearQuery();
    url_sanitizer.ClearRef();

    if (action->page_url().is_valid())
      action->set_page_url(action->page_url().ReplaceComponents(url_sanitizer));
    if (action->arg_url().is_valid())
      action->set_arg_url(action->arg_url().ReplaceComponents(url_sanitizer));
  }

  // Clear WebRequest details; only keep a record of which types of
  // modifications were performed.
  if (action->action_type() == Action::ACTION_WEB_REQUEST) {
    DictionaryValue* details = NULL;
    if (action->mutable_other()->GetDictionary(constants::kActionWebRequest,
                                               &details)) {
      DictionaryValue::Iterator details_iterator(*details);
      while (!details_iterator.IsAtEnd()) {
        details->SetBoolean(details_iterator.key(), true);
        details_iterator.Advance();
      }
    }
  }
}

// static
void ActivityLogPolicy::Util::StripArguments(
    const std::set<std::string>& api_whitelist,
    scoped_refptr<Action> action) {
  if (action->action_type() != Action::ACTION_API_CALL &&
      action->action_type() != Action::ACTION_API_EVENT &&
      action->action_type() != Action::ACTION_API_BLOCKED)
    return;

  if (api_whitelist.find(action->api_name()) == api_whitelist.end())
    action->set_args(scoped_ptr<ListValue>());
}

// static
base::Time ActivityLogPolicy::Util::AddDays(const base::Time& base_date,
                                            int days) {
  // To allow for time zone changes, add an additional partial day then round
  // down to midnight.
  return (base_date + base::TimeDelta::FromDays(days) +
          base::TimeDelta::FromHours(4)).LocalMidnight();
}

// static
void ActivityLogPolicy::Util::ComputeDatabaseTimeBounds(const base::Time& now,
                                                        int days_ago,
                                                        int64* early_bound,
                                                        int64* late_bound) {
  base::Time morning_midnight = now.LocalMidnight();
  if (days_ago == 0) {
      *early_bound = morning_midnight.ToInternalValue();
      *late_bound = base::Time::Max().ToInternalValue();
  } else {
      base::Time early_time = Util::AddDays(morning_midnight, -days_ago);
      base::Time late_time = Util::AddDays(early_time, 1);
      *early_bound = early_time.ToInternalValue();
      *late_bound = late_time.ToInternalValue();
  }
}

// static
bool ActivityLogPolicy::Util::DropObsoleteTables(sql::Connection* db) {
  for (size_t i = 0; i < arraysize(kObsoleteTables); i++) {
    const char* table_name = kObsoleteTables[i];
    if (db->DoesTableExist(table_name)) {
      std::string drop_statement =
          base::StringPrintf("DROP TABLE %s", table_name);
      if (!db->Execute(drop_statement.c_str())) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace extensions
