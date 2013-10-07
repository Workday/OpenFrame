// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/toolbar/recent_tabs_sub_menu_model.h"

#include "base/bind.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/tab_restore_service.h"
#include "chrome/browser/sessions/tab_restore_service_delegate.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/sync/glue/session_model_associator.h"
#include "chrome/browser/sync/glue/synced_session.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/favicon/favicon_types.h"
#include "chrome/common/pref_names.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/favicon_size.h"

#if defined(USE_ASH)
#include "ash/accelerators/accelerator_table.h"
#endif  // defined(USE_ASH)

namespace {

// First comamnd id for navigatable (and hence executable) tab menu item.
// The models and menu are not 1-1:
// - menu has "Reopen closed tab", "No tabs from other devices", device section
//   headers, separators and executable tab items.
// - |tab_navigation_items_| only has navigatabale/executable tab items.
// - |window_items_| only has executable open window items.
// Using an initial command ids for tab/window items makes it easier and less
// error-prone to manipulate the models and menu.
// These values must be bigger than the maximum possible number of items in
// menu, so that index of last menu item doesn't clash with this value when menu
// items are retrieved via GetIndexOfCommandId.
const int kFirstTabCommandId = 100;
const int kFirstWindowCommandId = 200;

// The maximum number of recently closed entries to be shown in the menu.
const int kMaxRecentlyClosedEntries = 8;

// Comparator function for use with std::sort that will sort sessions by
// descending modified_time (i.e., most recent first).
bool SortSessionsByRecency(const browser_sync::SyncedSession* s1,
                           const browser_sync::SyncedSession* s2) {
  return s1->modified_time > s2->modified_time;
}

// Comparator function for use with std::sort that will sort tabs by
// descending timestamp (i.e., most recent first).
bool SortTabsByRecency(const SessionTab* t1, const SessionTab* t2) {
  return t1->timestamp > t2->timestamp;
}

// Returns true if the command id is related to a tab model index.
bool IsTabModelCommandId(int command_id) {
  return command_id >= kFirstTabCommandId && command_id < kFirstWindowCommandId;
}

// Returns true if the command id is related to a window model index.
bool IsWindowModelCommandId(int command_id) {
  return command_id >= kFirstWindowCommandId &&
         command_id < RecentTabsSubMenuModel::kRecentlyClosedHeaderCommandId;
}

// Convert |tab_model_index| to command id of menu item.
int TabModelIndexToCommandId(int tab_model_index) {
  int command_id = tab_model_index + kFirstTabCommandId;
  DCHECK_LT(command_id, kFirstWindowCommandId);
  return command_id;
}

// Convert |command_id| of menu item to index in tab model.
int CommandIdToTabModelIndex(int command_id) {
  DCHECK_GE(command_id, kFirstTabCommandId);
  DCHECK_LT(command_id, kFirstWindowCommandId);
  return command_id - kFirstTabCommandId;
}

// Convert |window_model_index| to command id of menu item.
int WindowModelIndexToCommandId(int window_model_index) {
  int command_id = window_model_index + kFirstWindowCommandId;
  DCHECK_LT(command_id, RecentTabsSubMenuModel::kRecentlyClosedHeaderCommandId);
  return command_id;
}

// Convert |command_id| of menu item to index in window model.
int CommandIdToWindowModelIndex(int command_id) {
  DCHECK_GE(command_id, kFirstWindowCommandId);
  DCHECK_LT(command_id, RecentTabsSubMenuModel::kRecentlyClosedHeaderCommandId);
  return command_id - kFirstWindowCommandId;
}

}  // namespace

enum RecentTabAction {
  LOCAL_SESSION_TAB = 0,
  OTHER_DEVICE_TAB,
  RESTORE_WINDOW,
  SHOW_MORE,
  LIMIT_RECENT_TAB_ACTION
};

