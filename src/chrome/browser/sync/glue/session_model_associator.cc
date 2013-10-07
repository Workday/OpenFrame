// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/session_model_associator.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/safe_numerics.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service.h"
#include "chrome/browser/prefs/pref_service_syncable.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_id.h"
#include "chrome/browser/sync/glue/device_info.h"
#include "chrome/browser/sync/glue/synced_device_tracker.h"
#include "chrome/browser/sync/glue/synced_session.h"
#include "chrome/browser/sync/glue/synced_tab_delegate.h"
#include "chrome/browser/sync/glue/synced_window_delegate.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "components/sessions/serialized_navigation_entry.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "sync/api/sync_error.h"
#include "sync/api/time.h"
#include "sync/internal_api/public/base/model_type.h"
#include "sync/internal_api/public/base/model_type_invalidation_map.h"
#include "sync/internal_api/public/read_node.h"
#include "sync/internal_api/public/read_transaction.h"
#include "sync/internal_api/public/write_node.h"
#include "sync/internal_api/public/write_transaction.h"
#include "sync/protocol/session_specifics.pb.h"
#include "sync/syncable/directory.h"
#include "sync/syncable/syncable_read_transaction.h"
#include "sync/syncable/syncable_write_transaction.h"
#if defined(OS_LINUX)
#include "base/linux_util.h"
#elif defined(OS_WIN)
#include <windows.h>
#endif

using content::BrowserThread;
using content::NavigationEntry;
using prefs::kSyncSessionsGUID;
using sessions::SerializedNavigationEntry;
using syncer::SESSIONS;

namespace {

std::string SessionTagPrefix() {
  return std::string("session_sync");
}

// Given a transaction, returns the GUID-based string that should be used for
// |current_machine_tag_|.
std::string GetMachineTagFromTransaction(
    syncer::WriteTransaction* trans) {
  syncer::syncable::Directory* dir = trans->GetWrappedWriteTrans()->directory();
  std::string machine_tag = SessionTagPrefix();
  machine_tag.append(dir->cache_guid());
  return machine_tag;
}

// Given a session tag this function returns the client_id(cache_guid).
std::string GetClientIdFromSessionTag(const std::string& session_tag) {
  if (session_tag.find_first_of(SessionTagPrefix()) == std::string::npos) {
    LOG(ERROR) << "Session tag is malformatted";
    return std::string();
  }

  std::string client_id = session_tag.substr(
      SessionTagPrefix().length(),
      session_tag.length());

  return client_id;
}

}  // namespace

