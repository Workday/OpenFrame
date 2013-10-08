// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_ACTIVITY_LOG_ACTIVITY_ACTIONS_H_
#define CHROME_BROWSER_EXTENSIONS_ACTIVITY_LOG_ACTIVITY_ACTIONS_H_

#include <string>
#include <vector>

#include "base/memory/ref_counted_memory.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/activity_log_private.h"
#include "sql/connection.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "url/gurl.h"

namespace extensions {

// This is the interface for extension actions that are to be recorded in
// the activity log.
class Action : public base::RefCountedThreadSafe<Action> {
 public:
  // Types of log entries that can be stored.  The numeric values are stored in
  // the database, so keep them stable.  Append values only.
  enum ActionType {
    ACTION_API_CALL = 0,
    ACTION_API_EVENT = 1,
    ACTION_API_BLOCKED = 2,
    ACTION_CONTENT_SCRIPT = 3,
    ACTION_DOM_ACCESS = 4,
    ACTION_DOM_EVENT = 5,
    ACTION_WEB_REQUEST = 6,
  };

  // A useful shorthand for methods that take or return collections of Action
  // objects.
  typedef std::vector<scoped_refptr<Action> > ActionVector;

  // Creates a new activity log Action object.  The extension_id, time, and
  // type are immutable.  All other fields can be filled in with the
  // accessors/mutators below.
  Action(const std::string& extension_id,
         const base::Time& time,
         const ActionType action_type,
         const std::string& api_name);

  // Creates and returns a mutable copy of an Action.
  scoped_refptr<Action> Clone() const;

  // The extension which caused this record to be generated.
  const std::string& extension_id() const { return extension_id_; }

  // The time the record was generated (or some approximation).
  const base::Time& time() const { return time_; }

  // The ActionType distinguishes different classes of actions that can be
  // logged, and determines which other fields are expected to be filled in.
  ActionType action_type() const { return action_type_; }

  // The specific API call used or accessed, for example "chrome.tabs.get".
  const std::string& api_name() const { return api_name_; }
  void set_api_name(const std::string api_name) { api_name_ = api_name; }

  // Any applicable arguments.  This might be null to indicate no data
  // available (a distinct condition from an empty argument list).
  // mutable_args() returns a pointer to the list stored in the Action which
  // can be modified in place; if the list was null an empty list is created
  // first.
  const ListValue* args() const { return args_.get(); }
  void set_args(scoped_ptr<ListValue> args);
  ListValue* mutable_args();

  // The URL of the page which was modified or accessed.
  const GURL& page_url() const { return page_url_; }
  void set_page_url(const GURL& page_url);

  // The title of the above page if available.
  const std::string& page_title() const { return page_title_; }
  void set_page_title(const std::string& title) { page_title_ = title; }

  // A URL which appears in the arguments of the API call, if present.
  const GURL& arg_url() const { return arg_url_; }
  void set_arg_url(const GURL& arg_url);

  // Get or set a flag indicating whether the page or argument values above
  // refer to incognito pages.
  bool page_incognito() const { return page_incognito_; }
  void set_page_incognito(bool incognito) { page_incognito_ = incognito; }
  bool arg_incognito() const { return arg_incognito_; }
  void set_arg_incognito(bool incognito) { arg_incognito_ = incognito; }

  // A dictionary where any additional data can be stored.
  const DictionaryValue* other() const { return other_.get(); }
  void set_other(scoped_ptr<DictionaryValue> other);
  DictionaryValue* mutable_other();

  // Helper methods for serializing and deserializing URLs into strings.  If
  // the URL is marked as incognito, then the string is prefixed with
  // kIncognitoUrl ("<incognito>").
  std::string SerializePageUrl() const;
  void ParsePageUrl(const std::string& url);
  std::string SerializeArgUrl() const;
  void ParseArgUrl(const std::string& url);

  // Flatten the activity's type-specific fields into an ExtensionActivity.
  scoped_ptr<api::activity_log_private::ExtensionActivity>
      ConvertToExtensionActivity();

  // Print an action as a regular string for debugging purposes.
  virtual std::string PrintForDebug() const;

 protected:
  virtual ~Action();

 private:
  friend class base::RefCountedThreadSafe<Action>;

  std::string extension_id_;
  base::Time time_;
  api::activity_log_private::ExtensionActivity::ActivityType activity_type_;
  ActionType action_type_;
  std::string api_name_;
  scoped_ptr<ListValue> args_;
  GURL page_url_;
  std::string page_title_;
  bool page_incognito_;
  GURL arg_url_;
  bool arg_incognito_;
  scoped_ptr<DictionaryValue> other_;

  DISALLOW_COPY_AND_ASSIGN(Action);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_ACTIVITY_LOG_ACTIVITY_ACTIONS_H_
