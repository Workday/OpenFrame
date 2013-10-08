// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_THEMES_THEME_SYNCABLE_SERVICE_H_
#define CHROME_BROWSER_THEMES_THEME_SYNCABLE_SERVICE_H_

#include "base/gtest_prod_util.h"
#include "base/threading/thread_checker.h"
#include "sync/api/sync_change.h"
#include "sync/api/sync_data.h"
#include "sync/api/sync_error_factory.h"
#include "sync/api/sync_error.h"
#include "sync/api/syncable_service.h"

class Profile;
class ThemeService;
class ThemeSyncableServiceTest;

namespace sync_pb {
class ThemeSpecifics;
}

class ThemeSyncableService : public syncer::SyncableService {
 public:
  ThemeSyncableService(Profile* profile,  // Same profile used by theme_service.
                       ThemeService* theme_service);
  virtual ~ThemeSyncableService();

  static syncer::ModelType model_type() { return syncer::THEMES; }

  // Called by ThemeService when user changes theme.
  void OnThemeChange();

  // syncer::SyncableService implementation.
  virtual syncer::SyncMergeResult MergeDataAndStartSyncing(
      syncer::ModelType type,
      const syncer::SyncDataList& initial_sync_data,
      scoped_ptr<syncer::SyncChangeProcessor> sync_processor,
      scoped_ptr<syncer::SyncErrorFactory> error_handler) OVERRIDE;
  virtual void StopSyncing(syncer::ModelType type) OVERRIDE;
  virtual syncer::SyncDataList GetAllSyncData(
      syncer::ModelType type) const OVERRIDE;
  virtual syncer::SyncError ProcessSyncChanges(
      const tracked_objects::Location& from_here,
      const syncer::SyncChangeList& change_list) OVERRIDE;

  // Client tag and tile of theme node in sync.
  static const char kCurrentThemeClientTag[];
  static const char kCurrentThemeNodeTitle[];

 private:
  static bool AreThemeSpecificsEqual(
      const sync_pb::ThemeSpecifics& a,
      const sync_pb::ThemeSpecifics& b,
      bool is_system_theme_distinct_from_default_theme);

  // Set theme from theme specifics in |sync_data| using
  // SetCurrentThemeFromThemeSpecifics() if it's different from |current_specs|.
  void MaybeSetTheme(const sync_pb::ThemeSpecifics& current_specs,
                     const syncer::SyncData& sync_data);

  void SetCurrentThemeFromThemeSpecifics(
      const sync_pb::ThemeSpecifics& theme_specifics);

  // If the current theme is syncable, fills in the passed |theme_specifics|
  // structure based on the currently applied theme and returns |true|.
  // Otherwise returns |false|.
  bool GetThemeSpecificsFromCurrentTheme(
      sync_pb::ThemeSpecifics* theme_specifics) const;

  // Updates theme specifics in sync to |theme_specifics|.
  syncer::SyncError ProcessNewTheme(
      syncer::SyncChange::SyncChangeType change_type,
      const sync_pb::ThemeSpecifics& theme_specifics);

  Profile* const profile_;
  ThemeService* const theme_service_;

  scoped_ptr<syncer::SyncChangeProcessor> sync_processor_;
  scoped_ptr<syncer::SyncErrorFactory> sync_error_handler_;

  // Persist use_system_theme_by_default for platforms that use it, even if
  // we're not on one.
  bool use_system_theme_by_default_;

  base::ThreadChecker thread_checker_;

  FRIEND_TEST_ALL_PREFIXES(ThemeSyncableServiceTest, AreThemeSpecificsEqual);

  DISALLOW_COPY_AND_ASSIGN(ThemeSyncableService);
};

#endif  // CHROME_BROWSER_THEMES_THEME_SYNCABLE_SERVICE_H_
