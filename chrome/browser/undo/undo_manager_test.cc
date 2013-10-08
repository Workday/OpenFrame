// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/auto_reset.h"
#include "chrome/browser/undo/undo_manager.h"
#include "chrome/browser/undo/undo_operation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class TestUndoOperation;

// TestUndoService -------------------------------------------------------------

class TestUndoService {
 public:
  TestUndoService();
  ~TestUndoService();

  void Redo();
  void TriggerOperation();
  void RecordUndoCall();

  UndoManager undo_manager_;

  bool performing_redo_;

  int undo_operation_count_;
  int redo_operation_count_;
};

// TestUndoOperation -----------------------------------------------------------

class TestUndoOperation : public UndoOperation {
 public:
  explicit TestUndoOperation(TestUndoService* undo_service);
  virtual ~TestUndoOperation();

  // UndoOperation:
  virtual void Undo() OVERRIDE;

 private:
  TestUndoService* undo_service_;

  DISALLOW_COPY_AND_ASSIGN(TestUndoOperation);
};

TestUndoOperation::TestUndoOperation(TestUndoService* undo_service)
      : undo_service_(undo_service) {
}

TestUndoOperation::~TestUndoOperation() {
}

void TestUndoOperation::Undo() {
  undo_service_->TriggerOperation();
  undo_service_->RecordUndoCall();
}

// TestUndoService -------------------------------------------------------------

TestUndoService::TestUndoService() : performing_redo_(false),
                                     undo_operation_count_(0),
                                     redo_operation_count_(0) {
}

TestUndoService::~TestUndoService() {
}

void TestUndoService::Redo() {
  base::AutoReset<bool> incoming_changes(&performing_redo_, true);
  undo_manager_.Redo();
}

void TestUndoService::TriggerOperation() {
  scoped_ptr<TestUndoOperation> op(new TestUndoOperation(this));
  undo_manager_.AddUndoOperation(op.PassAs<UndoOperation>());
}

void TestUndoService::RecordUndoCall() {
  if (performing_redo_)
    ++redo_operation_count_;
  else
    ++undo_operation_count_;
}

// Tests -----------------------------------------------------------------------

TEST(UndoServiceTest, AddUndoActions) {
  TestUndoService undo_service;

  undo_service.TriggerOperation();
  undo_service.TriggerOperation();
  EXPECT_EQ(2U, undo_service.undo_manager_.undo_count());
  EXPECT_EQ(0U, undo_service.undo_manager_.redo_count());
}

TEST(UndoServiceTest, UndoMultipleActions) {
  TestUndoService undo_service;

  undo_service.TriggerOperation();
  undo_service.TriggerOperation();

  undo_service.undo_manager_.Undo();
  EXPECT_EQ(1U, undo_service.undo_manager_.undo_count());
  EXPECT_EQ(1U, undo_service.undo_manager_.redo_count());

  undo_service.undo_manager_.Undo();
  EXPECT_EQ(0U, undo_service.undo_manager_.undo_count());
  EXPECT_EQ(2U, undo_service.undo_manager_.redo_count());

  EXPECT_EQ(2, undo_service.undo_operation_count_);
  EXPECT_EQ(0, undo_service.redo_operation_count_);
}

TEST(UndoServiceTest, RedoAction) {
  TestUndoService undo_service;

  undo_service.TriggerOperation();

  undo_service.undo_manager_.Undo();
  EXPECT_EQ(0U, undo_service.undo_manager_.undo_count());
  EXPECT_EQ(1U, undo_service.undo_manager_.redo_count());

  undo_service.Redo();
  EXPECT_EQ(1U, undo_service.undo_manager_.undo_count());
  EXPECT_EQ(0U, undo_service.undo_manager_.redo_count());

  EXPECT_EQ(1, undo_service.undo_operation_count_);
  EXPECT_EQ(1, undo_service.redo_operation_count_);
}

TEST(UndoServiceTest, GroupActions) {
  TestUndoService undo_service;

  // Add two operations in a single action.
  undo_service.undo_manager_.StartGroupingActions();
  undo_service.TriggerOperation();
  undo_service.TriggerOperation();
  undo_service.undo_manager_.EndGroupingActions();

  // Check that only one action is created.
  EXPECT_EQ(1U, undo_service.undo_manager_.undo_count());
  EXPECT_EQ(0U, undo_service.undo_manager_.redo_count());

  undo_service.undo_manager_.Undo();
  EXPECT_EQ(0U, undo_service.undo_manager_.undo_count());
  EXPECT_EQ(1U, undo_service.undo_manager_.redo_count());

  undo_service.Redo();
  EXPECT_EQ(1U, undo_service.undo_manager_.undo_count());
  EXPECT_EQ(0U, undo_service.undo_manager_.redo_count());

  // Check that both operations were called in Undo and Redo.
  EXPECT_EQ(2, undo_service.undo_operation_count_);
  EXPECT_EQ(2, undo_service.redo_operation_count_);
}

TEST(UndoServiceTest, SuspendUndoTracking) {
  TestUndoService undo_service;

  undo_service.undo_manager_.SuspendUndoTracking();
  EXPECT_TRUE(undo_service.undo_manager_.IsUndoTrakingSuspended());

  undo_service.TriggerOperation();

  undo_service.undo_manager_.ResumeUndoTracking();
  EXPECT_FALSE(undo_service.undo_manager_.IsUndoTrakingSuspended());

  EXPECT_EQ(0U, undo_service.undo_manager_.undo_count());
  EXPECT_EQ(0U, undo_service.undo_manager_.redo_count());
}

TEST(UndoServiceTest, RedoEmptyAfterNewAction) {
  TestUndoService undo_service;

  undo_service.TriggerOperation();
  undo_service.undo_manager_.Undo();
  EXPECT_EQ(0U, undo_service.undo_manager_.undo_count());
  EXPECT_EQ(1U, undo_service.undo_manager_.redo_count());

  undo_service.TriggerOperation();
  EXPECT_EQ(1U, undo_service.undo_manager_.undo_count());
  EXPECT_EQ(0U, undo_service.undo_manager_.redo_count());
}

TEST(UndoServiceTest, GetAllUndoOperations) {
  TestUndoService undo_service;

  undo_service.TriggerOperation();

  undo_service.undo_manager_.StartGroupingActions();
  undo_service.TriggerOperation();
  undo_service.TriggerOperation();
  undo_service.undo_manager_.EndGroupingActions();

  undo_service.TriggerOperation();

  undo_service.undo_manager_.Undo();
  ASSERT_EQ(2U, undo_service.undo_manager_.undo_count());
  ASSERT_EQ(1U, undo_service.undo_manager_.redo_count());

  std::vector<UndoOperation*> all_operations =
      undo_service.undo_manager_.GetAllUndoOperations();
  EXPECT_EQ(4U, all_operations.size());
}

} // namespace
