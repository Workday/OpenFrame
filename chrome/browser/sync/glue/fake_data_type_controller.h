// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_FAKE_DATA_TYPE_CONTROLLER_H__
#define CHROME_BROWSER_SYNC_GLUE_FAKE_DATA_TYPE_CONTROLLER_H__

#include "chrome/browser/sync/glue/data_type_controller.h"
#include "chrome/browser/sync/glue/data_type_manager.h"

namespace browser_sync {
// Fake DataTypeController implementation that simulates the state
// machine of a typical asynchronous data type.
//
// TODO(akalin): Consider using subclasses of
// {Frontend,NonFrontend,NewNonFrontend}DataTypeController instead, so
// that we don't have to update this class if we change the expected
// behavior of controllers. (It would be easier of the above classes
// used delegation instead of subclassing for per-data-type
// functionality.)
class FakeDataTypeController : public DataTypeController {
 public:
  explicit FakeDataTypeController(syncer::ModelType type);

  virtual void LoadModels(
      const ModelLoadCallback& model_load_callback) OVERRIDE;

  virtual void OnModelLoaded() OVERRIDE;

  virtual void StartAssociating(const StartCallback& start_callback) OVERRIDE;

  void FinishStart(StartResult result);

  virtual void Stop() OVERRIDE;

  virtual syncer::ModelType type() const OVERRIDE;

  virtual std::string name() const OVERRIDE;

  virtual syncer::ModelSafeGroup model_safe_group() const OVERRIDE;

  virtual State state() const OVERRIDE;

  virtual void OnSingleDatatypeUnrecoverableError(
      const tracked_objects::Location& from_here,
      const std::string& message) OVERRIDE;

  virtual void RecordUnrecoverableError(
      const tracked_objects::Location& from_here,
      const std::string& message) OVERRIDE;

  virtual void SetDelayModelLoad();

  virtual void SimulateModelLoadFinishing();

 protected:
  virtual ~FakeDataTypeController();

 private:
  DataTypeController::State state_;
  bool model_load_delayed_;
  syncer::ModelType type_;
  StartCallback last_start_callback_;
  ModelLoadCallback model_load_callback_;
};

}  // namespace browser_sync
#endif  // CHROME_BROWSER_SYNC_GLUE_FAKE_DATA_TYPE_CONTROLLER_H__
