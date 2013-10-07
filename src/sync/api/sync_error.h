// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_API_SYNC_ERROR_H_
#define SYNC_API_SYNC_ERROR_H_

#include <iosfwd>
#include <string>

#include "base/memory/scoped_ptr.h"
#include "sync/base/sync_export.h"
#include "sync/internal_api/public/base/model_type.h"

namespace tracked_objects {
class Location;
}  // namespace tracked_objects

namespace syncer {

// Sync errors are used for debug purposes and handled internally and/or
// exposed through Chrome's "about:sync" internal page.
// This class is copy-friendly and thread-safe.
class SYNC_EXPORT SyncError {
 public:
  // Error types are used to distinguish general datatype errors (which result
  // in the datatype being disabled) from actionable sync errors (which might
  // have more complicated results).
  enum ErrorType {
    UNSET,                // No error.
    UNRECOVERABLE_ERROR,  // An unrecoverable runtime error was encountered, and
                          // sync should be disabled completely.
    DATATYPE_ERROR,       // A datatype error was encountered, and the datatype
                          // should be disabled.
    PERSISTENCE_ERROR,    // A persistence error was detected, and the
                          // datataype should be associated after a sync update.
    CRYPTO_ERROR,         // A cryptographer error was detected, and the
                          // datatype should be associated after it is resolved.
  };

  // Default constructor refers to "no error", and IsSet() will return false.
  SyncError();

  // Create a new Sync error of type |error_type| triggered by |model_type|
  // from the specified location. IsSet() will return true afterward. Will
  // create and print an error specific message to LOG(ERROR).
  SyncError(const tracked_objects::Location& location,
            ErrorType error_type,
            const std::string& message,
            ModelType model_type);

  // Copy and assign via deep copy.
  SyncError(const SyncError& other);
  SyncError& operator=(const SyncError& other);

  ~SyncError();

  // Reset the current error to a new datatype error. May be called
  // irrespective of whether IsSet() is true. After this is called, IsSet()
  // will return true.
  // Will print the new error to LOG(ERROR).
  void Reset(const tracked_objects::Location& location,
             const std::string& message,
             ModelType type);

  // Whether this is a valid error or not.
  bool IsSet() const;

  // These must only be called if IsSet() is true.
  const tracked_objects::Location& location() const;
  const std::string& message() const;
  ModelType model_type() const;
  ErrorType error_type() const;

  // Returns empty string is IsSet() is false.
  std::string ToString() const;
 private:
  // Print error information to log.
  void PrintLogError() const;

  // Make a copy of a SyncError. If other.IsSet() == false, this->IsSet() will
  // now return false.
  void Copy(const SyncError& other);

  // Initialize the local error data with the specified error data. After this
  // is called, IsSet() will return true.
  void Init(const tracked_objects::Location& location,
            const std::string& message,
            ModelType model_type,
            ErrorType error_type);

  // Reset the error to it's default (unset) values.
  void Clear();

  // scoped_ptr is necessary because Location objects aren't assignable.
  scoped_ptr<tracked_objects::Location> location_;
  std::string message_;
  ModelType model_type_;
  ErrorType error_type_;
};

// gmock printer helper.
SYNC_EXPORT void PrintTo(const SyncError& sync_error, std::ostream* os);

}  // namespace syncer

#endif  // SYNC_API_SYNC_ERROR_H_