// An element in |RecentTabsSubMenuModel::tab_navigation_items_| that stores
// the navigation information of a local or foreign tab required to restore the
// tab.
struct RecentTabsSubMenuModel::TabNavigationItem {
  TabNavigationItem() : tab_id(-1) {}

  TabNavigationItem(const std::string& session_tag,
                    const SessionID::id_type& tab_id,
                    const string16& title,
                    const GURL& url)
      : session_tag(session_tag),
        tab_id(tab_id),
        title(title),
        url(url) {}

  // For use by std::set for sorting.
  bool operator<(const TabNavigationItem& other) const {
    return url < other.url;
  }

  std::string session_tag;  // Empty for local tabs, non-empty for foreign tabs.
  SessionID::id_type tab_id;  // -1 for invalid, >= 0 otherwise.
  string16 title;
  GURL url;
};

const int RecentTabsSubMenuModel::kRecentlyClosedHeaderCommandId = 500;
const int RecentTabsSubMenuModel::kDisabledRecentlyClosedHeaderCommandId = 501;
const int RecentTabsSubMenuModel::kDeviceNameCommandId = 1000;

RecentTabsSubMenuModel::RecentTabsSubMenuModel(
    ui::AcceleratorProvider* accelerator_provider,
    Browser* browser,
    browser_sync::SessionModelAssociator* associator)
    : ui::SimpleMenuModel(this),
      browser_(browser),
      associator_(associator),
      default_favicon_(ResourceBundle::GetSharedInstance().
          GetNativeImageNamed(IDR_DEFAULT_FAVICON)),
      weak_ptr_factory_(this) {
  Build();

  // Retrieve accelerator key for IDC_RESTORE_TAB now, because on ASH, it's not
  // defined in |accelerator_provider|, but in shell, so simply retrieve it now
  // for all ASH and non-ASH for use in |GetAcceleratorForCommandId|.
#if defined(USE_ASH)
  for (size_t i = 0; i < ash::kAcceleratorDataLength; ++i) {
    const ash::AcceleratorData& accel_data = ash::kAcceleratorData[i];
    if (accel_data.action == ash::RESTORE_TAB) {
      reopen_closed_tab_accelerator_ = ui::Accelerator(accel_data.keycode,
                                                       accel_data.modifiers);
      break;
    }
  }
#else
  if (accelerator_provider) {
    accelerator_provider->GetAcceleratorForCommandId(
        IDC_RESTORE_TAB, &reopen_closed_tab_accelerator_);
  }
#endif  // defined(USE_ASH)
}

RecentTabsSubMenuModel::~RecentTabsSubMenuModel() {
}

bool RecentTabsSubMenuModel::IsCommandIdChecked(int command_id) const {
  return false;
}

bool RecentTabsSubMenuModel::IsCommandIdEnabled(int command_id) const {
  if (command_id == kRecentlyClosedHeaderCommandId ||
      command_id == kDisabledRecentlyClosedHeaderCommandId ||
      command_id == kDeviceNameCommandId ||
      command_id == IDC_RECENT_TABS_NO_DEVICE_TABS) {
    return false;
  }
  return true;
}

bool RecentTabsSubMenuModel::GetAcceleratorForCommandId(
    int command_id, ui::Accelerator* accelerator) {
  // If there are no recently closed items, we show the accelerator beside
  // the header, otherwise, we show it beside the first item underneath it.
  int index_in_menu = GetIndexOfCommandId(command_id);
  int header_index = GetIndexOfCommandId(kRecentlyClosedHeaderCommandId);
  if ((command_id == kDisabledRecentlyClosedHeaderCommandId ||
       (header_index != -1 && index_in_menu == header_index + 1)) &&
      reopen_closed_tab_accelerator_.key_code() != ui::VKEY_UNKNOWN) {
    *accelerator = reopen_closed_tab_accelerator_;
    return true;
  }
  return false;
}

