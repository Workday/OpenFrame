// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_ACTIVITY_LOG_FULLSTREAM_UI_POLICY_H_
#define CHROME_BROWSER_EXTENSIONS_ACTIVITY_LOG_FULLSTREAM_UI_POLICY_H_

#include <string>

#include "chrome/browser/extensions/activity_log/activity_database.h"
#include "chrome/browser/extensions/activity_log/activity_log_policy.h"

class GURL;

namespace extensions {

// A policy for logging the full stream of actions, including all arguments.
// It's mostly intended to be used in testing and analysis.
//
// NOTE: The FullStreamUIPolicy deliberately keeps almost all information,
// including some data that could be privacy sensitive (full URLs including
// incognito URLs, full headers when WebRequest is used, etc.).  It should not
// be used during normal browsing if users care about privacy.
class FullStreamUIPolicy : public ActivityLogDatabasePolicy {
 public:
  // For more info about these member functions, see the super class.
  explicit FullStreamUIPolicy(Profile* profile);

  virtual void ProcessAction(scoped_refptr<Action> action) OVERRIDE;

  // TODO(felt,dbabic) This is overly specific to FullStreamUIPolicy.
  // It assumes that the callback can return a sorted vector of actions.  Some
  // policies might not do that.  For instance, imagine a trivial policy that
  // just counts the frequency of certain actions within some time period,
  // this call would be meaningless, as it couldn't return anything useful.
  virtual void ReadData(
      const std::string& extension_id,
      const int day,
      const base::Callback
          <void(scoped_ptr<Action::ActionVector>)>& callback) OVERRIDE;

  virtual void Close() OVERRIDE;

  // Database table schema.
  static const char* kTableName;
  static const char* kTableContentFields[];
  static const char* kTableFieldTypes[];
  static const int kTableFieldCount;

 protected:
  // Only ever run by OnDatabaseClose() below; see the comments on the
  // ActivityDatabase class for an overall discussion of how cleanup works.
  virtual ~FullStreamUIPolicy();

  // The ActivityDatabase::Delegate interface.  These are always called from
  // the database thread.
  virtual bool InitDatabase(sql::Connection* db) OVERRIDE;
  virtual bool FlushDatabase(sql::Connection* db) OVERRIDE;
  virtual void OnDatabaseFailure() OVERRIDE;
  virtual void OnDatabaseClose() OVERRIDE;

  // Strips arguments if needed by policy.  May return the original object (if
  // unmodified), or a copy (if modifications were made).  The implementation
  // in FullStreamUIPolicy returns the action unmodified.
  virtual scoped_refptr<Action> ProcessArguments(
      scoped_refptr<Action> action) const;

  // Tracks any pending updates to be written to the database, if write
  // batching is turned on.  Should only be accessed from the database thread.
  Action::ActionVector queued_actions_;

 private:
  // Adds an Action to queued_actions_; this should be invoked only on the
  // database thread.
  void QueueAction(scoped_refptr<Action> action);

  // The implementation of ReadData; this must only run on the database thread.
  scoped_ptr<Action::ActionVector> DoReadData(const std::string& extension_id,
                                              const int days_ago);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_ACTIVITY_LOG_FULLSTREAM_UI_POLICY_H_
