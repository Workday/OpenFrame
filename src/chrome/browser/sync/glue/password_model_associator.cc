// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/password_model_associator.h"

#include <set>

#include "base/location.h"
#include "base/metrics/histogram.h"
#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/password_manager/password_store.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "content/public/common/password_form.h"
#include "net/base/escape.h"
#include "sync/api/sync_error.h"
#include "sync/internal_api/public/read_node.h"
#include "sync/internal_api/public/read_transaction.h"
#include "sync/internal_api/public/write_node.h"
#include "sync/internal_api/public/write_transaction.h"
#include "sync/protocol/password_specifics.pb.h"

using content::BrowserThread;

namespace browser_sync {

const char kPasswordTag[] = "google_chrome_passwords";

PasswordModelAssociator::PasswordModelAssociator(
    ProfileSyncService* sync_service,
    PasswordStore* password_store,
    DataTypeErrorHandler* error_handler)
    : sync_service_(sync_service),
      password_store_(password_store),
      password_node_id_(syncer::kInvalidId),
      abort_association_requested_(false),
      expected_loop_(base::MessageLoop::current()),
      error_handler_(error_handler) {
  DCHECK(sync_service_);
#if defined(OS_MACOSX)
  DCHECK(!BrowserThread::CurrentlyOn(BrowserThread::UI));
#else
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
#endif
}

PasswordModelAssociator::~PasswordModelAssociator() {
  DCHECK(!BrowserThread::CurrentlyOn(BrowserThread::UI));
}

syncer::SyncError PasswordModelAssociator::AssociateModels(
    syncer::SyncMergeResult* local_merge_result,
    syncer::SyncMergeResult* syncer_merge_result) {
  DCHECK(expected_loop_ == base::MessageLoop::current());

  // We must not be holding a transaction when we interact with the password
  // store, as it can post tasks to the UI thread which can itself be blocked
  // on our transaction, resulting in deadlock. (http://crbug.com/70658)
  std::vector<content::PasswordForm*> passwords;
  if (!password_store_->FillAutofillableLogins(&passwords) ||
      !password_store_->FillBlacklistLogins(&passwords)) {
    STLDeleteElements(&passwords);

    // Password store often fails to load passwords. Track failures with UMA.
    // (http://crbug.com/249000)
    UMA_HISTOGRAM_ENUMERATION("Sync.LocalDataFailedToLoad",
                              ModelTypeToHistogramInt(syncer::PASSWORDS),
                              syncer::MODEL_TYPE_COUNT);
    return syncer::SyncError(FROM_HERE,
                             syncer::SyncError::DATATYPE_ERROR,
                             "Could not get the password entries.",
                             model_type());
  }

  PasswordVector new_passwords;
  PasswordVector updated_passwords;
  {
    base::AutoLock lock(association_lock_);
    if (abort_association_requested_)
      return syncer::SyncError();

    std::set<std::string> current_passwords;
    syncer::WriteTransaction trans(FROM_HERE, sync_service_->GetUserShare());
    syncer::ReadNode password_root(&trans);
    if (password_root.InitByTagLookup(kPasswordTag) !=
            syncer::BaseNode::INIT_OK) {
      return error_handler_->CreateAndUploadError(
          FROM_HERE,
          "Server did not create the top-level password node. We "
          "might be running against an out-of-date server.",
          model_type());
    }

    for (std::vector<content::PasswordForm*>::iterator ix =
             passwords.begin();
         ix != passwords.end(); ++ix) {
      std::string tag = MakeTag(**ix);

      syncer::ReadNode node(&trans);
      if (node.InitByClientTagLookup(syncer::PASSWORDS, tag) ==
              syncer::BaseNode::INIT_OK) {
        const sync_pb::PasswordSpecificsData& password =
            node.GetPasswordSpecifics();
        DCHECK_EQ(tag, MakeTag(password));

        content::PasswordForm new_password;

        if (MergePasswords(password, **ix, &new_password)) {
          syncer::WriteNode write_node(&trans);
          if (write_node.InitByClientTagLookup(syncer::PASSWORDS, tag) !=
                  syncer::BaseNode::INIT_OK) {
            STLDeleteElements(&passwords);
            return error_handler_->CreateAndUploadError(
                FROM_HERE,
                "Failed to edit password sync node.",
                model_type());
          }
          WriteToSyncNode(new_password, &write_node);
          updated_passwords.push_back(new_password);
        }

        Associate(&tag, node.GetId());
      } else {
        syncer::WriteNode node(&trans);
        syncer::WriteNode::InitUniqueByCreationResult result =
            node.InitUniqueByCreation(syncer::PASSWORDS, password_root, tag);
        if (result != syncer::WriteNode::INIT_SUCCESS) {
          STLDeleteElements(&passwords);
          return error_handler_->CreateAndUploadError(
              FROM_HERE,
              "Failed to create password sync node.",
              model_type());
        }

        WriteToSyncNode(**ix, &node);

        Associate(&tag, node.GetId());
      }

      current_passwords.insert(tag);
    }

    STLDeleteElements(&passwords);

    int64 sync_child_id = password_root.GetFirstChildId();
    while (sync_child_id != syncer::kInvalidId) {
      syncer::ReadNode sync_child_node(&trans);
      if (sync_child_node.InitByIdLookup(sync_child_id) !=
              syncer::BaseNode::INIT_OK) {
        return error_handler_->CreateAndUploadError(
            FROM_HERE,
            "Failed to fetch child node.",
            model_type());
      }
      const sync_pb::PasswordSpecificsData& password =
          sync_child_node.GetPasswordSpecifics();
      std::string tag = MakeTag(password);

      // The password only exists on the server.  Add it to the local
      // model.
      if (current_passwords.find(tag) == current_passwords.end()) {
        content::PasswordForm new_password;

        CopyPassword(password, &new_password);
        Associate(&tag, sync_child_node.GetId());
        new_passwords.push_back(new_password);
      }

      sync_child_id = sync_child_node.GetSuccessorId();
    }
  }

  // We must not be holding a transaction when we interact with the password
  // store, as it can post tasks to the UI thread which can itself be blocked
  // on our transaction, resulting in deadlock. (http://crbug.com/70658)
  return WriteToPasswordStore(&new_passwords,
                              &updated_passwords,
                              NULL);
}

bool PasswordModelAssociator::DeleteAllNodes(
    syncer::WriteTransaction* trans) {
  DCHECK(expected_loop_ == base::MessageLoop::current());
  for (PasswordToSyncIdMap::iterator node_id = id_map_.begin();
       node_id != id_map_.end(); ++node_id) {
    syncer::WriteNode sync_node(trans);
    if (sync_node.InitByIdLookup(node_id->second) !=
            syncer::BaseNode::INIT_OK) {
      LOG(ERROR) << "Typed url node lookup failed.";
      return false;
    }
    sync_node.Tombstone();
  }

  id_map_.clear();
  id_map_inverse_.clear();
  return true;
}

syncer::SyncError PasswordModelAssociator::DisassociateModels() {
  id_map_.clear();
  id_map_inverse_.clear();
  return syncer::SyncError();
}

bool PasswordModelAssociator::SyncModelHasUserCreatedNodes(bool* has_nodes) {
  DCHECK(has_nodes);
  *has_nodes = false;
  int64 password_sync_id;
  if (!GetSyncIdForTaggedNode(kPasswordTag, &password_sync_id)) {
    LOG(ERROR) << "Server did not create the top-level password node. We "
               << "might be running against an out-of-date server.";
    return false;
  }
  syncer::ReadTransaction trans(FROM_HERE, sync_service_->GetUserShare());

  syncer::ReadNode password_node(&trans);
  if (password_node.InitByIdLookup(password_sync_id) !=
          syncer::BaseNode::INIT_OK) {
    LOG(ERROR) << "Server did not create the top-level password node. We "
               << "might be running against an out-of-date server.";
    return false;
  }

  // The sync model has user created nodes if the password folder has any
  // children.
  *has_nodes = password_node.HasChildren();
  return true;
}

void PasswordModelAssociator::AbortAssociation() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  base::AutoLock lock(association_lock_);
  abort_association_requested_ = true;
}