void RecentTabsSubMenuModel::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == IDC_SHOW_HISTORY) {
    UMA_HISTOGRAM_ENUMERATION("WrenchMenu.RecentTabsSubMenu", SHOW_MORE,
                              LIMIT_RECENT_TAB_ACTION);
    // We show all "other devices" on the history page.
    chrome::ExecuteCommandWithDisposition(browser_, IDC_SHOW_HISTORY,
        ui::DispositionFromEventFlags(event_flags));
    return;
  }

  DCHECK_NE(kDeviceNameCommandId, command_id);
  DCHECK_NE(IDC_RECENT_TABS_NO_DEVICE_TABS, command_id);

  WindowOpenDisposition disposition =
      ui::DispositionFromEventFlags(event_flags);
  if (disposition == CURRENT_TAB)  // Force to open a new foreground tab.
    disposition = NEW_FOREGROUND_TAB;

  TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfile(browser_->profile());
  TabRestoreServiceDelegate* delegate =
      TabRestoreServiceDelegate::FindDelegateForWebContents(
          browser_->tab_strip_model()->GetActiveWebContents());
  if (IsTabModelCommandId(command_id)) {
    int model_idx = CommandIdToTabModelIndex(command_id);
    DCHECK(model_idx >= 0 &&
           model_idx < static_cast<int>(tab_navigation_items_.size()));
    const TabNavigationItem& item = tab_navigation_items_[model_idx];
    DCHECK(item.tab_id > -1 && item.url.is_valid());

    if (item.session_tag.empty()) {  // Restore tab of local session.
      if (service && delegate) {
        UMA_HISTOGRAM_ENUMERATION("WrenchMenu.RecentTabsSubMenu",
                                  LOCAL_SESSION_TAB, LIMIT_RECENT_TAB_ACTION);
        service->RestoreEntryById(delegate, item.tab_id,
                                  browser_->host_desktop_type(), disposition);
      }
    } else {  // Restore tab of foreign session.
      browser_sync::SessionModelAssociator* associator = GetModelAssociator();
      if (!associator)
        return;
      const SessionTab* tab;
      if (!associator->GetForeignTab(item.session_tag, item.tab_id, &tab))
        return;
      if (tab->navigations.empty())
        return;
      UMA_HISTOGRAM_ENUMERATION("WrenchMenu.RecentTabsSubMenu",
                                OTHER_DEVICE_TAB, LIMIT_RECENT_TAB_ACTION);
      SessionRestore::RestoreForeignSessionTab(
          browser_->tab_strip_model()->GetActiveWebContents(),
          *tab, disposition);
    }
  } else {
    DCHECK(IsWindowModelCommandId(command_id));
    if (service && delegate) {
      int model_idx = CommandIdToWindowModelIndex(command_id);
      DCHECK(model_idx >= 0 &&
             model_idx < static_cast<int>(window_items_.size()));
      UMA_HISTOGRAM_ENUMERATION("WrenchMenu.RecentTabsSubMenu", RESTORE_WINDOW,
                                LIMIT_RECENT_TAB_ACTION);
      service->RestoreEntryById(delegate, window_items_[model_idx],
                                browser_->host_desktop_type(), disposition);
    }
  }
}

const gfx::Font* RecentTabsSubMenuModel::GetLabelFontAt(int index) const {
  int command_id = GetCommandIdAt(index);
  if (command_id == kDeviceNameCommandId ||
      command_id == kRecentlyClosedHeaderCommandId) {
    return &ResourceBundle::GetSharedInstance().GetFont(
        ResourceBundle::BoldFont);
  }
  return NULL;
}

int RecentTabsSubMenuModel::GetMaxWidthForItemAtIndex(int item_index) const {
  int command_id = GetCommandIdAt(item_index);
  if (command_id == IDC_RECENT_TABS_NO_DEVICE_TABS ||
      command_id == kRecentlyClosedHeaderCommandId ||
      command_id == kDisabledRecentlyClosedHeaderCommandId) {
    return -1;
  }
  return 320;
}