namespace browser_sync {

namespace {
static const char kNoSessionsFolderError[] =
    "Server did not create the top-level sessions node. We "
    "might be running against an out-of-date server.";

// The maximum number of navigations in each direction we care to sync.
static const int kMaxSyncNavigationCount = 6;

// Default number of days without activity after which a session is considered
// stale and becomes a candidate for garbage collection.
static const size_t kDefaultStaleSessionThresholdDays = 14;  // 2 weeks.

// Maximum number of favicons to sync.
// TODO(zea): pull this from the server.
static const int kMaxSyncFavicons = 200;

}  // namespace

SessionModelAssociator::SessionModelAssociator(
    ProfileSyncService* sync_service,
    DataTypeErrorHandler* error_handler)
    : local_tab_pool_(sync_service),
      local_session_syncid_(syncer::kInvalidId),
      sync_service_(sync_service),
      stale_session_threshold_days_(kDefaultStaleSessionThresholdDays),
      setup_for_test_(false),
      waiting_for_change_(false),
      test_weak_factory_(this),
      profile_(sync_service->profile()),
      error_handler_(error_handler),
      favicon_cache_(profile_,
                     sync_service->current_experiments().favicon_sync_limit) {
  DCHECK(CalledOnValidThread());
  DCHECK(sync_service_);
  DCHECK(profile_);
}

SessionModelAssociator::SessionModelAssociator(ProfileSyncService* sync_service,
                                               bool setup_for_test)
    : local_tab_pool_(sync_service),
      local_session_syncid_(syncer::kInvalidId),
      sync_service_(sync_service),
      stale_session_threshold_days_(kDefaultStaleSessionThresholdDays),
      setup_for_test_(setup_for_test),
      waiting_for_change_(false),
      test_weak_factory_(this),
      profile_(sync_service->profile()),
      error_handler_(NULL),
      favicon_cache_(profile_, kMaxSyncFavicons) {
  DCHECK(CalledOnValidThread());
  DCHECK(sync_service_);
  DCHECK(profile_);
  DCHECK(setup_for_test);
}

SessionModelAssociator::~SessionModelAssociator() {
  DCHECK(CalledOnValidThread());
}

bool SessionModelAssociator::SyncModelHasUserCreatedNodes(bool* has_nodes) {
  DCHECK(CalledOnValidThread());
  CHECK(has_nodes);
  *has_nodes = false;
  syncer::ReadTransaction trans(FROM_HERE, sync_service_->GetUserShare());
  syncer::ReadNode root(&trans);
  if (root.InitByTagLookup(syncer::ModelTypeToRootTag(syncer::SESSIONS)) !=
                           syncer::BaseNode::INIT_OK) {
    LOG(ERROR) << kNoSessionsFolderError;
    return false;
  }
  // The sync model has user created nodes iff the sessions folder has
  // any children.
  *has_nodes = root.HasChildren();
  return true;
}

int64 SessionModelAssociator::GetSyncIdFromSessionTag(const std::string& tag) {
  DCHECK(CalledOnValidThread());
  syncer::ReadTransaction trans(FROM_HERE, sync_service_->GetUserShare());
  syncer::ReadNode node(&trans);
  if (node.InitByClientTagLookup(SESSIONS, tag) != syncer::BaseNode::INIT_OK)
    return syncer::kInvalidId;
  return node.GetId();
}

bool SessionModelAssociator::AssociateWindows(bool reload_tabs,
                                              syncer::SyncError* error) {
  DCHECK(CalledOnValidThread());
  std::string local_tag = GetCurrentMachineTag();
  sync_pb::SessionSpecifics specifics;
  specifics.set_session_tag(local_tag);
  sync_pb::SessionHeader* header_s = specifics.mutable_header();
  SyncedSession* current_session =
      synced_session_tracker_.GetSession(local_tag);
  current_session->modified_time = base::Time::Now();
  header_s->set_client_name(current_session_name_);
  header_s->set_device_type(DeviceInfo::GetLocalDeviceType());

  synced_session_tracker_.ResetSessionTracking(local_tag);
  std::set<SyncedWindowDelegate*> windows =
      SyncedWindowDelegate::GetSyncedWindowDelegates();
  for (std::set<SyncedWindowDelegate*>::const_iterator i =
           windows.begin(); i != windows.end(); ++i) {
    // Make sure the window has tabs and a viewable window. The viewable window
    // check is necessary because, for example, when a browser is closed the
    // destructor is not necessarily run immediately. This means its possible
    // for us to get a handle to a browser that is about to be removed. If
    // the tab count is 0 or the window is NULL, the browser is about to be
    // deleted, so we ignore it.
    if (ShouldSyncWindow(*i) && (*i)->GetTabCount() && (*i)->HasWindow()) {
      sync_pb::SessionWindow window_s;
      SessionID::id_type window_id = (*i)->GetSessionId();
      DVLOG(1) << "Associating window " << window_id << " with "
               << (*i)->GetTabCount() << " tabs.";
      window_s.set_window_id(window_id);
      // Note: We don't bother to set selected tab index anymore. We still
      // consume it when receiving foreign sessions, as reading it is free, but
      // it triggers too many sync cycles with too little value to make setting
      // it worthwhile.
      if ((*i)->IsTypeTabbed()) {
        window_s.set_browser_type(
            sync_pb::SessionWindow_BrowserType_TYPE_TABBED);
      } else {
        window_s.set_browser_type(
            sync_pb::SessionWindow_BrowserType_TYPE_POPUP);
      }

      // Store the order of tabs.
      bool found_tabs = false;
      for (int j = 0; j < (*i)->GetTabCount(); ++j) {
        SessionID::id_type tab_id = (*i)->GetTabIdAt(j);
        SyncedTabDelegate* synced_tab = (*i)->GetTabAt(j);

        // GetTabAt can return a null tab; in that case just skip it.
        if (!synced_tab)
          continue;

        if (!synced_tab->HasWebContents()) {
          // For tabs without WebContents update the |tab_id|, as it could have
          // changed after a session restore.
          // Note: We cannot check if a tab is valid if it has no WebContents.
          // We assume any such tab is valid and leave the contents of
          // corresponding sync node unchanged.
          if (synced_tab->GetSyncId() > TabNodePool::kInvalidTabNodeID &&
              tab_id > TabNodePool::kInvalidTabID) {
            UpdateTabIdIfNecessary(synced_tab->GetSyncId(), tab_id);
            found_tabs = true;
            window_s.add_tab(tab_id);
          }
          continue;
        }

        if (reload_tabs) {
          // It's possible for GetTabAt to return a tab which has no web
          // contents. We can assume this means the tab already existed but
          // hasn't changed, so no need to reassociate.
          if (synced_tab->HasWebContents() &&
              !AssociateTab(synced_tab, error)) {
            // Association failed. Either we need to re-associate, or this is an
            // unrecoverable error.
            return false;
          }
        }

        // If the tab is valid, it would have been added to the tracker either
        // by the above AssociateTab call (at association time), or by the
        // change processor calling AssociateTab for all modified tabs.
        // Therefore, we can key whether this window has valid tabs based on
        // the tab's presence in the tracker.
        const SessionTab* tab = NULL;
        if (synced_session_tracker_.LookupSessionTab(local_tag, tab_id, &tab)) {
          found_tabs = true;
          window_s.add_tab(tab_id);
        }
      }
      // Only add a window if it contains valid tabs.
      if (found_tabs) {
        sync_pb::SessionWindow* header_window = header_s->add_window();
        *header_window = window_s;

        // Update this window's representation in the synced session tracker.
        synced_session_tracker_.PutWindowInSession(local_tag, window_id);
        PopulateSessionWindowFromSpecifics(
            local_tag,
            window_s,
            base::Time::Now(),
            current_session->windows[window_id],
            &synced_session_tracker_);
      }
    }
  }

  local_tab_pool_.DeleteUnassociatedTabNodes();
  // Free memory for closed windows and tabs.
  synced_session_tracker_.CleanupSession(local_tag);

  syncer::WriteTransaction trans(FROM_HERE, sync_service_->GetUserShare());
  syncer::WriteNode header_node(&trans);
  if (header_node.InitByIdLookup(local_session_syncid_) !=
          syncer::BaseNode::INIT_OK) {
    if (error) {
      *error = error_handler_->CreateAndUploadError(
           FROM_HERE,
           "Failed to load local session header node.",
           model_type());
    }
    return false;
  }
  header_node.SetSessionSpecifics(specifics);
  if (waiting_for_change_) QuitLoopForSubtleTesting();
  return true;
}

// Static.
bool SessionModelAssociator::ShouldSyncWindow(
    const SyncedWindowDelegate* window) {
  if (window->IsApp())
    return false;
  return window->IsTypeTabbed() || window->IsTypePopup();
}

bool SessionModelAssociator::AssociateTabs(
    const std::vector<SyncedTabDelegate*>& tabs,
    syncer::SyncError* error) {
  DCHECK(CalledOnValidThread());
  for (std::vector<SyncedTabDelegate*>::const_iterator i = tabs.begin();
       i != tabs.end();
       ++i) {
    if (!AssociateTab(*i, error))
      return false;
  }
  if (waiting_for_change_) QuitLoopForSubtleTesting();
  return true;
}

bool SessionModelAssociator::AssociateTab(SyncedTabDelegate* const tab,
                                          syncer::SyncError* error) {
  DCHECK(CalledOnValidThread());
  DCHECK(tab->HasWebContents());
  int tab_node_id(TabNodePool::kInvalidTabNodeID);
  SessionID::id_type tab_id = tab->GetSessionId();
  if (tab->IsBeingDestroyed()) {
    // This tab is closing.
    TabLinksMap::iterator tab_iter = local_tab_map_.find(tab_id);
    if (tab_iter == local_tab_map_.end()) {
      // We aren't tracking this tab (for example, sync setting page).
      return true;
    }
    local_tab_pool_.FreeTabNode(tab_iter->second->tab_node_id());
    local_tab_map_.erase(tab_iter);
    return true;
  }

  if (!ShouldSyncTab(*tab))
    return true;

  TabLinksMap::iterator local_tab_map_iter = local_tab_map_.find(tab_id);
  TabLink* tab_link = NULL;
  if (local_tab_map_iter == local_tab_map_.end()) {
    tab_node_id = tab->GetSyncId();
    // if there is an old sync node for the tab, reuse it.
    if (!local_tab_pool_.IsUnassociatedTabNode(tab_node_id)) {
      // This is a new tab, get a sync node for it.
      tab_node_id = local_tab_pool_.GetFreeTabNode();
      if (tab_node_id == TabNodePool::kInvalidTabNodeID) {
        if (error) {
          *error = error_handler_->CreateAndUploadError(
              FROM_HERE,
              "Received invalid tab node from tab pool.",
              model_type());
        }
        return false;
      }
      tab->SetSyncId(tab_node_id);
    }
    local_tab_pool_.AssociateTabNode(tab_node_id, tab_id);
    tab_link = new TabLink(tab_node_id, tab);
    local_tab_map_[tab_id] = make_linked_ptr<TabLink>(tab_link);
  } else {
    // This tab is already associated with a sync node, reuse it.
    // Note: on some platforms the tab object may have changed, so we ensure
    // the tab link is up to date.
    tab_link = local_tab_map_iter->second.get();
    local_tab_map_iter->second->set_tab(tab);
  }
  DCHECK(tab_link);
  DCHECK_NE(tab_link->tab_node_id(), TabNodePool::kInvalidTabNodeID);

  DVLOG(1) << "Reloading tab " << tab_id << " from window "
           << tab->GetWindowId();
  return WriteTabContentsToSyncModel(tab_link, error);
}

// static
GURL SessionModelAssociator::GetCurrentVirtualURL(
    const SyncedTabDelegate& tab_delegate) {
  const int current_index = tab_delegate.GetCurrentEntryIndex();
  const int pending_index = tab_delegate.GetPendingEntryIndex();
  const NavigationEntry* current_entry =
      (current_index == pending_index) ?
      tab_delegate.GetPendingEntry() :
      tab_delegate.GetEntryAtIndex(current_index);
  return current_entry->GetVirtualURL();
}

// static
GURL SessionModelAssociator::GetCurrentFaviconURL(
    const SyncedTabDelegate& tab_delegate) {
  const int current_index = tab_delegate.GetCurrentEntryIndex();
  const int pending_index = tab_delegate.GetPendingEntryIndex();
  const NavigationEntry* current_entry =
      (current_index == pending_index) ?
      tab_delegate.GetPendingEntry() :
      tab_delegate.GetEntryAtIndex(current_index);
  return (current_entry->GetFavicon().valid ?
          current_entry->GetFavicon().url :
          GURL());
}

bool SessionModelAssociator::WriteTabContentsToSyncModel(
    TabLink* tab_link,
    syncer::SyncError* error) {
  DCHECK(CalledOnValidThread());
  const SyncedTabDelegate& tab_delegate = *(tab_link->tab());
  int tab_node_id = tab_link->tab_node_id();
  GURL old_tab_url = tab_link->url();
  const GURL new_url = GetCurrentVirtualURL(tab_delegate);
  DVLOG(1) << "Local tab " << tab_delegate.GetSessionId()
           << " now has URL " << new_url.spec();

  SessionTab* session_tab = NULL;
  {
    syncer::WriteTransaction trans(FROM_HERE, sync_service_->GetUserShare());
    syncer::WriteNode tab_node(&trans);
    if (tab_node.InitByClientTagLookup(
            syncer::SESSIONS,
            TabNodePool::TabIdToTag(current_machine_tag_, tab_node_id)) !=
        syncer::BaseNode::INIT_OK) {
      if (error) {
        *error = error_handler_->CreateAndUploadError(
            FROM_HERE,
            "Failed to look up local tab node",
            model_type());
      }
      return false;
    }

    // Load the last stored version of this tab so we can compare changes. If
    // this is a new tab, session_tab will be a new, blank SessionTab object.
    sync_pb::SessionSpecifics specifics = tab_node.GetSessionSpecifics();
    const int s_tab_node_id(specifics.tab_node_id());
    DCHECK_EQ(tab_node_id, s_tab_node_id);
    session_tab =
        synced_session_tracker_.GetTab(GetCurrentMachineTag(),
                                       tab_delegate.GetSessionId(),
                                       specifics.tab_node_id());
    SetSessionTabFromDelegate(tab_delegate, base::Time::Now(), session_tab);
    sync_pb::SessionTab tab_s = session_tab->ToSyncData();

    if (new_url == old_tab_url) {
      // Load the old specifics and copy over the favicon data if needed.
      // TODO(zea): remove this once favicon sync is enabled as a separate type.
      tab_s.set_favicon(specifics.tab().favicon());
      tab_s.set_favicon_source(specifics.tab().favicon_source());
      tab_s.set_favicon_type(specifics.tab().favicon_type());
    }
    // Retain the base SessionSpecifics data (tag, tab_node_id, etc.), and just
    // write the new SessionTabSpecifics.
    specifics.mutable_tab()->CopyFrom(tab_s);

    // Write into the actual sync model.
    tab_node.SetSessionSpecifics(specifics);
  }

  // Trigger the favicon load if needed. We do this outside the write
  // transaction to avoid jank.
  tab_link->set_url(new_url);
  if (new_url != old_tab_url) {
    favicon_cache_.OnFaviconVisited(new_url,
                                    GetCurrentFaviconURL(tab_delegate));
  }

  // Update our last modified time.
  synced_session_tracker_.GetSession(GetCurrentMachineTag())->modified_time =
      base::Time::Now();

  return true;
}

// static
void SessionModelAssociator::SetSessionTabFromDelegate(
    const SyncedTabDelegate& tab_delegate,
    base::Time mtime,
    SessionTab* session_tab) {
  DCHECK(session_tab);
  session_tab->window_id.set_id(tab_delegate.GetWindowId());
  session_tab->tab_id.set_id(tab_delegate.GetSessionId());
  session_tab->tab_visual_index = 0;
  session_tab->current_navigation_index = tab_delegate.GetCurrentEntryIndex();
  session_tab->pinned = tab_delegate.IsPinned();
  session_tab->extension_app_id = tab_delegate.GetExtensionAppId();
  session_tab->user_agent_override.clear();
  session_tab->timestamp = mtime;
  const int current_index = tab_delegate.GetCurrentEntryIndex();
  const int pending_index = tab_delegate.GetPendingEntryIndex();
  const int min_index = std::max(0, current_index - kMaxSyncNavigationCount);
  const int max_index = std::min(current_index + kMaxSyncNavigationCount,
                                 tab_delegate.GetEntryCount());
  bool is_managed = tab_delegate.ProfileIsManaged();
  session_tab->navigations.clear();
  for (int i = min_index; i < max_index; ++i) {
    const NavigationEntry* entry = (i == pending_index) ?
        tab_delegate.GetPendingEntry() : tab_delegate.GetEntryAtIndex(i);
    DCHECK(entry);
    if (!entry->GetVirtualURL().is_valid())
      continue;

    session_tab->navigations.push_back(
        SerializedNavigationEntry::FromNavigationEntry(i, *entry));
    if (is_managed) {
      session_tab->navigations.back().set_blocked_state(
          SerializedNavigationEntry::STATE_ALLOWED);
    }
  }

  if (is_managed) {
    const std::vector<const NavigationEntry*>& blocked_navigations =
        *tab_delegate.GetBlockedNavigations();
    int offset = session_tab->navigations.size();
    for (size_t i = 0; i < blocked_navigations.size(); ++i) {
      session_tab->navigations.push_back(
          SerializedNavigationEntry::FromNavigationEntry(
              i + offset, *blocked_navigations[i]));
      session_tab->navigations.back().set_blocked_state(
          SerializedNavigationEntry::STATE_BLOCKED);
      // TODO(bauerb): Add categories
    }
  }
  session_tab->session_storage_persistent_id.clear();
}

void SessionModelAssociator::FaviconsUpdated(
    const std::set<GURL>& urls) {
  // TODO(zea): consider a separate container for tabs with outstanding favicon
  // loads so we don't have to iterate through all tabs comparing urls.
  for (std::set<GURL>::const_iterator i = urls.begin(); i != urls.end(); ++i) {
    for (TabLinksMap::iterator tab_iter = local_tab_map_.begin();
         tab_iter != local_tab_map_.end();
         ++tab_iter) {
      if (tab_iter->second->url() == *i)
        favicon_cache_.OnPageFaviconUpdated(*i);
    }
  }
}

syncer::SyncError SessionModelAssociator::AssociateModels(
    syncer::SyncMergeResult* local_merge_result,
    syncer::SyncMergeResult* syncer_merge_result) {
  DCHECK(CalledOnValidThread());
  syncer::SyncError error;

  // Ensure that we disassociated properly, otherwise memory might leak.
  DCHECK(synced_session_tracker_.Empty());
  DCHECK_EQ(0U, local_tab_pool_.Capacity());

  local_session_syncid_ = syncer::kInvalidId;

  scoped_ptr<DeviceInfo> local_device_info(sync_service_->GetLocalDeviceInfo());

#if defined(OS_ANDROID)
  std::string transaction_tag;
#endif
  // Read any available foreign sessions and load any session data we may have.
  // If we don't have any local session data in the db, create a header node.
  {
    syncer::WriteTransaction trans(FROM_HERE, sync_service_->GetUserShare());

    syncer::ReadNode root(&trans);
    if (root.InitByTagLookup(syncer::ModelTypeToRootTag(model_type())) !=
            syncer::BaseNode::INIT_OK) {
      return error_handler_->CreateAndUploadError(
          FROM_HERE,
          kNoSessionsFolderError,
          model_type());
    }

    // Make sure we have a machine tag.
    if (current_machine_tag_.empty())
      InitializeCurrentMachineTag(&trans);
    if (local_device_info) {
      current_session_name_ = local_device_info->client_name();
    } else {
      return error_handler_->CreateAndUploadError(
          FROM_HERE,
          "Failed to get device info.",
          model_type());
    }
    synced_session_tracker_.SetLocalSessionTag(current_machine_tag_);
    if (!UpdateAssociationsFromSyncModel(root, &trans, &error)) {
      DCHECK(error.IsSet());
      return error;
    }

    if (local_session_syncid_ == syncer::kInvalidId) {
      // The sync db didn't have a header node for us, we need to create one.
      syncer::WriteNode write_node(&trans);
      syncer::WriteNode::InitUniqueByCreationResult result =
          write_node.InitUniqueByCreation(SESSIONS, root, current_machine_tag_);
      if (result != syncer::WriteNode::INIT_SUCCESS) {
        // If we can't look it up, and we can't create it, chances are there's
        // a pre-existing node that has encryption issues. But, since we can't
        // load the item, we can't remove it, and error out at this point.
        return error_handler_->CreateAndUploadError(
            FROM_HERE,
            "Failed to create sessions header sync node.",
            model_type());
      }

      // Write the initial values to the specifics so that in case of a crash or
      // error we don't persist a half-written node.
      write_node.SetTitle(UTF8ToWide(current_machine_tag_));
      sync_pb::SessionSpecifics base_specifics;
      base_specifics.set_session_tag(current_machine_tag_);
      sync_pb::SessionHeader* header_s = base_specifics.mutable_header();
      header_s->set_client_name(current_session_name_);
      header_s->set_device_type(DeviceInfo::GetLocalDeviceType());
      write_node.SetSessionSpecifics(base_specifics);

      local_session_syncid_ = write_node.GetId();
    }
#if defined(OS_ANDROID)
    transaction_tag = GetMachineTagFromTransaction(&trans);
#endif
  }
#if defined(OS_ANDROID)
  // We need to delete foreign sessions after giving up our
  // syncer::WriteTransaction, since DeleteForeignSession(std::string&) uses
  // its own syncer::WriteTransaction.
  if (current_machine_tag_.compare(transaction_tag) != 0)
    DeleteForeignSession(transaction_tag);
#endif

  // Check if anything has changed on the client side.
  if (!UpdateSyncModelDataFromClient(&error)) {
    DCHECK(error.IsSet());
    return error;
  }

  DVLOG(1) << "Session models associated.";
  DCHECK(!error.IsSet());
  return error;
}

syncer::SyncError SessionModelAssociator::DisassociateModels() {
  DCHECK(CalledOnValidThread());
  DVLOG(1) << "Disassociating local session " << GetCurrentMachineTag();
  synced_session_tracker_.Clear();
  local_tab_map_.clear();
  local_tab_pool_.Clear();
  local_session_syncid_ = syncer::kInvalidId;
  current_machine_tag_ = "";
  current_session_name_ = "";

  // There is no local model stored with which to disassociate, just notify
  // foreign session handlers.
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_FOREIGN_SESSION_DISABLED,
      content::Source<Profile>(sync_service_->profile()),
      content::NotificationService::NoDetails());
  return syncer::SyncError();
}

