// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class defines tests that implementations of InvalidationService should
// pass in order to be conformant.  Here's how you use it to test your
// implementation.
//
// Say your class is called MyInvalidationService.  Then you need to define a
// class called MyInvalidationServiceTestDelegate in
// my_invalidation_frontend_unittest.cc like this:
//
//   class MyInvalidationServiceTestDelegate {
//    public:
//     MyInvalidationServiceTestDelegate() ...
//
//     ~MyInvalidationServiceTestDelegate() {
//       // DestroyInvalidator() may not be explicitly called by tests.
//       DestroyInvalidator();
//     }
//
//     // Create the InvalidationService implementation with the given params.
//     void CreateInvalidationService() {
//       ...
//     }
//
//     // Should return the InvalidationService implementation.  Only called
//     // after CreateInvalidator and before DestroyInvalidator.
//     MyInvalidationService* GetInvalidationService() {
//       ...
//     }
//
//     // Destroy the InvalidationService implementation.
//     void DestroyInvalidationService() {
//       ...
//     }
//
//     // The Trigger* functions below should block until the effects of
//     // the call are visible on the current thread.
//
//     // Should cause OnInvalidatorStateChange() to be called on all
//     // observers of the InvalidationService implementation with the given
//     // parameters.
//     void TriggerOnInvalidatorStateChange(InvalidatorState state) {
//       ...
//     }
//
//     // Should cause OnIncomingInvalidation() to be called on all
//     // observers of the InvalidationService implementation with the given
//     // parameters.
//     void TriggerOnIncomingInvalidation(
//         const ObjectIdInvalidationMap& invalidation_map) {
//       ...
//     }
//   };
//
// The InvalidationServiceTest test harness will have a member variable of
// this delegate type and will call its functions in the various
// tests.
//
// Then you simply #include this file as well as gtest.h and add the
// following statement to my_sync_notifier_unittest.cc:
//
//   INSTANTIATE_TYPED_TEST_CASE_P(
//       MyInvalidationService,
//       InvalidationServiceTest,
//       MyInvalidatorTestDelegate);
//
// Easy!

#ifndef CHROME_BROWSER_INVALIDATION_INVALIDATION_SERVICE_TEST_TEMPLATE_H_
#define CHROME_BROWSER_INVALIDATION_INVALIDATION_SERVICE_TEST_TEMPLATE_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/invalidation/invalidation_service.h"
#include "google/cacheinvalidation/include/types.h"
#include "google/cacheinvalidation/types.pb.h"
#include "sync/notifier/fake_invalidation_handler.h"
#include "sync/notifier/object_id_invalidation_map.h"
#include "sync/notifier/object_id_invalidation_map_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

template <typename InvalidatorTestDelegate>
class InvalidationServiceTest : public testing::Test {
 protected:
  InvalidationServiceTest()
      : id1(ipc::invalidation::ObjectSource::CHROME_SYNC, "BOOKMARK"),
        id2(ipc::invalidation::ObjectSource::CHROME_SYNC, "PREFERENCE"),
        id3(ipc::invalidation::ObjectSource::CHROME_SYNC, "AUTOFILL"),
        id4(ipc::invalidation::ObjectSource::CHROME_PUSH_MESSAGING,
            "PUSH_MESSAGE") {
  }

  invalidation::InvalidationService*
  CreateAndInitializeInvalidationService() {
    this->delegate_.CreateInvalidationService();
    return this->delegate_.GetInvalidationService();
  }

  InvalidatorTestDelegate delegate_;

  const invalidation::ObjectId id1;
  const invalidation::ObjectId id2;
  const invalidation::ObjectId id3;
  const invalidation::ObjectId id4;
};

TYPED_TEST_CASE_P(InvalidationServiceTest);