bool RecentTabsSubMenuModel::GetURLAndTitleForItemAtIndex(
    int index,
    std::string* url,
    string16* title) const {
  int command_id = GetCommandIdAt(index);
  if (IsTabModelCommandId(command_id)) {
    int model_idx = CommandIdToTabModelIndex(command_id);
    DCHECK(model_idx >= 0 &&
           model_idx < static_cast<int>(tab_navigation_items_.size()));
    *url = tab_navigation_items_[model_idx].url.possibly_invalid_spec();
    *title = tab_navigation_items_[model_idx].title;
    return true;
  }
  return false;
}

void RecentTabsSubMenuModel::Build() {
  // The menu contains:
  // - Recently closed tabs header, then list of tabs, then separator
  // - device 1 section header, then list of tabs from device, then separator
  // - device 2 section header, then list of tabs from device, then separator
  // - device 3 section header, then list of tabs from device, then separator
  // - More... to open the history tab to get more other devices.
  // |tab_navigation_items_| only contains navigatable (and hence executable)
  // tab items for other devices, and |window_items_| contains the recently
  // closed windows.
  BuildRecentTabs();
  BuildDevices();
}

void RecentTabsSubMenuModel::BuildRecentTabs() {
  ListValue recently_closed_list;
  TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfile(browser_->profile());
  if (service) {
    // This does nothing if the tabs have already been loaded or they
    // shouldn't be loaded.
    service->LoadTabsFromLastSession();
  }

  if (!service || service->entries().size() == 0) {
    // This is to show a disabled restore tab entry with the accelerator to
    // teach users about this command.
    AddItemWithStringId(kDisabledRecentlyClosedHeaderCommandId,
                        IDS_NEW_TAB_RECENTLY_CLOSED);
    return;
  }

  AddItemWithStringId(kRecentlyClosedHeaderCommandId,
                      IDS_NEW_TAB_RECENTLY_CLOSED);
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  SetIcon(GetItemCount() - 1,
          rb.GetNativeImageNamed(IDR_RECENTLY_CLOSED_WINDOW));

  int added_count = 0;
  TabRestoreService::Entries entries = service->entries();
  for (TabRestoreService::Entries::const_iterator it = entries.begin();
       it != entries.end() && added_count < kMaxRecentlyClosedEntries; ++it) {
    TabRestoreService::Entry* entry = *it;
    if (entry->type == TabRestoreService::TAB) {
      TabRestoreService::Tab* tab = static_cast<TabRestoreService::Tab*>(entry);
      const sessions::SerializedNavigationEntry& current_navigation =
          tab->navigations.at(tab->current_navigation_index);
      BuildLocalTabItem(
          entry->id,
          current_navigation.title(),
          current_navigation.virtual_url());
    } else  {
      DCHECK_EQ(entry->type, TabRestoreService::WINDOW);
      BuildWindowItem(
          entry->id,
          static_cast<TabRestoreService::Window*>(entry)->tabs.size());
    }
    ++added_count;
  }
}