void SessionModelAssociator::InitializeCurrentMachineTag(
    syncer::WriteTransaction* trans) {
  DCHECK(CalledOnValidThread());
  DCHECK(current_machine_tag_.empty());
  std::string persisted_guid;
  browser_sync::SyncPrefs prefs(profile_->GetPrefs());
  persisted_guid = prefs.GetSyncSessionsGUID();
  if (!persisted_guid.empty()) {
    current_machine_tag_ = persisted_guid;
    DVLOG(1) << "Restoring persisted session sync guid: "
             << persisted_guid;
  } else {
    current_machine_tag_ = GetMachineTagFromTransaction(trans);
    DVLOG(1) << "Creating session sync guid: " << current_machine_tag_;
    prefs.SetSyncSessionsGUID(current_machine_tag_);
  }

  local_tab_pool_.SetMachineTag(current_machine_tag_);
}

bool SessionModelAssociator::GetSyncedFaviconForPageURL(
    const std::string& page_url,
    scoped_refptr<base::RefCountedMemory>* favicon_png) const {
  return favicon_cache_.GetSyncedFaviconForPageURL(GURL(page_url), favicon_png);
}

scoped_ptr<browser_sync::DeviceInfo>
    SessionModelAssociator::GetDeviceInfoForSessionTag(
        const std::string& session_tag) {
  std::string client_id = GetClientIdFromSessionTag(session_tag);
  return sync_service_->GetDeviceInfo(client_id);
}