// Initialize the invalidator, register a handler, register some IDs for that
// handler, and then unregister the handler, dispatching invalidations in
// between.  The handler should only see invalidations when its registered and
// its IDs are registered.
TYPED_TEST_P(InvalidationServiceTest, Basic) {
  invalidation::InvalidationService* const invalidator =
      this->CreateAndInitializeInvalidationService();

  syncer::FakeInvalidationHandler handler;

  invalidator->RegisterInvalidationHandler(&handler);

  syncer::ObjectIdInvalidationMap states;
  states[this->id1].payload = "1";
  states[this->id2].payload = "2";
  states[this->id3].payload = "3";

  // Should be ignored since no IDs are registered to |handler|.
  this->delegate_.TriggerOnIncomingInvalidation(states);
  EXPECT_EQ(0, handler.GetInvalidationCount());

  syncer::ObjectIdSet ids;
  ids.insert(this->id1);
  ids.insert(this->id2);
  invalidator->UpdateRegisteredInvalidationIds(&handler, ids);

  this->delegate_.TriggerOnInvalidatorStateChange(
      syncer::INVALIDATIONS_ENABLED);
  EXPECT_EQ(syncer::INVALIDATIONS_ENABLED, handler.GetInvalidatorState());

  syncer::ObjectIdInvalidationMap expected_states;
  expected_states[this->id1].payload = "1";
  expected_states[this->id2].payload = "2";

  this->delegate_.TriggerOnIncomingInvalidation(states);
  EXPECT_EQ(1, handler.GetInvalidationCount());
  EXPECT_THAT(expected_states, Eq(handler.GetLastInvalidationMap()));

  ids.erase(this->id1);
  ids.insert(this->id3);
  invalidator->UpdateRegisteredInvalidationIds(&handler, ids);

  expected_states.erase(this->id1);
  expected_states[this->id3].payload = "3";

  // Removed object IDs should not be notified, newly-added ones should.
  this->delegate_.TriggerOnIncomingInvalidation(states);
  EXPECT_EQ(2, handler.GetInvalidationCount());
  EXPECT_THAT(expected_states, Eq(handler.GetLastInvalidationMap()));

  this->delegate_.TriggerOnInvalidatorStateChange(
      syncer::TRANSIENT_INVALIDATION_ERROR);
  EXPECT_EQ(syncer::TRANSIENT_INVALIDATION_ERROR,
            handler.GetInvalidatorState());

  this->delegate_.TriggerOnInvalidatorStateChange(
      syncer::INVALIDATIONS_ENABLED);
  EXPECT_EQ(syncer::INVALIDATIONS_ENABLED,
            handler.GetInvalidatorState());

  invalidator->UnregisterInvalidationHandler(&handler);

  // Should be ignored since |handler| isn't registered anymore.
  this->delegate_.TriggerOnIncomingInvalidation(states);
  EXPECT_EQ(2, handler.GetInvalidationCount());
}

