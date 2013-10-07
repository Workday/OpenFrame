// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/autofill_data_type_controller.h"

#include "base/bind.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_components_factory.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/webdata/autocomplete_syncable_service.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#include "content/public/browser/browser_thread.h"
#include "sync/api/sync_error.h"
#include "sync/internal_api/public/util/experiments.h"

using content::BrowserThread;

namespace browser_sync {

AutofillDataTypeController::AutofillDataTypeController(
    ProfileSyncComponentsFactory* profile_sync_factory,
    Profile* profile,
    ProfileSyncService* sync_service)
    : NonUIDataTypeController(
        profile_sync_factory, profile, sync_service) {
}

syncer::ModelType AutofillDataTypeController::type() const {
  return syncer::AUTOFILL;
}

syncer::ModelSafeGroup AutofillDataTypeController::model_safe_group() const {
  return syncer::GROUP_DB;
}

void AutofillDataTypeController::WebDatabaseLoaded() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_EQ(MODEL_STARTING, state());

  OnModelLoaded();
}

AutofillDataTypeController::~AutofillDataTypeController() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

bool AutofillDataTypeController::PostTaskOnBackendThread(
    const tracked_objects::Location& from_here,
    const base::Closure& task) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return BrowserThread::PostTask(BrowserThread::DB, from_here, task);
}

bool AutofillDataTypeController::StartModels() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_EQ(MODEL_STARTING, state());

  autofill::AutofillWebDataService* web_data_service =
      autofill::AutofillWebDataService::FromBrowserContext(profile()).get();

  if (!web_data_service)
    return false;

  if (web_data_service->IsDatabaseLoaded()) {
    return true;
  } else {
    web_data_service->RegisterDBLoadedCallback(base::Bind(
        &AutofillDataTypeController::WebDatabaseLoaded, this));
    return false;
  }
}

void AutofillDataTypeController::StopModels() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(state() == STOPPING || state() == NOT_RUNNING || state() == DISABLED);
  DVLOG(1) << "AutofillDataTypeController::StopModels() : State = " << state();
}

void AutofillDataTypeController::StartAssociating(
    const StartCallback& start_callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_EQ(state(), MODEL_LOADED);
  ProfileSyncService* sync = ProfileSyncServiceFactory::GetForProfile(
      profile());
  DCHECK(sync);
  scoped_refptr<autofill::AutofillWebDataService> web_data_service =
      autofill::AutofillWebDataService::FromBrowserContext(profile());
  bool cull_expired_entries = sync->current_experiments().autofill_culling;
  // First, post the update task to the DB thread, which guarantees us it
  // would run before anything StartAssociating does (e.g.
  // MergeDataAndStartSyncing).
  PostTaskOnBackendThread(
      FROM_HERE,
      base::Bind(
          &AutofillDataTypeController::UpdateAutofillCullingSettings,
          this,
          cull_expired_entries,
          web_data_service));
  NonUIDataTypeController::StartAssociating(start_callback);
}

void AutofillDataTypeController::UpdateAutofillCullingSettings(
    bool cull_expired_entries,
    scoped_refptr<autofill::AutofillWebDataService> web_data_service) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::DB));
  AutocompleteSyncableService* service =
      AutocompleteSyncableService::FromWebDataService(web_data_service.get());
  if (!service) {
    DVLOG(1) << "Can't update culling, no AutocompleteSyncableService.";
    return;
  }

  service->UpdateCullSetting(cull_expired_entries);
}

}  // namespace browser_sync
