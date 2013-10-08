// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/bookmarks/bookmark_codec.h"

#include <algorithm>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

using base::Time;

const char* BookmarkCodec::kRootsKey = "roots";
const char* BookmarkCodec::kRootFolderNameKey = "bookmark_bar";
const char* BookmarkCodec::kOtherBookmarkFolderNameKey = "other";
// The value is left as 'synced' for historical reasons.
const char* BookmarkCodec::kMobileBookmarkFolderNameKey = "synced";
const char* BookmarkCodec::kVersionKey = "version";
const char* BookmarkCodec::kChecksumKey = "checksum";
const char* BookmarkCodec::kIdKey = "id";
const char* BookmarkCodec::kTypeKey = "type";
const char* BookmarkCodec::kNameKey = "name";
const char* BookmarkCodec::kDateAddedKey = "date_added";
const char* BookmarkCodec::kURLKey = "url";
const char* BookmarkCodec::kDateModifiedKey = "date_modified";
const char* BookmarkCodec::kChildrenKey = "children";
const char* BookmarkCodec::kMetaInfo = "meta_info";
const char* BookmarkCodec::kTypeURL = "url";
const char* BookmarkCodec::kTypeFolder = "folder";

// Current version of the file.
static const int kCurrentVersion = 1;

BookmarkCodec::BookmarkCodec()
    : ids_reassigned_(false),
      ids_valid_(true),
      maximum_id_(0) {
}

BookmarkCodec::~BookmarkCodec() {}

Value* BookmarkCodec::Encode(BookmarkModel* model) {
  return Encode(model->bookmark_bar_node(),
                model->other_node(),
                model->mobile_node(),
                model->root_node()->meta_info_str());
}

Value* BookmarkCodec::Encode(const BookmarkNode* bookmark_bar_node,
                             const BookmarkNode* other_folder_node,
                             const BookmarkNode* mobile_folder_node,
                             const std::string& model_meta_info) {
  ids_reassigned_ = false;
  InitializeChecksum();
  DictionaryValue* roots = new DictionaryValue();
  roots->Set(kRootFolderNameKey, EncodeNode(bookmark_bar_node));
  roots->Set(kOtherBookmarkFolderNameKey, EncodeNode(other_folder_node));
  roots->Set(kMobileBookmarkFolderNameKey, EncodeNode(mobile_folder_node));
  if (!model_meta_info.empty())
    roots->SetString(kMetaInfo, model_meta_info);
  DictionaryValue* main = new DictionaryValue();
  main->SetInteger(kVersionKey, kCurrentVersion);
  FinalizeChecksum();
  // We are going to store the computed checksum. So set stored checksum to be
  // the same as computed checksum.
  stored_checksum_ = computed_checksum_;
  main->Set(kChecksumKey, new base::StringValue(computed_checksum_));
  main->Set(kRootsKey, roots);
  return main;
}

bool BookmarkCodec::Decode(BookmarkNode* bb_node,
                           BookmarkNode* other_folder_node,
                           BookmarkNode* mobile_folder_node,
                           int64* max_id,
                           const Value& value) {
  ids_.clear();
  ids_reassigned_ = false;
  ids_valid_ = true;
  maximum_id_ = 0;
  stored_checksum_.clear();
  InitializeChecksum();
  bool success = DecodeHelper(bb_node, other_folder_node, mobile_folder_node,
                              value);
  FinalizeChecksum();
  // If either the checksums differ or some IDs were missing/not unique,
  // reassign IDs.
  if (!ids_valid_ || computed_checksum() != stored_checksum())
    ReassignIDs(bb_node, other_folder_node, mobile_folder_node);
  *max_id = maximum_id_ + 1;
  return success;
}

Value* BookmarkCodec::EncodeNode(const BookmarkNode* node) {
  DictionaryValue* value = new DictionaryValue();
  std::string id = base::Int64ToString(node->id());
  value->SetString(kIdKey, id);
  const string16& title = node->GetTitle();
  value->SetString(kNameKey, title);
  value->SetString(kDateAddedKey,
                   base::Int64ToString(node->date_added().ToInternalValue()));
  if (node->is_url()) {
    value->SetString(kTypeKey, kTypeURL);
    std::string url = node->url().possibly_invalid_spec();
    value->SetString(kURLKey, url);
    UpdateChecksumWithUrlNode(id, title, url);
  } else {
    value->SetString(kTypeKey, kTypeFolder);
    value->SetString(kDateModifiedKey,
                     base::Int64ToString(node->date_folder_modified().
                                   ToInternalValue()));
    UpdateChecksumWithFolderNode(id, title);

    ListValue* child_values = new ListValue();
    value->Set(kChildrenKey, child_values);
    for (int i = 0; i < node->child_count(); ++i)
      child_values->Append(EncodeNode(node->GetChild(i)));
  }
  if (!node->meta_info_str().empty())
    value->SetString(kMetaInfo, node->meta_info_str());
  return value;
}

