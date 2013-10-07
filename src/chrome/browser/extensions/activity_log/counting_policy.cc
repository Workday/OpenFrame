// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A policy for storing activity log data to a database that performs
// aggregation to reduce the size of the database.  The database layout is
// nearly the same as FullStreamUIPolicy, which stores a complete log, with a
// few changes:
//   - a "count" column is added to track how many log records were merged
//     together into this row
//   - the "time" column measures the most recent time that the current row was
//     updated
// When writing a record, if a row already exists where all other columns
// (extension_id, action_type, api_name, args, urls, etc.) all match, and the
// previous time falls within today (the current time), then the count field on
// the old row is incremented.  Otherwise, a new row is written.
//
// For many text columns, repeated strings are compressed by moving string
// storage to a separate table ("string_ids") and storing only an identifier in
// the logging table.  For example, if the api_name_x column contained the
// value 4 and the string_ids table contained a row with primary key 4 and
// value 'tabs.query', then the api_name field should be taken to have the
// value 'tabs.query'.  Each column ending with "_x" is compressed in this way.
// All lookups are to the string_ids table, except for the page_url_x and
// arg_url_x columns, which are converted via the url_ids table (this
// separation of URL values is to help simplify history clearing).
//
// The activitylog_uncompressed view allows for simpler reading of the activity
// log contents with identifiers already translated to string values.

#include "chrome/browser/extensions/activity_log/counting_policy.h"

#include <map>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/json/json_string_value_serializer.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "chrome/common/chrome_constants.h"

using content::BrowserThread;

namespace {

// Delay between cleaning passes (to delete old action records) through the
// database.
const int kCleaningDelayInHours = 12;

// We should log the arguments to these API calls.  Be careful when
// constructing this whitelist to not keep arguments that might compromise
// privacy by logging too much data to the activity log.
//
// TODO(mvrable): The contents of this whitelist should be reviewed and
// expanded as needed.
const char* kAlwaysLog[] = {"extension.connect", "extension.sendMessage",
                            "tabs.executeScript", "tabs.insertCSS"};

// Columns in the main database table.  See the file-level comment for a
// discussion of how data is stored and the meanings of the _x columns.
const char* kTableContentFields[] = {
    "count", "extension_id_x", "time", "action_type", "api_name_x", "args_x",
    "page_url_x", "page_title_x", "arg_url_x", "other_x"};
const char* kTableFieldTypes[] = {
    "INTEGER NOT NULL DEFAULT 1", "INTEGER NOT NULL", "INTEGER", "INTEGER",
    "INTEGER", "INTEGER", "INTEGER", "INTEGER", "INTEGER",
    "INTEGER"};

// Miscellaneous SQL commands for initializing the database; these should be
// idempotent.
static const char kPolicyMiscSetup[] =
    // The activitylog_uncompressed view performs string lookups for simpler
    // access to the log data.
    "DROP VIEW IF EXISTS activitylog_uncompressed;\n"
    "CREATE VIEW activitylog_uncompressed AS\n"
    "SELECT count,\n"
    "    x1.value AS extension_id,\n"
    "    time,\n"
    "    action_type,\n"
    "    x2.value AS api_name,\n"
    "    x3.value AS args,\n"
    "    x4.value AS page_url,\n"
    "    x5.value AS page_title,\n"
    "    x6.value AS arg_url,\n"
    "    x7.value AS other\n"
    "FROM activitylog_compressed\n"
    "    LEFT JOIN string_ids AS x1 ON (x1.id = extension_id_x)\n"
    "    LEFT JOIN string_ids AS x2 ON (x2.id = api_name_x)\n"
    "    LEFT JOIN string_ids AS x3 ON (x3.id = args_x)\n"
    "    LEFT JOIN url_ids    AS x4 ON (x4.id = page_url_x)\n"
    "    LEFT JOIN string_ids AS x5 ON (x5.id = page_title_x)\n"
    "    LEFT JOIN url_ids    AS x6 ON (x6.id = arg_url_x)\n"
    "    LEFT JOIN string_ids AS x7 ON (x7.id = other_x);\n"
    // An index on all fields except count and time: all the fields that aren't
    // changed when incrementing a count.  This should accelerate finding the
    // rows to update (at worst several rows will need to be checked to find
    // the one in the right time range).
    "CREATE INDEX IF NOT EXISTS activitylog_compressed_index\n"
    "ON activitylog_compressed(extension_id_x, action_type, api_name_x,\n"
    "    args_x, page_url_x, page_title_x, arg_url_x, other_x)";

// SQL statements to clean old, unused entries out of the string and URL id
// tables.
static const char kStringTableCleanup[] =
    "DELETE FROM string_ids WHERE id NOT IN\n"
    "(SELECT extension_id_x FROM activitylog_compressed\n"
    " UNION SELECT api_name_x FROM activitylog_compressed\n"
    " UNION SELECT args_x FROM activitylog_compressed\n"
    " UNION SELECT page_title_x FROM activitylog_compressed\n"
    " UNION SELECT other_x FROM activitylog_compressed)";
static const char kUrlTableCleanup[] =
    "DELETE FROM url_ids WHERE id NOT IN\n"
    "(SELECT page_url_x FROM activitylog_compressed\n"
    " UNION SELECT arg_url_x FROM activitylog_compressed)";

}  // namespace

