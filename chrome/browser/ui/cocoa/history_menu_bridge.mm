// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/history_menu_bridge.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"  // IDC_HISTORY_MENU
#import "chrome/browser/app_controller_mac.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/history/page_usage_data.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_types.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#import "chrome/browser/ui/cocoa/history_menu_cocoa_controller.h"
#include "chrome/common/favicon/favicon_types.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/image/image.h"

namespace {

// Menus more than this many chars long will get trimmed.
const NSUInteger kMaximumMenuWidthInChars = 50;

// When trimming, use this many chars from each side.
const NSUInteger kMenuTrimSizeInChars = 25;

// Number of days to consider when getting the number of visited items.
const int kVisitedScope = 90;

// The number of visisted results to get.
const int kVisitedCount = 15;

// The number of recently closed items to get.
const unsigned int kRecentlyClosedCount = 10;

}  // namespace

HistoryMenuBridge::HistoryItem::HistoryItem()
   : icon_requested(false),
     icon_task_id(CancelableTaskTracker::kBadTaskId),
     menu_item(nil),
     session_id(0) {
}

HistoryMenuBridge::HistoryItem::HistoryItem(const HistoryItem& copy)
   : title(copy.title),
     url(copy.url),
     icon_requested(false),
     icon_task_id(CancelableTaskTracker::kBadTaskId),
     menu_item(nil),
     session_id(copy.session_id) {
}

HistoryMenuBridge::HistoryItem::~HistoryItem() {
}

HistoryMenuBridge::HistoryMenuBridge(Profile* profile)
    : controller_([[HistoryMenuCocoaController alloc] initWithBridge:this]),
      profile_(profile),
      history_service_(NULL),
      tab_restore_service_(NULL),
      create_in_progress_(false),
      need_recreate_(false) {
  // If we don't have a profile, do not bother initializing our data sources.
  // This shouldn't happen except in unit tests.
  if (profile_) {
    // Check to see if the history service is ready. Because it loads async, it
    // may not be ready when the Bridge is created. If this happens, register
    // for a notification that tells us the HistoryService is ready.
    HistoryService* hs = HistoryServiceFactory::GetForProfile(
        profile_, Profile::EXPLICIT_ACCESS);
    if (hs != NULL && hs->BackendLoaded()) {
      history_service_ = hs;
      Init();
    }

    tab_restore_service_ = TabRestoreServiceFactory::GetForProfile(profile_);
    if (tab_restore_service_) {
      tab_restore_service_->AddObserver(this);
      // If the tab entries are already loaded, invoke the observer method to
      // build the "Recently Closed" section. Otherwise it will be when the
      // backend loads.
      if (!tab_restore_service_->IsLoaded())
        tab_restore_service_->LoadTabsFromLastSession();
      else
        TabRestoreServiceChanged(tab_restore_service_);
    }
  }

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  default_favicon_.reset(
      rb.GetNativeImageNamed(IDR_DEFAULT_FAVICON).CopyNSImage());

  // Set the static icons in the menu.
  NSMenuItem* item = [HistoryMenu() itemWithTag:IDC_SHOW_HISTORY];
  [item setImage:rb.GetNativeImageNamed(IDR_HISTORY_FAVICON).ToNSImage()];

  // The service is not ready for use yet, so become notified when it does.
  if (!history_service_) {
    registrar_.Add(
        this, chrome::NOTIFICATION_HISTORY_LOADED,
        content::Source<Profile>(profile_));
  }
}

// Note that all requests sent to either the history service or the favicon
// service will be automatically cancelled by their respective Consumers, so
// task cancellation is not done manually here in the dtor.
HistoryMenuBridge::~HistoryMenuBridge() {
  // Unregister ourselves as observers and notifications.
  DCHECK(profile_);
  if (history_service_) {
    registrar_.Remove(this, chrome::NOTIFICATION_HISTORY_URLS_MODIFIED,
                      content::Source<Profile>(profile_));
    registrar_.Remove(this, chrome::NOTIFICATION_HISTORY_URL_VISITED,
                      content::Source<Profile>(profile_));
    registrar_.Remove(this, chrome::NOTIFICATION_HISTORY_URLS_DELETED,
                      content::Source<Profile>(profile_));
  } else {
    registrar_.Remove(this, chrome::NOTIFICATION_HISTORY_LOADED,
                      content::Source<Profile>(profile_));
  }

  if (tab_restore_service_)
    tab_restore_service_->RemoveObserver(this);

  // Since the map owns the HistoryItems, delete anything that still exists.
  std::map<NSMenuItem*, HistoryItem*>::iterator it = menu_item_map_.begin();
  while (it != menu_item_map_.end()) {
    HistoryItem* item  = it->second;
    menu_item_map_.erase(it++);
    delete item;
  }
}