bool BookmarkCodec::DecodeHelper(BookmarkNode* bb_node,
                                 BookmarkNode* other_folder_node,
                                 BookmarkNode* mobile_folder_node,
                                 const Value& value) {
  if (value.GetType() != Value::TYPE_DICTIONARY)
    return false;  // Unexpected type.

  const DictionaryValue& d_value = static_cast<const DictionaryValue&>(value);

  int version;
  if (!d_value.GetInteger(kVersionKey, &version) || version != kCurrentVersion)
    return false;  // Unknown version.

  const Value* checksum_value;
  if (d_value.Get(kChecksumKey, &checksum_value)) {
    if (checksum_value->GetType() != Value::TYPE_STRING)
      return false;
    if (!checksum_value->GetAsString(&stored_checksum_))
      return false;
  }

  const Value* roots;
  if (!d_value.Get(kRootsKey, &roots))
    return false;  // No roots.

  if (roots->GetType() != Value::TYPE_DICTIONARY)
    return false;  // Invalid type for roots.

  const DictionaryValue* roots_d_value =
      static_cast<const DictionaryValue*>(roots);
  const Value* root_folder_value;
  const Value* other_folder_value = NULL;
  if (!roots_d_value->Get(kRootFolderNameKey, &root_folder_value) ||
      root_folder_value->GetType() != Value::TYPE_DICTIONARY ||
      !roots_d_value->Get(kOtherBookmarkFolderNameKey, &other_folder_value) ||
      other_folder_value->GetType() != Value::TYPE_DICTIONARY) {
    return false;  // Invalid type for root folder and/or other
                   // folder.
  }
  DecodeNode(*static_cast<const DictionaryValue*>(root_folder_value), NULL,
             bb_node);
  DecodeNode(*static_cast<const DictionaryValue*>(other_folder_value), NULL,
             other_folder_node);

  // Fail silently if we can't deserialize mobile bookmarks. We can't require
  // them to exist in order to be backwards-compatible with older versions of
  // chrome.
  const Value* mobile_folder_value;
  if (roots_d_value->Get(kMobileBookmarkFolderNameKey, &mobile_folder_value) &&
      mobile_folder_value->GetType() == Value::TYPE_DICTIONARY) {
    DecodeNode(*static_cast<const DictionaryValue*>(mobile_folder_value), NULL,
               mobile_folder_node);
  } else {
    // If we didn't find the mobile folder, we're almost guaranteed to have a
    // duplicate id when we add the mobile folder. Consequently, if we don't
    // intend to reassign ids in the future (ids_valid_ is still true), then at
    // least reassign the mobile bookmarks to avoid it colliding with anything
    // else.
    if (ids_valid_)
      ReassignIDsHelper(mobile_folder_node);
  }

  roots_d_value->GetString(kMetaInfo, &model_meta_info_);

  // Need to reset the type as decoding resets the type to FOLDER. Similarly
  // we need to reset the title as the title is persisted and restored from
  // the file.
  bb_node->set_type(BookmarkNode::BOOKMARK_BAR);
  other_folder_node->set_type(BookmarkNode::OTHER_NODE);
  mobile_folder_node->set_type(BookmarkNode::MOBILE);
  bb_node->SetTitle(l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_FOLDER_NAME));
  other_folder_node->SetTitle(
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_OTHER_FOLDER_NAME));
  mobile_folder_node->SetTitle(
        l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_MOBILE_FOLDER_NAME));

  return true;
}

bool BookmarkCodec::DecodeChildren(const ListValue& child_value_list,
                                   BookmarkNode* parent) {
  for (size_t i = 0; i < child_value_list.GetSize(); ++i) {
    const Value* child_value;
    if (!child_value_list.Get(i, &child_value))
      return false;

    if (child_value->GetType() != Value::TYPE_DICTIONARY)
      return false;

    DecodeNode(*static_cast<const DictionaryValue*>(child_value), parent, NULL);
  }
  return true;
}