namespace extensions {

// A specialized Action subclass which is used to represent an action read from
// the database with a corresponding count.
class CountedAction : public Action {
 public:
  CountedAction(const std::string& extension_id,
                const base::Time& time,
                const ActionType action_type,
                const std::string& api_name)
      : Action(extension_id, time, action_type, api_name) {}

  // Number of merged records for this action.
  int count() const { return count_; }
  void set_count(int count) { count_ = count; }

  virtual std::string PrintForDebug() const OVERRIDE;

 protected:
  virtual ~CountedAction() {}

 private:
  int count_;
};

std::string CountedAction::PrintForDebug() const {
  return base::StringPrintf(
      "%s COUNT=%d", Action::PrintForDebug().c_str(), count());
}

const char* CountingPolicy::kTableName = "activitylog_compressed";
const char* CountingPolicy::kReadViewName = "activitylog_uncompressed";

CountingPolicy::CountingPolicy(Profile* profile)
    : ActivityLogDatabasePolicy(
          profile,
          base::FilePath(chrome::kExtensionActivityLogFilename)),
      string_table_("string_ids"),
      url_table_("url_ids"),
      retention_time_(base::TimeDelta::FromHours(60)) {
  for (size_t i = 0; i < arraysize(kAlwaysLog); i++) {
    api_arg_whitelist_.insert(kAlwaysLog[i]);
  }
}

CountingPolicy::~CountingPolicy() {}

bool CountingPolicy::InitDatabase(sql::Connection* db) {
  if (!Util::DropObsoleteTables(db))
    return false;

  if (!string_table_.Initialize(db))
    return false;
  if (!url_table_.Initialize(db))
    return false;

  // Create the unified activity log entry table.
  if (!ActivityDatabase::InitializeTable(db,
                                         kTableName,
                                         kTableContentFields,
                                         kTableFieldTypes,
                                         arraysize(kTableContentFields)))
    return false;

  // Create a view for easily accessing the uncompressed form of the data, and
  // any necessary indexes if needed.
  return db->Execute(kPolicyMiscSetup);
}

void CountingPolicy::ProcessAction(scoped_refptr<Action> action) {
  ScheduleAndForget(this, &CountingPolicy::QueueAction, action);
}

void CountingPolicy::QueueAction(scoped_refptr<Action> action) {
  if (activity_database()->is_db_valid()) {
    action = action->Clone();
    Util::StripPrivacySensitiveFields(action);
    Util::StripArguments(api_arg_whitelist_, action);
    queued_actions_.push_back(action);
    activity_database()->AdviseFlush(queued_actions_.size());
  }
}

bool CountingPolicy::FlushDatabase(sql::Connection* db) {
  // Columns that must match exactly for database rows to be coalesced.
  static const char* matched_columns[] = {
      "extension_id_x", "action_type", "api_name_x", "args_x", "page_url_x",
      "page_title_x", "arg_url_x", "other_x"};
  Action::ActionVector queue;
  queue.swap(queued_actions_);

  // Whether to clean old records out of the activity log database.  Do this
  // much less frequently than database flushes since it is expensive, but
  // always check on the first database flush (since there might be a large
  // amount of data to clear).
  bool clean_database = (last_database_cleaning_time_.is_null() ||
                         Now() - last_database_cleaning_time_ >
                             base::TimeDelta::FromHours(kCleaningDelayInHours));

  if (queue.empty() && !clean_database)
    return true;

  sql::Transaction transaction(db);
  if (!transaction.Begin())
    return false;

  std::string insert_str =
      "INSERT INTO " + std::string(kTableName) + "(count, time";
  std::string update_str =
      "UPDATE " + std::string(kTableName) +
      " SET count = count + 1, time = max(?, time)"
      " WHERE time >= ? AND time < ?";

  for (size_t i = 0; i < arraysize(matched_columns); i++) {
    insert_str =
        base::StringPrintf("%s, %s", insert_str.c_str(), matched_columns[i]);
    update_str = base::StringPrintf(
        "%s AND %s IS ?", update_str.c_str(), matched_columns[i]);
  }
  insert_str += ") VALUES (1, ?";
  for (size_t i = 0; i < arraysize(matched_columns); i++) {
    insert_str += ", ?";
  }
  insert_str += ")";

  Action::ActionVector::size_type i;
  for (i = 0; i != queue.size(); ++i) {
    const Action& action = *queue[i];

    base::Time day_start = action.time().LocalMidnight();
    base::Time next_day = Util::AddDays(day_start, 1);

    // The contents in values must match up with fields in matched_columns.  A
    // value of -1 is used to encode a null database value.
    int64 id;
    std::vector<int64> matched_values;

    if (!string_table_.StringToInt(db, action.extension_id(), &id))
      return false;
    matched_values.push_back(id);

    matched_values.push_back(static_cast<int>(action.action_type()));

    if (!string_table_.StringToInt(db, action.api_name(), &id))
      return false;
    matched_values.push_back(id);

    if (action.args()) {
      std::string args = Util::Serialize(action.args());
      // TODO(mvrable): For now, truncate long argument lists.  This is a
      // workaround for excessively-long values coming from DOM logging.  When
      // the V8ValueConverter is fixed to return more reasonable values, we can
      // drop the truncation.
      if (args.length() > 10000) {
        args = "[\"<too_large>\"]";
      }
      if (!string_table_.StringToInt(db, args, &id))
        return false;
      matched_values.push_back(id);
    } else {
      matched_values.push_back(-1);
    }

    std::string page_url_string = action.SerializePageUrl();
    if (!page_url_string.empty()) {
      if (!url_table_.StringToInt(db, page_url_string, &id))
        return false;
      matched_values.push_back(id);
    } else {
      matched_values.push_back(-1);
    }

    // TODO(mvrable): Create a title_table_?
    if (!action.page_title().empty()) {
      if (!string_table_.StringToInt(db, action.page_title(), &id))
        return false;
      matched_values.push_back(id);
    } else {
      matched_values.push_back(-1);
    }

    std::string arg_url_string = action.SerializeArgUrl();
    if (!arg_url_string.empty()) {
      if (!url_table_.StringToInt(db, arg_url_string, &id))
        return false;
      matched_values.push_back(id);
    } else {
      matched_values.push_back(-1);
    }

    if (action.other()) {
      if (!string_table_.StringToInt(db, Util::Serialize(action.other()), &id))
        return false;
      matched_values.push_back(id);
    } else {
      matched_values.push_back(-1);
    }

    // Assume there is an existing row for this action, and try to update the
    // count.
    sql::Statement update_statement(db->GetCachedStatement(
        sql::StatementID(SQL_FROM_HERE), update_str.c_str()));
    update_statement.BindInt64(0, action.time().ToInternalValue());
    update_statement.BindInt64(1, day_start.ToInternalValue());
    update_statement.BindInt64(2, next_day.ToInternalValue());
    for (size_t j = 0; j < matched_values.size(); j++) {
      // A call to BindNull when matched_values contains -1 is likely not
      // necessary as parameters default to null before they are explicitly
      // bound.  But to be completely clear, and in case a cached statement
      // ever comes with some values already bound, we bind all parameters
      // (even null ones) explicitly.
      if (matched_values[j] == -1)
        update_statement.BindNull(j + 3);
      else
        update_statement.BindInt64(j + 3, matched_values[j]);
    }
    if (!update_statement.Run())
      return false;

    // Check if the update succeeded (was the count of updated rows non-zero)?
    // If it failed because no matching row existed, fall back to inserting a
    // new record.
    if (db->GetLastChangeCount() > 0) {
      if (db->GetLastChangeCount() > 1) {
        LOG(WARNING) << "Found and updated multiple rows in the activity log "
                     << "database; counts may be off!";
      }
      continue;
    }
    sql::Statement insert_statement(db->GetCachedStatement(
        sql::StatementID(SQL_FROM_HERE), insert_str.c_str()));
    insert_statement.BindInt64(0, action.time().ToInternalValue());
    for (size_t j = 0; j < matched_values.size(); j++) {
      if (matched_values[j] == -1)
        insert_statement.BindNull(j + 1);
      else
        insert_statement.BindInt64(j + 1, matched_values[j]);
    }
    if (!insert_statement.Run())
      return false;
  }

  if (clean_database) {
    base::Time cutoff = (Now() - retention_time()).LocalMidnight();
    if (!CleanOlderThan(db, cutoff))
      return false;
    last_database_cleaning_time_ = Now();
  }

  if (!transaction.Commit())
    return false;

  return true;
}

scoped_ptr<Action::ActionVector> CountingPolicy::DoReadData(
    const std::string& extension_id,
    const int days_ago) {
  // Ensure data is flushed to the database first so that we query over all
  // data.
  activity_database()->AdviseFlush(ActivityDatabase::kFlushImmediately);

  DCHECK_GE(days_ago, 0);
  scoped_ptr<Action::ActionVector> actions(new Action::ActionVector());

  sql::Connection* db = GetDatabaseConnection();
  if (!db) {
    return actions.Pass();
  }

  int64 early_bound;
  int64 late_bound;
  Util::ComputeDatabaseTimeBounds(Now(), days_ago, &early_bound, &late_bound);
  std::string query_str = base::StringPrintf(
      "SELECT time, action_type, api_name, args, page_url, page_title, "
      "arg_url, other, count "
      "FROM %s WHERE extension_id=? AND time>? AND time<=? "
      "ORDER BY time DESC",
      kReadViewName);
  sql::Statement query(db->GetCachedStatement(SQL_FROM_HERE,
                                              query_str.c_str()));
  query.BindString(0, extension_id);
  query.BindInt64(1, early_bound);
  query.BindInt64(2, late_bound);

  while (query.is_valid() && query.Step()) {
    scoped_refptr<CountedAction> action =
        new CountedAction(extension_id,
                          base::Time::FromInternalValue(query.ColumnInt64(0)),
                          static_cast<Action::ActionType>(query.ColumnInt(1)),
                          query.ColumnString(2));

    if (query.ColumnType(3) != sql::COLUMN_TYPE_NULL) {
      scoped_ptr<Value> parsed_value(
          base::JSONReader::Read(query.ColumnString(3)));
      if (parsed_value && parsed_value->IsType(Value::TYPE_LIST)) {
        action->set_args(
            make_scoped_ptr(static_cast<ListValue*>(parsed_value.release())));
      } else {
        LOG(WARNING) << "Unable to parse args: '" << query.ColumnString(3)
                     << "'";
      }
    }

    action->ParsePageUrl(query.ColumnString(4));
    action->set_page_title(query.ColumnString(5));
    action->ParseArgUrl(query.ColumnString(6));

    if (query.ColumnType(7) != sql::COLUMN_TYPE_NULL) {
      scoped_ptr<Value> parsed_value(
          base::JSONReader::Read(query.ColumnString(7)));
      if (parsed_value && parsed_value->IsType(Value::TYPE_DICTIONARY)) {
        action->set_other(make_scoped_ptr(
            static_cast<DictionaryValue*>(parsed_value.release())));
      } else {
        LOG(WARNING) << "Unable to parse other: '" << query.ColumnString(7)
                     << "'";
      }
    }

    action->set_count(query.ColumnInt(8));

    actions->push_back(action);
  }

  return actions.Pass();
}

void CountingPolicy::ReadData(
    const std::string& extension_id,
    const int day,
    const base::Callback<void(scoped_ptr<Action::ActionVector>)>& callback) {
  BrowserThread::PostTaskAndReplyWithResult(
      BrowserThread::DB,
      FROM_HERE,
      base::Bind(&CountingPolicy::DoReadData,
                 base::Unretained(this),
                 extension_id,
                 day),
      callback);
}

void CountingPolicy::OnDatabaseFailure() {
  queued_actions_.clear();
}

void CountingPolicy::OnDatabaseClose() {
  delete this;
}

// Cleans old records from the activity log database.
bool CountingPolicy::CleanOlderThan(sql::Connection* db,
                                    const base::Time& cutoff) {
  std::string clean_statement =
      "DELETE FROM " + std::string(kTableName) + " WHERE time < ?";
  sql::Statement cleaner(db->GetCachedStatement(sql::StatementID(SQL_FROM_HERE),
                                                clean_statement.c_str()));
  cleaner.BindInt64(0, cutoff.ToInternalValue());
  if (!cleaner.Run())
    return false;
  return CleanStringTables(db);
}

// Cleans unused interned strings from the database.  This should be run after
// deleting rows from the main log table to clean out stale values.
bool CountingPolicy::CleanStringTables(sql::Connection* db) {
  sql::Statement cleaner1(db->GetCachedStatement(
      sql::StatementID(SQL_FROM_HERE), kStringTableCleanup));
  if (!cleaner1.Run())
    return false;
  if (db->GetLastChangeCount() > 0)
    string_table_.ClearCache();

  sql::Statement cleaner2(db->GetCachedStatement(
      sql::StatementID(SQL_FROM_HERE), kUrlTableCleanup));
  if (!cleaner2.Run())
    return false;
  if (db->GetLastChangeCount() > 0)
    url_table_.ClearCache();

  return true;
}

void CountingPolicy::Close() {
  // The policy object should have never been created if there's no DB thread.
  DCHECK(BrowserThread::IsMessageLoopValid(BrowserThread::DB));
  ScheduleAndForget(activity_database(), &ActivityDatabase::Close);
}

}  // namespace extensions
