// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/push_messaging/push_messaging_invalidation_handler.h"

#include <algorithm>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "chrome/browser/extensions/api/push_messaging/push_messaging_invalidation_handler_delegate.h"
#include "chrome/browser/invalidation/invalidation_service.h"
#include "chrome/common/extensions/extension.h"
#include "google/cacheinvalidation/types.pb.h"

namespace extensions {

namespace {

const int kNumberOfSubchannels = 4;

// Chrome push messaging object IDs currently have the following format:
// <format type>/<GAIA ID>/<extension ID>/<subchannel>
// <format type> must be 'U', and <GAIA ID> is handled server-side so the client
// never sees it.
syncer::ObjectIdSet ExtensionIdToObjectIds(const std::string& extension_id) {
  syncer::ObjectIdSet object_ids;
  for (int i = 0; i < kNumberOfSubchannels; ++i) {
    std::string name("U/");
    name += extension_id;
    name += "/";
    name += base::IntToString(i);
    object_ids.insert(invalidation::ObjectId(
        ipc::invalidation::ObjectSource::CHROME_PUSH_MESSAGING,
        name));
  }
  return object_ids;
}

// Returns true iff the conversion was successful.
bool ObjectIdToExtensionAndSubchannel(const invalidation::ObjectId& object_id,
                                      std::string* extension_id,
                                      int* subchannel) {
  if (object_id.source() !=
      ipc::invalidation::ObjectSource::CHROME_PUSH_MESSAGING) {
    DLOG(WARNING) << "Invalid source: " << object_id.source();
    return false;
  }

  const std::string& name = object_id.name();
  std::vector<std::string> components;
  base::SplitStringDontTrim(name, '/', &components);
  if (components.size() < 3) {
    DLOG(WARNING) << "Invalid format type from object name " << name;
    return false;
  }
  if (components[0] != "U") {
    DLOG(WARNING) << "Invalid format type from object name " << name;
    return false;
  }
  if (!Extension::IdIsValid(components[1])) {
    DLOG(WARNING) << "Invalid extension ID from object name " << name;
    return false;
  }
  *extension_id = components[1];
  if (!base::StringToInt(components[2], subchannel)) {
    DLOG(WARNING) << "Subchannel not a number from object name " << name;
    return false;
  }
  if (*subchannel < 0 || *subchannel >= kNumberOfSubchannels) {
    DLOG(WARNING) << "Subchannel out of range from object name " << name;
    return false;
  }
  return true;
}

}  // namespace

PushMessagingInvalidationHandler::PushMessagingInvalidationHandler(
    invalidation::InvalidationService* service,
    PushMessagingInvalidationHandlerDelegate* delegate)
    : service_(service),
      delegate_(delegate) {
  DCHECK(service_);
  service_->RegisterInvalidationHandler(this);
}

PushMessagingInvalidationHandler::~PushMessagingInvalidationHandler() {
  DCHECK(thread_checker_.CalledOnValidThread());
  service_->UnregisterInvalidationHandler(this);
}

void PushMessagingInvalidationHandler::SuppressInitialInvalidationsForExtension(
    const std::string& extension_id) {
  DCHECK(thread_checker_.CalledOnValidThread());
  const syncer::ObjectIdSet& suppressed_ids =
      ExtensionIdToObjectIds(extension_id);
  suppressed_ids_.insert(suppressed_ids.begin(), suppressed_ids.end());
}

void PushMessagingInvalidationHandler::RegisterExtension(
    const std::string& extension_id) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(Extension::IdIsValid(extension_id));
  registered_extensions_.insert(extension_id);
  UpdateRegistrations();
}

void PushMessagingInvalidationHandler::UnregisterExtension(
    const std::string& extension_id) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(Extension::IdIsValid(extension_id));
  registered_extensions_.erase(extension_id);
  UpdateRegistrations();
}

void PushMessagingInvalidationHandler::OnInvalidatorStateChange(
    syncer::InvalidatorState state) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // Nothing to do.
}

void PushMessagingInvalidationHandler::OnIncomingInvalidation(
    const syncer::ObjectIdInvalidationMap& invalidation_map) {
  DCHECK(thread_checker_.CalledOnValidThread());
  for (syncer::ObjectIdInvalidationMap::const_iterator it =
           invalidation_map.begin(); it != invalidation_map.end(); ++it) {
    service_->AcknowledgeInvalidation(it->first, it->second.ack_handle);

    syncer::ObjectIdSet::iterator suppressed_id =
        suppressed_ids_.find(it->first);
    if (suppressed_id != suppressed_ids_.end()) {
      suppressed_ids_.erase(suppressed_id);
      continue;
    }

    std::string extension_id;
    int subchannel;
    if (ObjectIdToExtensionAndSubchannel(it->first,
                                         &extension_id,
                                         &subchannel)) {
      delegate_->OnMessage(extension_id, subchannel, it->second.payload);
    }
  }
}

void PushMessagingInvalidationHandler::UpdateRegistrations() {
  syncer::ObjectIdSet ids;
  for (std::set<std::string>::const_iterator it =
       registered_extensions_.begin(); it != registered_extensions_.end();
       ++it) {
    const syncer::ObjectIdSet& object_ids = ExtensionIdToObjectIds(*it);
    ids.insert(object_ids.begin(), object_ids.end());
  }
  service_->UpdateRegisteredInvalidationIds(this, ids);
}

}  // namespace extensions