bool BookmarkCodec::DecodeNode(const DictionaryValue& value,
                               BookmarkNode* parent,
                               BookmarkNode* node) {
  // If no |node| is specified, we'll create one and add it to the |parent|.
  // Therefore, in that case, |parent| must be non-NULL.
  if (!node && !parent) {
    NOTREACHED();
    return false;
  }

  std::string id_string;
  int64 id = 0;
  if (ids_valid_) {
    if (!value.GetString(kIdKey, &id_string) ||
        !base::StringToInt64(id_string, &id) ||
        ids_.count(id) != 0) {
      ids_valid_ = false;
    } else {
      ids_.insert(id);
    }
  }

  maximum_id_ = std::max(maximum_id_, id);

  string16 title;
  value.GetString(kNameKey, &title);

  std::string date_added_string;
  if (!value.GetString(kDateAddedKey, &date_added_string))
    date_added_string = base::Int64ToString(Time::Now().ToInternalValue());
  int64 internal_time;
  base::StringToInt64(date_added_string, &internal_time);

  std::string type_string;
  if (!value.GetString(kTypeKey, &type_string))
    return false;

  if (type_string != kTypeURL && type_string != kTypeFolder)
    return false;  // Unknown type.

  if (type_string == kTypeURL) {
    std::string url_string;
    if (!value.GetString(kURLKey, &url_string))
      return false;

    GURL url = GURL(url_string);
    if (!node && url.is_valid())
      node = new BookmarkNode(id, url);
    else
      return false;  // Node invalid.

    if (parent)
      parent->Add(node, parent->child_count());
    node->set_type(BookmarkNode::URL);
    UpdateChecksumWithUrlNode(id_string, title, url_string);
  } else {
    std::string last_modified_date;
    if (!value.GetString(kDateModifiedKey, &last_modified_date))
      last_modified_date = base::Int64ToString(Time::Now().ToInternalValue());

    const Value* child_values;
    if (!value.Get(kChildrenKey, &child_values))
      return false;

    if (child_values->GetType() != Value::TYPE_LIST)
      return false;

    if (!node) {
      node = new BookmarkNode(id, GURL());
    } else {
      // If a new node is not created, explicitly assign ID to the existing one.
      node->set_id(id);
    }

    node->set_type(BookmarkNode::FOLDER);
    int64 internal_time;
    base::StringToInt64(last_modified_date, &internal_time);
    node->set_date_folder_modified(Time::FromInternalValue(internal_time));

    if (parent)
      parent->Add(node, parent->child_count());

    UpdateChecksumWithFolderNode(id_string, title);

    if (!DecodeChildren(*static_cast<const ListValue*>(child_values), node))
      return false;
  }

  node->SetTitle(title);
  node->set_date_added(base::Time::FromInternalValue(internal_time));

  std::string meta_info;
  if (value.GetString(kMetaInfo, &meta_info))
    node->set_meta_info_str(meta_info);

  return true;
}

void BookmarkCodec::ReassignIDs(BookmarkNode* bb_node,
                                BookmarkNode* other_node,
                                BookmarkNode* mobile_node) {
  maximum_id_ = 0;
  ReassignIDsHelper(bb_node);
  ReassignIDsHelper(other_node);
  ReassignIDsHelper(mobile_node);
  ids_reassigned_ = true;
}

void BookmarkCodec::ReassignIDsHelper(BookmarkNode* node) {
  DCHECK(node);
  node->set_id(++maximum_id_);
  for (int i = 0; i < node->child_count(); ++i)
    ReassignIDsHelper(node->GetChild(i));
}

void BookmarkCodec::UpdateChecksum(const std::string& str) {
  base::MD5Update(&md5_context_, str);
}

void BookmarkCodec::UpdateChecksum(const string16& str) {
  base::MD5Update(&md5_context_,
                  base::StringPiece(
                      reinterpret_cast<const char*>(str.data()),
                      str.length() * sizeof(str[0])));
}

void BookmarkCodec::UpdateChecksumWithUrlNode(const std::string& id,
                                              const string16& title,
                                              const std::string& url) {
  DCHECK(IsStringUTF8(url));
  UpdateChecksum(id);
  UpdateChecksum(title);
  UpdateChecksum(kTypeURL);
  UpdateChecksum(url);
}

void BookmarkCodec::UpdateChecksumWithFolderNode(const std::string& id,
                                                 const string16& title) {
  UpdateChecksum(id);
  UpdateChecksum(title);
  UpdateChecksum(kTypeFolder);
}

void BookmarkCodec::InitializeChecksum() {
  base::MD5Init(&md5_context_);
}

void BookmarkCodec::FinalizeChecksum() {
  base::MD5Digest digest;
  base::MD5Final(&digest, &md5_context_);
  computed_checksum_ = base::MD5DigestToBase16(digest);
}