bool PasswordModelAssociator::CryptoReadyIfNecessary() {
  // We only access the cryptographer while holding a transaction.
  syncer::ReadTransaction trans(FROM_HERE, sync_service_->GetUserShare());
  // We always encrypt passwords, so no need to check if encryption is enabled.
  return sync_service_->IsCryptographerReady(&trans);
}

const std::string* PasswordModelAssociator::GetChromeNodeFromSyncId(
    int64 sync_id) {
  return NULL;
}

bool PasswordModelAssociator::InitSyncNodeFromChromeId(
    const std::string& node_id,
    syncer::BaseNode* sync_node) {
  return false;
}

int64 PasswordModelAssociator::GetSyncIdFromChromeId(
    const std::string& password) {
  PasswordToSyncIdMap::const_iterator iter = id_map_.find(password);
  return iter == id_map_.end() ? syncer::kInvalidId : iter->second;
}

void PasswordModelAssociator::Associate(
    const std::string* password, int64 sync_id) {
  DCHECK(expected_loop_ == base::MessageLoop::current());
  DCHECK_NE(syncer::kInvalidId, sync_id);
  DCHECK(id_map_.find(*password) == id_map_.end());
  DCHECK(id_map_inverse_.find(sync_id) == id_map_inverse_.end());
  id_map_[*password] = sync_id;
  id_map_inverse_[sync_id] = *password;
}

