// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_HISTORY_ARCHIVED_DATABASE_H_
#define CHROME_BROWSER_HISTORY_ARCHIVED_DATABASE_H_

#include "base/basictypes.h"
#include "chrome/browser/history/url_database.h"
#include "chrome/browser/history/visit_database.h"
#include "sql/connection.h"
#include "sql/init_status.h"
#include "sql/meta_table.h"

namespace base {
class FilePath;
}

namespace history {

// Encapsulates the database operations for archived history.
//
// IMPORTANT NOTE: The IDs in this system for URLs and visits will be
// different than those in the main database. This is to eliminate the
// dependency between them so we can deal with each one on its own.
class ArchivedDatabase : public URLDatabase,
                         public VisitDatabase {
 public:
  // Must call Init() before using other members.
  ArchivedDatabase();
  virtual ~ArchivedDatabase();

  // Initializes the database connection. This must return true before any other
  // functions on this class are called.
  bool Init(const base::FilePath& file_name);

  // Try to trim the cache memory used by the database.  If |aggressively| is
  // true try to trim all unused cache, otherwise trim by half.
  void TrimMemory(bool aggressively);

  // Transactions on the database. We support nested transactions and only
  // commit when the outermost one is committed (sqlite doesn't support true
  // nested transactions).
  void BeginTransaction();
  void CommitTransaction();

  // Returns the current version that we will generate archived databases with.
  static int GetCurrentVersion();

 private:
  bool InitTables();

  // Implemented for the specialized databases.
  virtual sql::Connection& GetDB() OVERRIDE;

  // Makes sure the version is up-to-date, updating if necessary. If the
  // database is too old to migrate, the user will be notified. In this case, or
  // for other errors, false will be returned. True means it is up-to-date and
  // ready for use.
  //
  // This assumes it is called from the init function inside a transaction. It
  // may commit the transaction and start a new one if migration requires it.
  sql::InitStatus EnsureCurrentVersion();

  // The database.
  sql::Connection db_;
  sql::MetaTable meta_table_;

  DISALLOW_COPY_AND_ASSIGN(ArchivedDatabase);
};

}  // namespace history

#endif  // CHROME_BROWSER_HISTORY_ARCHIVED_DATABASE_H_