bool SessionModelAssociator::UpdateAssociationsFromSyncModel(
    const syncer::ReadNode& root,
    syncer::WriteTransaction* trans,
    syncer::SyncError* error) {
  DCHECK(CalledOnValidThread());
  DCHECK(local_tab_pool_.Empty());
  DCHECK_EQ(local_session_syncid_, syncer::kInvalidId);

  // Iterate through the nodes and associate any foreign sessions.
  int64 id = root.GetFirstChildId();
  while (id != syncer::kInvalidId) {
    syncer::WriteNode sync_node(trans);
    if (sync_node.InitByIdLookup(id) != syncer::BaseNode::INIT_OK) {
      if (error) {
        *error = error_handler_->CreateAndUploadError(
            FROM_HERE,
            "Failed to load sync node",
            model_type());
      }
      return false;
    }
    int64 next_id = sync_node.GetSuccessorId();

    const sync_pb::SessionSpecifics& specifics =
        sync_node.GetSessionSpecifics();
    const base::Time& modification_time = sync_node.GetModificationTime();
    if (specifics.session_tag().empty() ||
           (specifics.has_tab() && (!specifics.has_tab_node_id() ||
            !specifics.tab().has_tab_id()))) {
      // This is a corrupted node. Just delete it.
      LOG(WARNING) << "Found invalid session node, deleting.";
      sync_node.Tombstone();
    } else if (specifics.session_tag() != GetCurrentMachineTag()) {
      AssociateForeignSpecifics(specifics, modification_time);
    } else {
      // This is previously stored local session information.
      if (specifics.has_header() &&
          local_session_syncid_ == syncer::kInvalidId) {
        // This is our previous header node, reuse it.
        local_session_syncid_ = id;
        if (specifics.header().has_client_name()) {
          current_session_name_ = specifics.header().client_name();
        }
      } else {
        if (specifics.has_header() || !specifics.has_tab()) {
          LOG(WARNING) << "Found invalid session node, deleting.";
          sync_node.Tombstone();
        } else {
          // This is a valid old tab node, add it to the pool so it can be
          // reused for reassociation.
          local_tab_pool_.AddTabNode(specifics.tab_node_id());
        }
      }
    }
    id = next_id;
  }

  return true;
}

