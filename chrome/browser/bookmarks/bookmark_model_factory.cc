// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/bookmarks/bookmark_model_factory.h"

#include "base/deferred_sequenced_task_runner.h"
#include "base/memory/singleton.h"
#include "base/values.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/startup_task_runner_service.h"
#include "chrome/browser/profiles/startup_task_runner_service_factory.h"
#include "chrome/common/pref_names.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"
#include "components/user_prefs/pref_registry_syncable.h"

// static
BookmarkModel* BookmarkModelFactory::GetForProfile(Profile* profile) {
  return static_cast<BookmarkModel*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

BookmarkModel* BookmarkModelFactory::GetForProfileIfExists(Profile* profile) {
  return static_cast<BookmarkModel*>(
      GetInstance()->GetServiceForBrowserContext(profile, false));
}

// static
BookmarkModelFactory* BookmarkModelFactory::GetInstance() {
  return Singleton<BookmarkModelFactory>::get();
}

BookmarkModelFactory::BookmarkModelFactory()
    : BrowserContextKeyedServiceFactory(
        "BookmarkModel",
        BrowserContextDependencyManager::GetInstance()) {
}

BookmarkModelFactory::~BookmarkModelFactory() {}

BrowserContextKeyedService* BookmarkModelFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  Profile* profile = static_cast<Profile*>(context);
  BookmarkModel* bookmark_model = new BookmarkModel(profile);
  bookmark_model->Load(StartupTaskRunnerServiceFactory::GetForProfile(profile)->
      GetBookmarkTaskRunner());
  return bookmark_model;
}

void BookmarkModelFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  // Don't sync this, as otherwise, due to a limitation in sync, it
  // will cause a deadlock (see http://crbug.com/97955).  If we truly
  // want to sync the expanded state of folders, it should be part of
  // bookmark sync itself (i.e., a property of the sync folder nodes).
  registry->RegisterListPref(prefs::kBookmarkEditorExpandedNodes,
                             new base::ListValue,
                             user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

content::BrowserContext* BookmarkModelFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

bool BookmarkModelFactory::ServiceIsNULLWhileTesting() const {
  return true;
}