void RecentTabsSubMenuModel::BuildDevices() {
  browser_sync::SessionModelAssociator* associator = GetModelAssociator();
  std::vector<const browser_sync::SyncedSession*> sessions;
  if (!associator || !associator->GetAllForeignSessions(&sessions)) {
    AddSeparator(ui::NORMAL_SEPARATOR);
    AddItemWithStringId(IDC_RECENT_TABS_NO_DEVICE_TABS,
                        IDS_RECENT_TABS_NO_DEVICE_TABS);
    return;
  }

  // Sort sessions from most recent to least recent.
  std::sort(sessions.begin(), sessions.end(), SortSessionsByRecency);

  const size_t kMaxSessionsToShow = 3;
  size_t num_sessions_added = 0;
  for (size_t i = 0;
       i < sessions.size() && num_sessions_added < kMaxSessionsToShow; ++i) {
    const browser_sync::SyncedSession* session = sessions[i];
    const std::string& session_tag = session->session_tag;

    // Get windows of session.
    std::vector<const SessionWindow*> windows;
    if (!associator->GetForeignSession(session_tag, &windows) ||
        windows.empty()) {
      continue;
    }

    // Collect tabs from all windows of session, pruning those that are not
    // syncable or are NewTabPage, then sort them from most recent to least
    // recent, independent of which window the tabs were from.
    std::vector<const SessionTab*> tabs_in_session;
    for (size_t j = 0; j < windows.size(); ++j) {
      const SessionWindow* window = windows[j];
      for (size_t t = 0; t < window->tabs.size(); ++t) {
        const SessionTab* tab = window->tabs[t];
        if (tab->navigations.empty())
          continue;
        const sessions::SerializedNavigationEntry& current_navigation =
            tab->navigations.at(tab->normalized_navigation_index());
        if (chrome::IsNTPURL(current_navigation.virtual_url(),
                             browser_->profile())) {
          continue;
        }
        tabs_in_session.push_back(tab);
      }
    }
    if (tabs_in_session.empty())
      continue;
    std::sort(tabs_in_session.begin(), tabs_in_session.end(),
              SortTabsByRecency);

    // Add the header for the device session.
    DCHECK(!session->session_name.empty());
    AddSeparator(ui::NORMAL_SEPARATOR);
    AddItem(kDeviceNameCommandId, UTF8ToUTF16(session->session_name));
    AddDeviceFavicon(GetItemCount() - 1, session->device_type);

    // Build tab menu items from sorted session tabs.
    const size_t kMaxTabsPerSessionToShow = 4;
    for (size_t k = 0;
         k < std::min(tabs_in_session.size(), kMaxTabsPerSessionToShow);
         ++k) {
      BuildForeignTabItem(session_tag, *tabs_in_session[k]);
    }  // for all tabs in one session

    ++num_sessions_added;
  }  // for all sessions

  // We are not supposed to get here unless at least some items were added.
  DCHECK_GT(GetItemCount(), 0);
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItemWithStringId(IDC_SHOW_HISTORY, IDS_RECENT_TABS_MORE);
}

void RecentTabsSubMenuModel::BuildLocalTabItem(
    int session_id,
    const string16& title,
    const GURL& url) {
  TabNavigationItem item("", session_id, title, url);
  int command_id = TabModelIndexToCommandId(tab_navigation_items_.size());
  // There may be no tab title, in which case, use the url as tab title.
  AddItem(command_id, title.empty() ? UTF8ToUTF16(item.url.spec()) : title);
  AddTabFavicon(tab_navigation_items_.size(), command_id, item.url);
  tab_navigation_items_.push_back(item);
}

void RecentTabsSubMenuModel::BuildForeignTabItem(
    const std::string& session_tag,
    const SessionTab& tab) {
  const sessions::SerializedNavigationEntry& current_navigation =
      tab.navigations.at(tab.normalized_navigation_index());
  TabNavigationItem item(session_tag, tab.tab_id.id(),
                         current_navigation.title(),
                         current_navigation.virtual_url());
  int command_id = TabModelIndexToCommandId(tab_navigation_items_.size());
  // There may be no tab title, in which case, use the url as tab title.
  AddItem(command_id,
          current_navigation.title().empty() ?
              UTF8ToUTF16(item.url.spec()) : current_navigation.title());
  AddTabFavicon(tab_navigation_items_.size(), command_id, item.url);
  tab_navigation_items_.push_back(item);
}

void RecentTabsSubMenuModel::BuildWindowItem(
    const SessionID::id_type& window_id,
    int num_tabs) {
  int command_id = WindowModelIndexToCommandId(window_items_.size());
  if (num_tabs == 1) {
    AddItemWithStringId(command_id, IDS_NEW_TAB_RECENTLY_CLOSED_WINDOW_SINGLE);
  } else {
    AddItem(command_id, l10n_util::GetStringFUTF16(
        IDS_NEW_TAB_RECENTLY_CLOSED_WINDOW_MULTIPLE,
        base::IntToString16(num_tabs)));
  }
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  SetIcon(GetItemCount() - 1,
          rb.GetNativeImageNamed(IDR_RECENTLY_CLOSED_WINDOW));
  window_items_.push_back(window_id);
}