void SessionModelAssociator::AssociateForeignSpecifics(
    const sync_pb::SessionSpecifics& specifics,
    const base::Time& modification_time) {
  DCHECK(CalledOnValidThread());
  std::string foreign_session_tag = specifics.session_tag();
  if (foreign_session_tag == GetCurrentMachineTag() && !setup_for_test_)
    return;

  SyncedSession* foreign_session =
      synced_session_tracker_.GetSession(foreign_session_tag);
  if (specifics.has_header()) {
    // Read in the header data for this foreign session.
    // Header data contains window information and ordered tab id's for each
    // window.

    // Load (or create) the SyncedSession object for this client.
    const sync_pb::SessionHeader& header = specifics.header();
    PopulateSessionHeaderFromSpecifics(header,
                                       modification_time,
                                       foreign_session);

    // Reset the tab/window tracking for this session (must do this before
    // we start calling PutWindowInSession and PutTabInWindow so that all
    // unused tabs/windows get cleared by the CleanupSession(...) call).
    synced_session_tracker_.ResetSessionTracking(foreign_session_tag);

    // Process all the windows and their tab information.
    int num_windows = header.window_size();
    DVLOG(1) << "Associating " << foreign_session_tag << " with "
             << num_windows << " windows.";
    for (int i = 0; i < num_windows; ++i) {
      const sync_pb::SessionWindow& window_s = header.window(i);
      SessionID::id_type window_id = window_s.window_id();
      synced_session_tracker_.PutWindowInSession(foreign_session_tag,
                                                 window_id);
      PopulateSessionWindowFromSpecifics(foreign_session_tag,
                                         window_s,
                                         modification_time,
                                         foreign_session->windows[window_id],
                                         &synced_session_tracker_);
    }

    // Delete any closed windows and unused tabs as necessary.
    synced_session_tracker_.CleanupSession(foreign_session_tag);
  } else if (specifics.has_tab()) {
    const sync_pb::SessionTab& tab_s = specifics.tab();
    SessionID::id_type tab_id = tab_s.tab_id();
    SessionTab* tab =
        synced_session_tracker_.GetTab(foreign_session_tag,
                                       tab_id,
                                       specifics.tab_node_id());

    // Update SessionTab based on protobuf.
    tab->SetFromSyncData(tab_s, modification_time);

    // If a favicon or favicon urls are present, load them into the in-memory
    // favicon cache.
    LoadForeignTabFavicon(tab_s);

    // Update the last modified time.
    if (foreign_session->modified_time < modification_time)
      foreign_session->modified_time = modification_time;
  } else {
    LOG(WARNING) << "Ignoring foreign session node with missing header/tab "
                 << "fields and tag " << foreign_session_tag << ".";
  }
}