void HistoryMenuBridge::Observe(int type,
                                const content::NotificationSource& source,
                                const content::NotificationDetails& details) {
  // A history service is now ready. Check to see if it's the one for the main
  // profile. If so, perform final initialization.
  if (type == chrome::NOTIFICATION_HISTORY_LOADED) {
    HistoryService* hs = HistoryServiceFactory::GetForProfile(
        profile_, Profile::EXPLICIT_ACCESS);
    if (hs != NULL && hs->BackendLoaded()) {
      history_service_ = hs;
      Init();

      // Found our HistoryService, so stop listening for this notification.
      registrar_.Remove(this,
                        chrome::NOTIFICATION_HISTORY_LOADED,
                        content::Source<Profile>(profile_));
    }
  }

  // All other notification types that we observe indicate that the history has
  // changed and we need to rebuild.
  need_recreate_ = true;
  CreateMenu();
}

void HistoryMenuBridge::TabRestoreServiceChanged(TabRestoreService* service) {
  const TabRestoreService::Entries& entries = service->entries();

  // Clear the history menu before rebuilding.
  NSMenu* menu = HistoryMenu();
  ClearMenuSection(menu, kRecentlyClosed);

  // Index for the next menu item.
  NSInteger index = [menu indexOfItemWithTag:kRecentlyClosedTitle] + 1;
  NSUInteger added_count = 0;

  for (TabRestoreService::Entries::const_iterator it = entries.begin();
       it != entries.end() && added_count < kRecentlyClosedCount; ++it) {
    TabRestoreService::Entry* entry = *it;

    // If this is a window, create a submenu for all of its tabs.
    if (entry->type == TabRestoreService::WINDOW) {
      TabRestoreService::Window* entry_win = (TabRestoreService::Window*)entry;
      std::vector<TabRestoreService::Tab>& tabs = entry_win->tabs;
      if (!tabs.size())
        continue;

      // Create the item for the parent/window. Do not set the title yet because
      // the actual number of items that are in the menu will not be known until
      // things like the NTP are filtered out, which is done when the tab items
      // are actually created.
      HistoryItem* item = new HistoryItem();
      item->session_id = entry_win->id;

      // Create the submenu.
      base::scoped_nsobject<NSMenu> submenu([[NSMenu alloc] init]);

      // Create standard items within the window submenu.
      NSString* restore_title = l10n_util::GetNSString(
          IDS_HISTORY_CLOSED_RESTORE_WINDOW_MAC);
      base::scoped_nsobject<NSMenuItem> restore_item(
          [[NSMenuItem alloc] initWithTitle:restore_title
                                     action:@selector(openHistoryMenuItem:)
                              keyEquivalent:@""]);
      [restore_item setTarget:controller_.get()];
      // Duplicate the HistoryItem otherwise the different NSMenuItems will
      // point to the same HistoryItem, which would then be double-freed when
      // removing the items from the map or in the dtor.
      HistoryItem* dup_item = new HistoryItem(*item);
      menu_item_map_.insert(std::make_pair(restore_item.get(), dup_item));
      [submenu addItem:restore_item.get()];
      [submenu addItem:[NSMenuItem separatorItem]];

      // Loop over the window's tabs and add them to the submenu.
      NSInteger subindex = [[submenu itemArray] count];
      std::vector<TabRestoreService::Tab>::const_iterator it;
      for (it = tabs.begin(); it != tabs.end(); ++it) {
        TabRestoreService::Tab tab = *it;
        HistoryItem* tab_item = HistoryItemForTab(tab);
        if (tab_item) {
          item->tabs.push_back(tab_item);
          AddItemToMenu(tab_item, submenu.get(), kRecentlyClosed + 1,
                        subindex++);
        }
      }

      // Now that the number of tabs that has been added is known, set the title
      // of the parent menu item.
      if (item->tabs.size() == 1) {
        item->title = l10n_util::GetStringUTF16(
            IDS_NEW_TAB_RECENTLY_CLOSED_WINDOW_SINGLE);
      } else {
        item->title =l10n_util::GetStringFUTF16(
            IDS_NEW_TAB_RECENTLY_CLOSED_WINDOW_MULTIPLE,
                base::IntToString16(item->tabs.size()));
      }

      // Sometimes it is possible for there to not be any subitems for a given
      // window; if that is the case, do not add the entry to the main menu.
      if ([[submenu itemArray] count] > 2) {
        // Create the menu item parent.
        NSMenuItem* parent_item =
            AddItemToMenu(item, menu, kRecentlyClosed, index++);
        [parent_item setSubmenu:submenu.get()];
        ++added_count;
      }
    } else if (entry->type == TabRestoreService::TAB) {
      TabRestoreService::Tab* tab =
          static_cast<TabRestoreService::Tab*>(entry);
      HistoryItem* item = HistoryItemForTab(*tab);
      if (item) {
        AddItemToMenu(item, menu, kRecentlyClosed, index++);
        ++added_count;
      }
    }
  }
}