// Register handlers and some IDs for those handlers, register a handler with
// no IDs, and register a handler with some IDs but unregister it.  Then,
// dispatch some invalidations and invalidations.  Handlers that are registered
// should get invalidations, and the ones that have registered IDs should
// receive invalidations for those IDs.
TYPED_TEST_P(InvalidationServiceTest, MultipleHandlers) {
  invalidation::InvalidationService* const invalidator =
      this->CreateAndInitializeInvalidationService();

  syncer::FakeInvalidationHandler handler1;
  syncer::FakeInvalidationHandler handler2;
  syncer::FakeInvalidationHandler handler3;
  syncer::FakeInvalidationHandler handler4;

  invalidator->RegisterInvalidationHandler(&handler1);
  invalidator->RegisterInvalidationHandler(&handler2);
  invalidator->RegisterInvalidationHandler(&handler3);
  invalidator->RegisterInvalidationHandler(&handler4);

  {
    syncer::ObjectIdSet ids;
    ids.insert(this->id1);
    ids.insert(this->id2);
    invalidator->UpdateRegisteredInvalidationIds(&handler1, ids);
  }

  {
    syncer::ObjectIdSet ids;
    ids.insert(this->id3);
    invalidator->UpdateRegisteredInvalidationIds(&handler2, ids);
  }

  // Don't register any IDs for handler3.

  {
    syncer::ObjectIdSet ids;
    ids.insert(this->id4);
    invalidator->UpdateRegisteredInvalidationIds(&handler4, ids);
  }

  invalidator->UnregisterInvalidationHandler(&handler4);

  this->delegate_.TriggerOnInvalidatorStateChange(
      syncer::INVALIDATIONS_ENABLED);
  EXPECT_EQ(syncer::INVALIDATIONS_ENABLED, handler1.GetInvalidatorState());
  EXPECT_EQ(syncer::INVALIDATIONS_ENABLED, handler2.GetInvalidatorState());
  EXPECT_EQ(syncer::INVALIDATIONS_ENABLED, handler3.GetInvalidatorState());
  EXPECT_EQ(syncer::TRANSIENT_INVALIDATION_ERROR,
            handler4.GetInvalidatorState());

  {
    syncer::ObjectIdInvalidationMap states;
    states[this->id1].payload = "1";
    states[this->id2].payload = "2";
    states[this->id3].payload = "3";
    states[this->id4].payload = "4";
    this->delegate_.TriggerOnIncomingInvalidation(states);

    syncer::ObjectIdInvalidationMap expected_states;
    expected_states[this->id1].payload = "1";
    expected_states[this->id2].payload = "2";

    EXPECT_EQ(1, handler1.GetInvalidationCount());
    EXPECT_THAT(expected_states, Eq(handler1.GetLastInvalidationMap()));

    expected_states.clear();
    expected_states[this->id3].payload = "3";

    EXPECT_EQ(1, handler2.GetInvalidationCount());
    EXPECT_THAT(expected_states, Eq(handler2.GetLastInvalidationMap()));

    EXPECT_EQ(0, handler3.GetInvalidationCount());
    EXPECT_EQ(0, handler4.GetInvalidationCount());
  }

  this->delegate_.TriggerOnInvalidatorStateChange(
      syncer::TRANSIENT_INVALIDATION_ERROR);
  EXPECT_EQ(syncer::TRANSIENT_INVALIDATION_ERROR,
            handler1.GetInvalidatorState());
  EXPECT_EQ(syncer::TRANSIENT_INVALIDATION_ERROR,
            handler2.GetInvalidatorState());
  EXPECT_EQ(syncer::TRANSIENT_INVALIDATION_ERROR,
            handler3.GetInvalidatorState());
  EXPECT_EQ(syncer::TRANSIENT_INVALIDATION_ERROR,
            handler4.GetInvalidatorState());

  invalidator->UnregisterInvalidationHandler(&handler3);
  invalidator->UnregisterInvalidationHandler(&handler2);
  invalidator->UnregisterInvalidationHandler(&handler1);
}