bool SessionModelAssociator::DisassociateForeignSession(
    const std::string& foreign_session_tag) {
  DCHECK(CalledOnValidThread());
  if (foreign_session_tag == GetCurrentMachineTag()) {
    DVLOG(1) << "Local session deleted! Doing nothing until a navigation is "
             << "triggered.";
    return false;
  }
  DVLOG(1) << "Disassociating session " << foreign_session_tag;
  return synced_session_tracker_.DeleteSession(foreign_session_tag);
}

// Static
void SessionModelAssociator::PopulateSessionHeaderFromSpecifics(
    const sync_pb::SessionHeader& header_specifics,
    base::Time mtime,
    SyncedSession* session_header) {
  if (header_specifics.has_client_name()) {
    session_header->session_name = header_specifics.client_name();
  }
  if (header_specifics.has_device_type()) {
    switch (header_specifics.device_type()) {
      case sync_pb::SyncEnums_DeviceType_TYPE_WIN:
        session_header->device_type = SyncedSession::TYPE_WIN;
        break;
      case sync_pb::SyncEnums_DeviceType_TYPE_MAC:
        session_header->device_type = SyncedSession::TYPE_MACOSX;
        break;
      case sync_pb::SyncEnums_DeviceType_TYPE_LINUX:
        session_header->device_type = SyncedSession::TYPE_LINUX;
        break;
      case sync_pb::SyncEnums_DeviceType_TYPE_CROS:
        session_header->device_type = SyncedSession::TYPE_CHROMEOS;
        break;
      case sync_pb::SyncEnums_DeviceType_TYPE_PHONE:
        session_header->device_type = SyncedSession::TYPE_PHONE;
        break;
      case sync_pb::SyncEnums_DeviceType_TYPE_TABLET:
        session_header->device_type = SyncedSession::TYPE_TABLET;
        break;
      case sync_pb::SyncEnums_DeviceType_TYPE_OTHER:
        // Intentionally fall-through
      default:
        session_header->device_type = SyncedSession::TYPE_OTHER;
        break;
    }
  }
  session_header->modified_time = mtime;
}