void HistoryMenuBridge::TabRestoreServiceDestroyed(
    TabRestoreService* service) {
  // Intentionally left blank. We hold a weak reference to the service.
}

void HistoryMenuBridge::ResetMenu() {
  NSMenu* menu = HistoryMenu();
  ClearMenuSection(menu, kVisited);
  ClearMenuSection(menu, kRecentlyClosed);
}

void HistoryMenuBridge::BuildMenu() {
  // If the history service is ready, use it. Otherwise, a Notification will
  // force an update when it's loaded.
  if (history_service_)
    CreateMenu();
}

HistoryMenuBridge::HistoryItem* HistoryMenuBridge::HistoryItemForMenuItem(
    NSMenuItem* item) {
  std::map<NSMenuItem*, HistoryItem*>::iterator it = menu_item_map_.find(item);
  if (it != menu_item_map_.end()) {
    return it->second;
  }
  return NULL;
}

HistoryService* HistoryMenuBridge::service() {
  return history_service_;
}

Profile* HistoryMenuBridge::profile() {
  return profile_;
}

NSMenu* HistoryMenuBridge::HistoryMenu() {
  NSMenu* history_menu = [[[NSApp mainMenu] itemWithTag:IDC_HISTORY_MENU]
                            submenu];
  return history_menu;
}

void HistoryMenuBridge::ClearMenuSection(NSMenu* menu, NSInteger tag) {
  for (NSMenuItem* menu_item in [menu itemArray]) {
    if ([menu_item tag] == tag  && [menu_item target] == controller_.get()) {
      // This is an item that should be removed, so find the corresponding model
      // item.
      HistoryItem* item = HistoryItemForMenuItem(menu_item);

      // Cancel favicon requests that could hold onto stale pointers. Also
      // remove the item from the mapping.
      if (item) {
        CancelFaviconRequest(item);
        menu_item_map_.erase(menu_item);
        delete item;
      }

      // If this menu item has a submenu, recurse.
      if ([menu_item hasSubmenu]) {
        ClearMenuSection([menu_item submenu], tag + 1);
      }

      // Now actually remove the item from the menu.
      [menu removeItem:menu_item];
    }
  }
}

NSMenuItem* HistoryMenuBridge::AddItemToMenu(HistoryItem* item,
                                             NSMenu* menu,
                                             NSInteger tag,
                                             NSInteger index) {
  NSString* title = base::SysUTF16ToNSString(item->title);
  std::string url_string = item->url.possibly_invalid_spec();

  // If we don't have a title, use the URL.
  if ([title isEqualToString:@""])
    title = base::SysUTF8ToNSString(url_string);
  NSString* full_title = title;
  if ([title length] > kMaximumMenuWidthInChars) {
    // TODO(rsesek): use app/text_elider.h once it uses string16 and can
    // take out the middle of strings.
    title = [NSString stringWithFormat:@"%@…%@",
               [title substringToIndex:kMenuTrimSizeInChars],
               [title substringFromIndex:([title length] -
                                          kMenuTrimSizeInChars)]];
  }
  item->menu_item.reset(
      [[NSMenuItem alloc] initWithTitle:title
                                 action:nil
                          keyEquivalent:@""]);
  [item->menu_item setTarget:controller_];
  [item->menu_item setAction:@selector(openHistoryMenuItem:)];
  [item->menu_item setTag:tag];
  if (item->icon.get())
    [item->menu_item setImage:item->icon.get()];
  else if (!item->tabs.size())
    [item->menu_item setImage:default_favicon_.get()];

  // Add a tooltip.
  NSString* tooltip = [NSString stringWithFormat:@"%@\n%s", full_title,
                                url_string.c_str()];
  [item->menu_item setToolTip:tooltip];

  [menu insertItem:item->menu_item.get() atIndex:index];
  menu_item_map_.insert(std::make_pair(item->menu_item.get(), item));

  return item->menu_item.get();
}