void PasswordModelAssociator::Disassociate(int64 sync_id) {
  DCHECK(expected_loop_ == base::MessageLoop::current());
  SyncIdToPasswordMap::iterator iter = id_map_inverse_.find(sync_id);
  if (iter == id_map_inverse_.end())
    return;
  CHECK(id_map_.erase(iter->second));
  id_map_inverse_.erase(iter);
}

bool PasswordModelAssociator::GetSyncIdForTaggedNode(const std::string& tag,
                                                     int64* sync_id) {
  syncer::ReadTransaction trans(FROM_HERE, sync_service_->GetUserShare());
  syncer::ReadNode sync_node(&trans);
  if (sync_node.InitByTagLookup(tag.c_str()) != syncer::BaseNode::INIT_OK)
    return false;
  *sync_id = sync_node.GetId();
  return true;
}

syncer::SyncError PasswordModelAssociator::WriteToPasswordStore(
    const PasswordVector* new_passwords,
    const PasswordVector* updated_passwords,
    const PasswordVector* deleted_passwords) {
  if (new_passwords) {
    for (PasswordVector::const_iterator password = new_passwords->begin();
         password != new_passwords->end(); ++password) {
      password_store_->AddLoginImpl(*password);
    }
  }

  if (updated_passwords) {
    for (PasswordVector::const_iterator password = updated_passwords->begin();
         password != updated_passwords->end(); ++password) {
      password_store_->UpdateLoginImpl(*password);
    }
  }

  if (deleted_passwords) {
    for (PasswordVector::const_iterator password = deleted_passwords->begin();
         password != deleted_passwords->end(); ++password) {
      password_store_->RemoveLoginImpl(*password);
    }
  }

  if (new_passwords || updated_passwords || deleted_passwords) {
    // We have to notify password store observers of the change by hand since
    // we use internal password store interfaces to make changes synchronously.
    password_store_->PostNotifyLoginsChanged();
  }
  return syncer::SyncError();
}