// Static
void SessionModelAssociator::PopulateSessionWindowFromSpecifics(
    const std::string& session_tag,
    const sync_pb::SessionWindow& specifics,
    base::Time mtime,
    SessionWindow* session_window,
    SyncedSessionTracker* tracker) {
  if (specifics.has_window_id())
    session_window->window_id.set_id(specifics.window_id());
  if (specifics.has_selected_tab_index())
    session_window->selected_tab_index = specifics.selected_tab_index();
  if (specifics.has_browser_type()) {
    if (specifics.browser_type() ==
        sync_pb::SessionWindow_BrowserType_TYPE_TABBED) {
      session_window->type = 1;
    } else {
      session_window->type = 2;
    }
  }
  session_window->timestamp = mtime;
  session_window->tabs.resize(specifics.tab_size(), NULL);
  for (int i = 0; i < specifics.tab_size(); i++) {
    SessionID::id_type tab_id = specifics.tab(i);
    tracker->PutTabInWindow(session_tag,
                            session_window->window_id.id(),
                            tab_id,
                            i);
  }
}

void SessionModelAssociator::LoadForeignTabFavicon(
    const sync_pb::SessionTab& tab) {
  // First go through and iterate over all the navigations, checking if any
  // have valid favicon urls.
  for (int i = 0; i < tab.navigation_size(); ++i) {
    if (!tab.navigation(i).favicon_url().empty()) {
      const std::string& page_url = tab.navigation(i).virtual_url();
      const std::string& favicon_url = tab.navigation(i).favicon_url();
      favicon_cache_.OnReceivedSyncFavicon(GURL(page_url),
                                           GURL(favicon_url),
                                           std::string(),
                                           syncer::TimeToProtoTime(
                                               base::Time::Now()));
    }
  }

  // Then go through and check for any legacy favicon data.
  if (!tab.has_favicon() || tab.favicon().empty())
    return;
  if (!tab.has_favicon_type() ||
      tab.favicon_type() != sync_pb::SessionTab::TYPE_WEB_FAVICON) {
    DVLOG(1) << "Ignoring non-web favicon.";
    return;
  }
  if (tab.navigation_size() == 0)
    return;
  int selected_index = tab.current_navigation_index();
  selected_index = std::max(
      0,
      std::min(selected_index,
               static_cast<int>(tab.navigation_size() - 1)));
  GURL navigation_url(tab.navigation(selected_index).virtual_url());
  if (!navigation_url.is_valid())
    return;
  GURL favicon_source(tab.favicon_source());
  if (!favicon_source.is_valid())
    return;

  const std::string& favicon = tab.favicon();
  DVLOG(1) << "Storing synced favicon for url " << navigation_url.spec()
           << " with size " << favicon.size() << " bytes.";
  favicon_cache_.OnReceivedSyncFavicon(navigation_url,
                                       favicon_source,
                                       favicon,
                                       syncer::TimeToProtoTime(
                                           base::Time::Now()));
}

bool SessionModelAssociator::UpdateSyncModelDataFromClient(
    syncer::SyncError* error) {
  DCHECK(CalledOnValidThread());

  // Associate all open windows and their tabs.
  return AssociateWindows(true, error);
}

void SessionModelAssociator::AttemptSessionsDataRefresh() const {
  DVLOG(1) << "Triggering sync refresh for sessions datatype.";
  const syncer::ModelTypeSet types(syncer::SESSIONS);
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_SYNC_REFRESH_LOCAL,
      content::Source<Profile>(profile_),
      content::Details<const syncer::ModelTypeSet>(&types));
}

bool SessionModelAssociator::GetLocalSession(
    const SyncedSession* * local_session) {
  DCHECK(CalledOnValidThread());
  if (current_machine_tag_.empty())
    return false;
  *local_session = synced_session_tracker_.GetSession(GetCurrentMachineTag());
  return true;
}

bool SessionModelAssociator::GetAllForeignSessions(
    std::vector<const SyncedSession*>* sessions) {
  DCHECK(CalledOnValidThread());
  return synced_session_tracker_.LookupAllForeignSessions(sessions);
}

bool SessionModelAssociator::GetForeignSession(
    const std::string& tag,
    std::vector<const SessionWindow*>* windows) {
  DCHECK(CalledOnValidThread());
  return synced_session_tracker_.LookupSessionWindows(tag, windows);
}

bool SessionModelAssociator::GetForeignTab(
    const std::string& tag,
    const SessionID::id_type tab_id,
    const SessionTab** tab) {
  DCHECK(CalledOnValidThread());
  const SessionTab* synced_tab = NULL;
  bool success = synced_session_tracker_.LookupSessionTab(tag,
                                                          tab_id,
                                                          &synced_tab);
  if (success)
    *tab = synced_tab;
  return success;
}

void SessionModelAssociator::DeleteStaleSessions() {
  DCHECK(CalledOnValidThread());
  std::vector<const SyncedSession*> sessions;
  if (!GetAllForeignSessions(&sessions))
    return;  // No foreign sessions.

  // Iterate through all the sessions and delete any with age older than
  // |stale_session_threshold_days_|.
  for (std::vector<const SyncedSession*>::const_iterator iter =
           sessions.begin(); iter != sessions.end(); ++iter) {
    const SyncedSession* session = *iter;
    int session_age_in_days =
        (base::Time::Now() - session->modified_time).InDays();
    std::string session_tag = session->session_tag;
    if (session_age_in_days > 0 &&  // If false, local clock is not trustworty.
        static_cast<size_t>(session_age_in_days) >
            stale_session_threshold_days_) {
      DVLOG(1) << "Found stale session " << session_tag
               << " with age " << session_age_in_days << ", deleting.";
      DeleteForeignSession(session_tag);
    }
  }
}

void SessionModelAssociator::SetStaleSessionThreshold(
    size_t stale_session_threshold_days) {
  DCHECK(CalledOnValidThread());
  if (stale_session_threshold_days_ == 0) {
    NOTREACHED() << "Attempted to set invalid stale session threshold.";
    return;
  }
  stale_session_threshold_days_ = stale_session_threshold_days;
  // TODO(zea): maybe make this preference-based? Might be nice to let users be
  // able to modify this once and forget about it. At the moment, if we want a
  // different threshold we will need to call this everytime we create a new
  // model associator and before we AssociateModels (probably from DTC).
}