void HistoryMenuBridge::Init() {
  registrar_.Add(this, chrome::NOTIFICATION_HISTORY_URLS_MODIFIED,
                 content::Source<Profile>(profile_));
  registrar_.Add(this, chrome::NOTIFICATION_HISTORY_URL_VISITED,
                 content::Source<Profile>(profile_));
  registrar_.Add(this, chrome::NOTIFICATION_HISTORY_URLS_DELETED,
                 content::Source<Profile>(profile_));
}

void HistoryMenuBridge::CreateMenu() {
  // If we're currently running CreateMenu(), wait until it finishes.
  if (create_in_progress_)
    return;
  create_in_progress_ = true;
  need_recreate_ = false;

  DCHECK(history_service_);

  history::QueryOptions options;
  options.max_count = kVisitedCount;
  options.SetRecentDayRange(kVisitedScope);

  history_service_->QueryHistory(
      string16(),
      options,
      &cancelable_request_consumer_,
      base::Bind(&HistoryMenuBridge::OnVisitedHistoryResults,
                 base::Unretained(this)));
}

void HistoryMenuBridge::OnVisitedHistoryResults(
    CancelableRequestProvider::Handle handle,
    history::QueryResults* results) {
  NSMenu* menu = HistoryMenu();
  ClearMenuSection(menu, kVisited);
  NSInteger top_item = [menu indexOfItemWithTag:kVisitedTitle] + 1;

  size_t count = results->size();
  for (size_t i = 0; i < count; ++i) {
    const history::URLResult& result = (*results)[i];

    HistoryItem* item = new HistoryItem;
    item->title = result.title();
    item->url = result.url();

    // Need to explicitly get the favicon for each row.
    GetFaviconForHistoryItem(item);

    // This will add |item| to the |menu_item_map_|, which takes ownership.
    AddItemToMenu(item, HistoryMenu(), kVisited, top_item + i);
  }

  // We are already invalid by the time we finished, darn.
  if (need_recreate_)
    CreateMenu();

  create_in_progress_ = false;
}

HistoryMenuBridge::HistoryItem* HistoryMenuBridge::HistoryItemForTab(
    const TabRestoreService::Tab& entry) {
  DCHECK(!entry.navigations.empty());

  const sessions::SerializedNavigationEntry& current_navigation =
      entry.navigations.at(entry.current_navigation_index);
  HistoryItem* item = new HistoryItem();
  item->title = current_navigation.title();
  item->url = current_navigation.virtual_url();
  item->session_id = entry.id;

  // Tab navigations don't come with icons, so we always have to request them.
  GetFaviconForHistoryItem(item);

  return item;
}

void HistoryMenuBridge::GetFaviconForHistoryItem(HistoryItem* item) {
  FaviconService* service =
      FaviconServiceFactory::GetForProfile(profile_, Profile::EXPLICIT_ACCESS);
  CancelableTaskTracker::TaskId task_id = service->GetFaviconImageForURL(
      FaviconService::FaviconForURLParams(profile_,
                                          item->url,
                                          chrome::FAVICON,
                                          gfx::kFaviconSize),
      base::Bind(&HistoryMenuBridge::GotFaviconData,
                 base::Unretained(this),
                 item),
      &cancelable_task_tracker_);
  item->icon_task_id = task_id;
  item->icon_requested = true;
}

void HistoryMenuBridge::GotFaviconData(
    HistoryItem* item,
    const chrome::FaviconImageResult& image_result) {
  // Since we're going to do Cocoa-y things, make sure this is the main thread.
  DCHECK([NSThread isMainThread]);

  DCHECK(item);
  item->icon_requested = false;
  item->icon_task_id = CancelableTaskTracker::kBadTaskId;

  NSImage* image = image_result.image.AsNSImage();
  if (image) {
    item->icon.reset([image retain]);
    [item->menu_item setImage:item->icon.get()];
  }
}

void HistoryMenuBridge::CancelFaviconRequest(HistoryItem* item) {
  DCHECK(item);
  if (item->icon_requested) {
    cancelable_task_tracker_.TryCancel(item->icon_task_id);
    item->icon_requested = false;
    item->icon_task_id = CancelableTaskTracker::kBadTaskId;
  }
}