// Make sure that passing an empty set to UpdateRegisteredInvalidationIds clears
// the corresponding entries for the handler.
TYPED_TEST_P(InvalidationServiceTest, EmptySetUnregisters) {
  invalidation::InvalidationService* const invalidator =
      this->CreateAndInitializeInvalidationService();

  syncer::FakeInvalidationHandler handler1;

  // Control observer.
  syncer::FakeInvalidationHandler handler2;

  invalidator->RegisterInvalidationHandler(&handler1);
  invalidator->RegisterInvalidationHandler(&handler2);

  {
    syncer::ObjectIdSet ids;
    ids.insert(this->id1);
    ids.insert(this->id2);
    invalidator->UpdateRegisteredInvalidationIds(&handler1, ids);
  }

  {
    syncer::ObjectIdSet ids;
    ids.insert(this->id3);
    invalidator->UpdateRegisteredInvalidationIds(&handler2, ids);
  }

  // Unregister the IDs for the first observer. It should not receive any
  // further invalidations.
  invalidator->UpdateRegisteredInvalidationIds(&handler1,
                                               syncer::ObjectIdSet());

  this->delegate_.TriggerOnInvalidatorStateChange(
      syncer::INVALIDATIONS_ENABLED);
  EXPECT_EQ(syncer::INVALIDATIONS_ENABLED, handler1.GetInvalidatorState());
  EXPECT_EQ(syncer::INVALIDATIONS_ENABLED, handler2.GetInvalidatorState());

  {
    syncer::ObjectIdInvalidationMap states;
    states[this->id1].payload = "1";
    states[this->id2].payload = "2";
    states[this->id3].payload = "3";
    this->delegate_.TriggerOnIncomingInvalidation(states);
    EXPECT_EQ(0, handler1.GetInvalidationCount());
    EXPECT_EQ(1, handler2.GetInvalidationCount());
  }

  this->delegate_.TriggerOnInvalidatorStateChange(
      syncer::TRANSIENT_INVALIDATION_ERROR);
  EXPECT_EQ(syncer::TRANSIENT_INVALIDATION_ERROR,
            handler1.GetInvalidatorState());
  EXPECT_EQ(syncer::TRANSIENT_INVALIDATION_ERROR,
            handler2.GetInvalidatorState());

  invalidator->UnregisterInvalidationHandler(&handler2);
  invalidator->UnregisterInvalidationHandler(&handler1);
}

namespace internal {

// A FakeInvalidationHandler that is "bound" to a specific
// InvalidationService.  This is for cross-referencing state information with
// the bound InvalidationService.
class BoundFakeInvalidationHandler : public syncer::FakeInvalidationHandler {
 public:
  explicit BoundFakeInvalidationHandler(
      const invalidation::InvalidationService& invalidator);
  virtual ~BoundFakeInvalidationHandler();

  // Returns the last return value of GetInvalidatorState() on the
  // bound invalidator from the last time the invalidator state
  // changed.
  syncer::InvalidatorState GetLastRetrievedState() const;

  // InvalidationHandler implementation.
  virtual void OnInvalidatorStateChange(
      syncer::InvalidatorState state) OVERRIDE;

 private:
  const invalidation::InvalidationService& invalidator_;
  syncer::InvalidatorState last_retrieved_state_;

  DISALLOW_COPY_AND_ASSIGN(BoundFakeInvalidationHandler);
};

}  // namespace internal

TYPED_TEST_P(InvalidationServiceTest, GetInvalidatorStateAlwaysCurrent) {
  invalidation::InvalidationService* const invalidator =
      this->CreateAndInitializeInvalidationService();

  internal::BoundFakeInvalidationHandler handler(*invalidator);
  invalidator->RegisterInvalidationHandler(&handler);

  this->delegate_.TriggerOnInvalidatorStateChange(
      syncer::INVALIDATIONS_ENABLED);
  EXPECT_EQ(syncer::INVALIDATIONS_ENABLED, handler.GetInvalidatorState());
  EXPECT_EQ(syncer::INVALIDATIONS_ENABLED, handler.GetLastRetrievedState());

  this->delegate_.TriggerOnInvalidatorStateChange(
      syncer::TRANSIENT_INVALIDATION_ERROR);
  EXPECT_EQ(syncer::TRANSIENT_INVALIDATION_ERROR,
            handler.GetInvalidatorState());
  EXPECT_EQ(syncer::TRANSIENT_INVALIDATION_ERROR,
            handler.GetLastRetrievedState());

  invalidator->UnregisterInvalidationHandler(&handler);
}

REGISTER_TYPED_TEST_CASE_P(InvalidationServiceTest,
                           Basic, MultipleHandlers, EmptySetUnregisters,
                           GetInvalidatorStateAlwaysCurrent);

#endif  // CHROME_BROWSER_INVALIDATION_INVALIDATION_SERVICE_TEST_TEMPLATE_H_
