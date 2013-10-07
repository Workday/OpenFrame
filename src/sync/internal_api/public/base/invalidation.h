// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_INTERNAL_API_PUBLIC_BASE_INVALIDATION_H_
#define SYNC_INTERNAL_API_PUBLIC_BASE_INVALIDATION_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "sync/base/sync_export.h"

namespace base {
class DictionaryValue;
}  // namespace

namespace syncer {

// Opaque class that represents a local ack handle. We don't reuse the
// invalidation ack handles to avoid unnecessary dependencies.
class SYNC_EXPORT AckHandle {
 public:
  static AckHandle CreateUnique();
  static AckHandle InvalidAckHandle();

  bool Equals(const AckHandle& other) const;

  scoped_ptr<base::DictionaryValue> ToValue() const;
  bool ResetFromValue(const base::DictionaryValue& value);

  bool IsValid() const;

  ~AckHandle();

 private:
  // Explicitly copyable and assignable for STL containers.
  AckHandle(const std::string& state, base::Time timestamp);

  std::string state_;
  base::Time timestamp_;
};

// Represents a local invalidation, and is roughly analogous to
// invalidation::Invalidation. It contains a version (which may be
// kUnknownVersion), a payload (which may be empty) and an
// associated ack handle that an InvalidationHandler implementation can use to
// acknowledge receipt of the invalidation. It does not embed the object ID,
// since it is typically associated with it through ObjectIdInvalidationMap.
struct SYNC_EXPORT Invalidation {
  static const int64 kUnknownVersion;

  Invalidation();
  ~Invalidation();

  bool Equals(const Invalidation& other) const;

  scoped_ptr<base::DictionaryValue> ToValue() const;
  bool ResetFromValue(const base::DictionaryValue& value);

  int64 version;
  std::string payload;
  AckHandle ack_handle;
};

}  // namespace syncer

#endif  // SYNC_INTERNAL_API_PUBLIC_BASE_INVALIDATION_H_