void RecentTabsSubMenuModel::AddDeviceFavicon(
    int index_in_menu,
    browser_sync::SyncedSession::DeviceType device_type) {
  int favicon_id = -1;
  switch (device_type) {
    case browser_sync::SyncedSession::TYPE_PHONE:
      favicon_id = IDR_PHONE_FAVICON;
      break;

    case browser_sync::SyncedSession::TYPE_TABLET:
      favicon_id = IDR_TABLET_FAVICON;
      break;

    case browser_sync::SyncedSession::TYPE_CHROMEOS:
    case browser_sync::SyncedSession::TYPE_WIN:
    case browser_sync::SyncedSession::TYPE_MACOSX:
    case browser_sync::SyncedSession::TYPE_LINUX:
    case browser_sync::SyncedSession::TYPE_OTHER:
    case browser_sync::SyncedSession::TYPE_UNSET:
      favicon_id = IDR_LAPTOP_FAVICON;
      break;
  };

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  SetIcon(index_in_menu, rb.GetNativeImageNamed(favicon_id));
}

void RecentTabsSubMenuModel::AddTabFavicon(int model_index,
                                           int command_id,
                                           const GURL& url) {
  int index_in_menu = GetIndexOfCommandId(command_id);

  // If tab has synced favicon, use it.
  // Note that currently, foreign tab only has favicon if --sync-tab-favicons
  // switch is on; according to zea@, this flag is now automatically enabled for
  // iOS and android, and they're looking into enabling it for other platforms.
  browser_sync::SessionModelAssociator* associator = GetModelAssociator();
  scoped_refptr<base::RefCountedMemory> favicon_png;
  if (associator &&
      associator->GetSyncedFaviconForPageURL(url.spec(), &favicon_png)) {
    gfx::Image image = gfx::Image::CreateFrom1xPNGBytes(
        favicon_png->front(),
        favicon_png->size());
    SetIcon(index_in_menu, image);
    return;
  }

  // Otherwise, start to fetch the favicon from local history asynchronously.
  // Set default icon first.
  SetIcon(index_in_menu, default_favicon_);
  // Start request to fetch actual icon if possible.
  FaviconService* favicon_service = FaviconServiceFactory::GetForProfile(
      browser_->profile(), Profile::EXPLICIT_ACCESS);
  if (!favicon_service)
    return;

  favicon_service->GetFaviconImageForURL(
      FaviconService::FaviconForURLParams(browser_->profile(),
                                          url,
                                          chrome::FAVICON,
                                          gfx::kFaviconSize),
      base::Bind(&RecentTabsSubMenuModel::OnFaviconDataAvailable,
                 weak_ptr_factory_.GetWeakPtr(),
                 command_id),
      &cancelable_task_tracker_);
}

void RecentTabsSubMenuModel::OnFaviconDataAvailable(
    int command_id,
    const chrome::FaviconImageResult& image_result) {
  if (image_result.image.IsEmpty())
    return;
  DCHECK(!tab_navigation_items_.empty());
  int index_in_menu = GetIndexOfCommandId(command_id);
  DCHECK(index_in_menu != -1);
  SetIcon(index_in_menu, image_result.image);
  if (GetMenuModelDelegate())
    GetMenuModelDelegate()->OnIconChanged(index_in_menu);
}

browser_sync::SessionModelAssociator*
    RecentTabsSubMenuModel::GetModelAssociator() {
  if (!associator_) {
    ProfileSyncService* service = ProfileSyncServiceFactory::GetInstance()->
        GetForProfile(browser_->profile());
    // Only return the associator if it exists and it is done syncing sessions.
    if (service && service->ShouldPushChanges())
      associator_ = service->GetSessionModelAssociator();
  }
  return associator_;
}