void SessionModelAssociator::DeleteForeignSession(const std::string& tag) {
  DCHECK(CalledOnValidThread());
  if (tag == GetCurrentMachineTag()) {
    LOG(ERROR) << "Attempting to delete local session. This is not currently "
               << "supported.";
    return;
  }

  if (!DisassociateForeignSession(tag)) {
    // We don't have any data for this session, our work here is done!
    return;
  }

  syncer::WriteTransaction trans(FROM_HERE, sync_service_->GetUserShare());
  syncer::ReadNode root(&trans);
  if (root.InitByTagLookup(syncer::ModelTypeToRootTag(syncer::SESSIONS)) !=
                           syncer::BaseNode::INIT_OK) {
    LOG(ERROR) << kNoSessionsFolderError;
    return;
  }
  int64 id = root.GetFirstChildId();
  while (id != syncer::kInvalidId) {
    syncer::WriteNode sync_node(&trans);
    if (sync_node.InitByIdLookup(id) != syncer::BaseNode::INIT_OK) {
      LOG(ERROR) << "Failed to fetch sync node for id " << id;
      continue;
    }
    id = sync_node.GetSuccessorId();
    const sync_pb::SessionSpecifics& specifics =
        sync_node.GetSessionSpecifics();
    if (specifics.session_tag() == tag)
      sync_node.Tombstone();
  }
}

bool SessionModelAssociator::IsValidTab(const SyncedTabDelegate& tab) const {
  if ((!sync_service_ || tab.profile() != sync_service_->profile()) &&
      !setup_for_test_) {
    return false;
  }
  const SyncedWindowDelegate* window =
      SyncedWindowDelegate::FindSyncedWindowDelegateWithId(
          tab.GetWindowId());
  if (!window && !setup_for_test_)
    return false;
  return true;
}

bool SessionModelAssociator::TabHasValidEntry(
    const SyncedTabDelegate& tab) const {
  if (tab.ProfileIsManaged() && tab.GetBlockedNavigations()->size() > 0)
    return true;

  int entry_count = tab.GetEntryCount();
  if (entry_count == 0)
    return false;  // This deliberately ignores a new pending entry.

  int pending_index = tab.GetPendingEntryIndex();
  bool found_valid_url = false;
  for (int i = 0; i < entry_count; ++i) {
    const content::NavigationEntry* entry = (i == pending_index) ?
       tab.GetPendingEntry() : tab.GetEntryAtIndex(i);
    if (!entry)
      return false;
    if (entry->GetVirtualURL().is_valid() &&
        !entry->GetVirtualURL().SchemeIs("chrome") &&
        !entry->GetVirtualURL().SchemeIsFile()) {
      found_valid_url = true;
    }
  }
  return found_valid_url;
}

// If this functionality changes, browser_sync::ShouldSyncSessionTab should be
// modified to match.
bool SessionModelAssociator::ShouldSyncTab(const SyncedTabDelegate& tab) const {
  DCHECK(CalledOnValidThread());
  if (!IsValidTab(tab))
    return false;
  return TabHasValidEntry(tab);
}

void SessionModelAssociator::QuitLoopForSubtleTesting() {
  if (waiting_for_change_) {
    DVLOG(1) << "Quitting base::MessageLoop for test.";
    waiting_for_change_ = false;
    test_weak_factory_.InvalidateWeakPtrs();
    base::MessageLoop::current()->Quit();
  }
}

FaviconCache* SessionModelAssociator::GetFaviconCache() {
  return &favicon_cache_;
}

void SessionModelAssociator::BlockUntilLocalChangeForTest(
    base::TimeDelta timeout) {
  if (test_weak_factory_.HasWeakPtrs())
    return;
  waiting_for_change_ = true;
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&SessionModelAssociator::QuitLoopForSubtleTesting,
                 test_weak_factory_.GetWeakPtr()),
      timeout);
}

bool SessionModelAssociator::CryptoReadyIfNecessary() {
  // We only access the cryptographer while holding a transaction.
  syncer::ReadTransaction trans(FROM_HERE, sync_service_->GetUserShare());
  const syncer::ModelTypeSet encrypted_types = trans.GetEncryptedTypes();
  return !encrypted_types.Has(SESSIONS) ||
         sync_service_->IsCryptographerReady(&trans);
}

void SessionModelAssociator::UpdateTabIdIfNecessary(
    int tab_node_id,
    SessionID::id_type new_tab_id) {
  DCHECK_NE(tab_node_id, TabNodePool::kInvalidTabNodeID);
  SessionID::id_type old_tab_id =
      local_tab_pool_.GetTabIdFromTabNodeId(tab_node_id);
  if (old_tab_id != new_tab_id) {
    // Rewrite tab id if required.
    syncer::WriteTransaction trans(FROM_HERE, sync_service_->GetUserShare());
    syncer::WriteNode tab_node(&trans);
    if (tab_node.InitByClientTagLookup(syncer::SESSIONS,
            TabNodePool::TabIdToTag(current_machine_tag_, tab_node_id)) ==
                syncer::BaseNode::INIT_OK) {
      sync_pb::SessionSpecifics session_specifics =
          tab_node.GetSessionSpecifics();
      DCHECK(session_specifics.has_tab());
      if (session_specifics.has_tab()) {
        sync_pb::SessionTab* tab_s = session_specifics.mutable_tab();
        tab_s->set_tab_id(new_tab_id);
        tab_node.SetSessionSpecifics(session_specifics);
        // Update tab node pool with the new association.
        local_tab_pool_.ReassociateTabNode(tab_node_id, new_tab_id);
      }
    }
  }
}

}  // namespace browser_sync
