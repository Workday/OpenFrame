// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PREDICTORS_PREDICTOR_DATABASE_FACTORY_H_
#define CHROME_BROWSER_PREDICTORS_PREDICTOR_DATABASE_FACTORY_H_

#include "base/basictypes.h"
#include "base/memory/singleton.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

class Profile;

namespace predictors {

class PredictorDatabase;

// Singleton that owns the PredictorDatabases and associates them with
// Profiles.
class PredictorDatabaseFactory : public BrowserContextKeyedServiceFactory {
 public:
  static PredictorDatabase* GetForProfile(Profile* profile);

  static PredictorDatabaseFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<PredictorDatabaseFactory>;

  PredictorDatabaseFactory();
  virtual ~PredictorDatabaseFactory();

  // BrowserContextKeyedServiceFactory:
  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(PredictorDatabaseFactory);
};

}  // namespace predictors

#endif  // CHROME_BROWSER_PREDICTORS_PREDICTOR_DATABASE_FACTORY_H_