// static
void PasswordModelAssociator::CopyPassword(
        const sync_pb::PasswordSpecificsData& password,
        content::PasswordForm* new_password) {
  new_password->scheme =
      static_cast<content::PasswordForm::Scheme>(password.scheme());
  new_password->signon_realm = password.signon_realm();
  new_password->origin = GURL(password.origin());
  new_password->action = GURL(password.action());
  new_password->username_element =
      UTF8ToUTF16(password.username_element());
  new_password->password_element =
      UTF8ToUTF16(password.password_element());
  new_password->username_value =
      UTF8ToUTF16(password.username_value());
  new_password->password_value =
      UTF8ToUTF16(password.password_value());
  new_password->ssl_valid = password.ssl_valid();
  new_password->preferred = password.preferred();
  new_password->date_created =
      base::Time::FromInternalValue(password.date_created());
  new_password->blacklisted_by_user =
      password.blacklisted();
}

// static
bool PasswordModelAssociator::MergePasswords(
        const sync_pb::PasswordSpecificsData& password,
        const content::PasswordForm& password_form,
        content::PasswordForm* new_password) {
  DCHECK(new_password);

  if (password.scheme() == password_form.scheme &&
      password_form.signon_realm == password.signon_realm() &&
      password_form.origin.spec() == password.origin() &&
      password_form.action.spec() == password.action() &&
      UTF16ToUTF8(password_form.username_element) ==
          password.username_element() &&
      UTF16ToUTF8(password_form.password_element) ==
          password.password_element() &&
      UTF16ToUTF8(password_form.username_value) ==
          password.username_value() &&
      UTF16ToUTF8(password_form.password_value) ==
          password.password_value() &&
      password.ssl_valid() == password_form.ssl_valid &&
      password.preferred() == password_form.preferred &&
      password.date_created() == password_form.date_created.ToInternalValue() &&
      password.blacklisted() == password_form.blacklisted_by_user) {
    return false;
  }

  // If the passwords differ, we take the one that was created more recently.
  if (base::Time::FromInternalValue(password.date_created()) <=
      password_form.date_created) {
    *new_password = password_form;
  } else {
    CopyPassword(password, new_password);
  }

  return true;
}

// static
void PasswordModelAssociator::WriteToSyncNode(
         const content::PasswordForm& password_form,
         syncer::WriteNode* node) {
  sync_pb::PasswordSpecificsData password;
  password.set_scheme(password_form.scheme);
  password.set_signon_realm(password_form.signon_realm);
  password.set_origin(password_form.origin.spec());
  password.set_action(password_form.action.spec());
  password.set_username_element(UTF16ToUTF8(password_form.username_element));
  password.set_password_element(UTF16ToUTF8(password_form.password_element));
  password.set_username_value(UTF16ToUTF8(password_form.username_value));
  password.set_password_value(UTF16ToUTF8(password_form.password_value));
  password.set_ssl_valid(password_form.ssl_valid);
  password.set_preferred(password_form.preferred);
  password.set_date_created(password_form.date_created.ToInternalValue());
  password.set_blacklisted(password_form.blacklisted_by_user);

  node->SetPasswordSpecifics(password);
}

// static
std::string PasswordModelAssociator::MakeTag(
                const content::PasswordForm& password) {
  return MakeTag(password.origin.spec(),
                 UTF16ToUTF8(password.username_element),
                 UTF16ToUTF8(password.username_value),
                 UTF16ToUTF8(password.password_element),
                 password.signon_realm);
}

// static
std::string PasswordModelAssociator::MakeTag(
                const sync_pb::PasswordSpecificsData& password) {
  return MakeTag(password.origin(),
                 password.username_element(),
                 password.username_value(),
                 password.password_element(),
                 password.signon_realm());
}

// static
std::string PasswordModelAssociator::MakeTag(
    const std::string& origin_url,
    const std::string& username_element,
    const std::string& username_value,
    const std::string& password_element,
    const std::string& signon_realm) {
  return net::EscapePath(origin_url) + "|" +
         net::EscapePath(username_element) + "|" +
         net::EscapePath(username_value) + "|" +
         net::EscapePath(password_element) + "|" +
         net::EscapePath(signon_realm);
}

}  // namespace browser_sync
