// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/scheduler/scheduler_state_machine.h"

#include "cc/scheduler/scheduler.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cc {

namespace {

const SchedulerStateMachine::CommitState all_commit_states[] = {
  SchedulerStateMachine::COMMIT_STATE_IDLE,
  SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
  SchedulerStateMachine::COMMIT_STATE_READY_TO_COMMIT,
  SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_DRAW
};

// Exposes the protected state fields of the SchedulerStateMachine for testing
class StateMachine : public SchedulerStateMachine {
 public:
  explicit StateMachine(const SchedulerSettings& scheduler_settings)
      : SchedulerStateMachine(scheduler_settings) {}
  void SetCommitState(CommitState cs) { commit_state_ = cs; }
  CommitState CommitState() const { return commit_state_; }

  bool NeedsCommit() const { return needs_commit_; }

  void SetNeedsRedraw(bool b) { needs_redraw_ = b; }
  bool NeedsRedraw() const { return needs_redraw_; }

  void SetNeedsForcedRedraw(bool b) { needs_forced_redraw_ = b; }
  bool NeedsForcedRedraw() const { return needs_forced_redraw_; }

  bool CanDraw() const { return can_draw_; }
  bool Visible() const { return visible_; }
};

TEST(SchedulerStateMachineTest, TestNextActionBeginsMainFrameIfNeeded) {
  SchedulerSettings default_scheduler_settings;

  // If no commit needed, do nothing.
  {
    StateMachine state(default_scheduler_settings);
    state.SetCanStart();
    state.UpdateState(state.NextAction());
    state.DidCreateAndInitializeOutputSurface();
    state.SetCommitState(SchedulerStateMachine::COMMIT_STATE_IDLE);
    state.SetNeedsRedraw(false);
    state.SetVisible(true);

    EXPECT_FALSE(state.BeginFrameNeededToDrawByImplThread());

    state.DidLeaveBeginFrame();
    EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
    EXPECT_FALSE(state.BeginFrameNeededToDrawByImplThread());
    state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
    EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  }

  // If commit requested but can_start is still false, do nothing.
  {
    StateMachine state(default_scheduler_settings);
    state.SetCommitState(SchedulerStateMachine::COMMIT_STATE_IDLE);
    state.SetNeedsRedraw(false);
    state.SetVisible(true);

    EXPECT_FALSE(state.BeginFrameNeededToDrawByImplThread());

    state.DidLeaveBeginFrame();
    EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
    EXPECT_FALSE(state.BeginFrameNeededToDrawByImplThread());
    state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
    EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  }

  // If commit requested, begin a main frame.
  {
    StateMachine state(default_scheduler_settings);
    state.SetCommitState(SchedulerStateMachine::COMMIT_STATE_IDLE);
    state.SetCanStart();
    state.SetNeedsRedraw(false);
    state.SetVisible(true);
    EXPECT_FALSE(state.BeginFrameNeededToDrawByImplThread());
  }

  // Begin the frame, make sure needs_commit and commit_state update correctly.
  {
    StateMachine state(default_scheduler_settings);
    state.SetCanStart();
    state.UpdateState(state.NextAction());
    state.DidCreateAndInitializeOutputSurface();
    state.SetVisible(true);
    state.UpdateState(
        SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD);
    EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
              state.CommitState());
    EXPECT_FALSE(state.NeedsCommit());
    EXPECT_FALSE(state.BeginFrameNeededToDrawByImplThread());
  }
}

TEST(SchedulerStateMachineTest, TestSetForcedRedrawDoesNotSetsNormalRedraw) {
  SchedulerSettings default_scheduler_settings;
  SchedulerStateMachine state(default_scheduler_settings);
  state.SetCanDraw(true);
  state.SetNeedsForcedRedraw();
  EXPECT_FALSE(state.RedrawPending());
  EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
}

TEST(SchedulerStateMachineTest,
     TestFailedDrawSetsNeedsCommitAndDoesNotDrawAgain) {
  SchedulerSettings default_scheduler_settings;
  SchedulerStateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);
  state.SetNeedsRedraw();
  EXPECT_TRUE(state.RedrawPending());
  EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());

  // We're drawing now.
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE);
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  EXPECT_FALSE(state.RedrawPending());
  EXPECT_FALSE(state.CommitPending());

  // Failing the draw makes us require a commit.
  state.DidDrawIfPossibleCompleted(false);
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
      state.NextAction());
  state.UpdateState(
      SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD);
  EXPECT_TRUE(state.RedrawPending());
  EXPECT_TRUE(state.CommitPending());
}

TEST(SchedulerStateMachineTest,
     TestsetNeedsRedrawDuringFailedDrawDoesNotRemoveNeedsRedraw) {
  SchedulerSettings default_scheduler_settings;
  SchedulerStateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();

  state.SetVisible(true);
  state.SetCanDraw(true);
  state.SetNeedsRedraw();
  EXPECT_TRUE(state.RedrawPending());
  EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());

  // We're drawing now.
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE);
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  EXPECT_FALSE(state.RedrawPending());
  EXPECT_FALSE(state.CommitPending());

  // While still in the same begin frame callback on the main thread,
  // set needs redraw again. This should not redraw.
  state.SetNeedsRedraw();
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Failing the draw makes us require a commit.
  state.DidDrawIfPossibleCompleted(false);
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
  EXPECT_TRUE(state.RedrawPending());
}

TEST(SchedulerStateMachineTest,
     TestCommitAfterFailedDrawAllowsDrawInSameFrame) {
  SchedulerSettings default_scheduler_settings;
  SchedulerStateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Start a commit.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
  state.UpdateState(
      SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD);
  EXPECT_TRUE(state.CommitPending());

  // Then initiate a draw.
  state.SetNeedsRedraw();
  EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  EXPECT_TRUE(state.RedrawPending());

  // Fail the draw.
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE);
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  state.DidDrawIfPossibleCompleted(false);
  EXPECT_TRUE(state.RedrawPending());
  // But the commit is ongoing.
  EXPECT_TRUE(state.CommitPending());

  // Finish the commit.
  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  state.UpdateState(SchedulerStateMachine::ACTION_COMMIT);
  EXPECT_TRUE(state.RedrawPending());

  // And we should be allowed to draw again.
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
}

TEST(SchedulerStateMachineTest,
     TestCommitAfterFailedAndSuccessfulDrawDoesNotAllowDrawInSameFrame) {
  SchedulerSettings default_scheduler_settings;
  SchedulerStateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Start a commit.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
  state.UpdateState(
      SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD);
  EXPECT_TRUE(state.CommitPending());

  // Then initiate a draw.
  state.SetNeedsRedraw();
  EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  EXPECT_TRUE(state.RedrawPending());

  // Fail the draw.
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE);
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  state.DidDrawIfPossibleCompleted(false);
  EXPECT_TRUE(state.RedrawPending());
  // But the commit is ongoing.
  EXPECT_TRUE(state.CommitPending());

  // Force a draw.
  state.SetNeedsForcedRedraw();
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_FORCED, state.NextAction());

  // Do the forced draw.
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_FORCED);
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  EXPECT_FALSE(state.RedrawPending());
  // And the commit is still ongoing.
  EXPECT_TRUE(state.CommitPending());

  // Finish the commit.
  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  state.UpdateState(SchedulerStateMachine::ACTION_COMMIT);
  EXPECT_TRUE(state.RedrawPending());

  // And we should not be allowed to draw again in the same frame..
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
}

TEST(SchedulerStateMachineTest,
     TestFailedDrawsWillEventuallyForceADrawAfterTheNextCommit) {
  SchedulerSettings default_scheduler_settings;
  SchedulerStateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);
  state.SetMaximumNumberOfFailedDrawsBeforeDrawIsForced(1);

  // Start a commit.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
  state.UpdateState(
      SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD);
  EXPECT_TRUE(state.CommitPending());

  // Then initiate a draw.
  state.SetNeedsRedraw();
  EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  EXPECT_TRUE(state.RedrawPending());

  // Fail the draw.
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE);
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  state.DidDrawIfPossibleCompleted(false);
  EXPECT_TRUE(state.RedrawPending());
  // But the commit is ongoing.
  EXPECT_TRUE(state.CommitPending());

  // Finish the commit. Note, we should not yet be forcing a draw, but should
  // continue the commit as usual.
  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  state.UpdateState(SchedulerStateMachine::ACTION_COMMIT);
  EXPECT_TRUE(state.RedrawPending());

  // The redraw should be forced in this case.
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_FORCED, state.NextAction());
}

TEST(SchedulerStateMachineTest,
    TestFailedDrawIsRetriedInNextBeginFrameForImplThread) {
  SchedulerSettings default_scheduler_settings;
  SchedulerStateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Start a draw.
  state.SetNeedsRedraw();
  EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  EXPECT_TRUE(state.RedrawPending());

  // Fail the draw.
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE);
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  state.DidDrawIfPossibleCompleted(false);
  EXPECT_TRUE(state.RedrawPending());

  // We should not be trying to draw again now, but we have a commit pending.
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());

  state.DidLeaveBeginFrame();
  EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());

  // We should try to draw again in the next begin frame on the impl thread.
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
}

TEST(SchedulerStateMachineTest, TestDoestDrawTwiceInSameFrame) {
  SchedulerSettings default_scheduler_settings;
  SchedulerStateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);
  state.SetNeedsRedraw();
  EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE);

  // While still in the same begin frame for the impl thread, set needs redraw
  // again. This should not redraw.
  state.SetNeedsRedraw();
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Move to another frame. This should now draw.
  state.DidDrawIfPossibleCompleted(true);
  state.DidLeaveBeginFrame();
  EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());

  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE);
  state.DidDrawIfPossibleCompleted(true);
  EXPECT_FALSE(state.BeginFrameNeededToDrawByImplThread());
}

TEST(SchedulerStateMachineTest, TestNextActionDrawsOnBeginFrame) {
  SchedulerSettings default_scheduler_settings;

  // When not in BeginFrame, or in BeginFrame but not visible,
  // don't draw.
  size_t num_commit_states =
      sizeof(all_commit_states) / sizeof(SchedulerStateMachine::CommitState);
  for (size_t i = 0; i < num_commit_states; ++i) {
    for (size_t j = 0; j < 2; ++j) {
      StateMachine state(default_scheduler_settings);
      state.SetCanStart();
      state.UpdateState(state.NextAction());
      state.DidCreateAndInitializeOutputSurface();
      state.SetCommitState(all_commit_states[i]);
      bool visible = j;
      if (!visible) {
        state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
        state.SetVisible(false);
      } else {
        state.SetVisible(true);
      }

      // Case 1: needs_commit=false
      EXPECT_NE(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE,
                state.NextAction());

      // Case 2: needs_commit=true
      state.SetNeedsCommit();
      EXPECT_NE(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE,
                state.NextAction());
    }
  }

  // When in BeginFrame, or not in BeginFrame but needs_forced_dedraw
  // set, should always draw except if you're ready to commit, in which case
  // commit.
  for (size_t i = 0; i < num_commit_states; ++i) {
    for (size_t j = 0; j < 2; ++j) {
      StateMachine state(default_scheduler_settings);
      state.SetCanStart();
      state.UpdateState(state.NextAction());
      state.DidCreateAndInitializeOutputSurface();
      state.SetCanDraw(true);
      state.SetCommitState(all_commit_states[i]);
      bool forced_draw = j;
      if (!forced_draw) {
        state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
        state.SetNeedsRedraw(true);
        state.SetVisible(true);
      } else {
        state.SetNeedsForcedRedraw(true);
      }

      SchedulerStateMachine::Action expected_action;
      if (all_commit_states[i] !=
          SchedulerStateMachine::COMMIT_STATE_READY_TO_COMMIT) {
        expected_action =
            forced_draw ? SchedulerStateMachine::ACTION_DRAW_FORCED
                        : SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE;
      } else {
        expected_action = SchedulerStateMachine::ACTION_COMMIT;
      }

      // Case 1: needs_commit=false.
      EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
      EXPECT_EQ(expected_action, state.NextAction());

      // Case 2: needs_commit=true.
      state.SetNeedsCommit();
      EXPECT_TRUE(state.BeginFrameNeededToDrawByImplThread());
      EXPECT_EQ(expected_action, state.NextAction());
    }
  }
}

TEST(SchedulerStateMachineTest, TestNoCommitStatesRedrawWhenInvisible) {
  SchedulerSettings default_scheduler_settings;

  size_t num_commit_states =
      sizeof(all_commit_states) / sizeof(SchedulerStateMachine::CommitState);
  for (size_t i = 0; i < num_commit_states; ++i) {
    // There shouldn't be any drawing regardless of BeginFrame.
    for (size_t j = 0; j < 2; ++j) {
      StateMachine state(default_scheduler_settings);
      state.SetCanStart();
      state.UpdateState(state.NextAction());
      state.DidCreateAndInitializeOutputSurface();
      state.SetCommitState(all_commit_states[i]);
      state.SetVisible(false);
      state.SetNeedsRedraw(true);
      state.SetNeedsForcedRedraw(false);
      if (j == 1)
        state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());

      // Case 1: needs_commit=false.
      EXPECT_NE(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE,
                state.NextAction());

      // Case 2: needs_commit=true.
      state.SetNeedsCommit();
      EXPECT_NE(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE,
                state.NextAction());
    }
  }
}

TEST(SchedulerStateMachineTest, TestCanRedraw_StopsDraw) {
  SchedulerSettings default_scheduler_settings;

  size_t num_commit_states =
      sizeof(all_commit_states) / sizeof(SchedulerStateMachine::CommitState);
  for (size_t i = 0; i < num_commit_states; ++i) {
    // There shouldn't be any drawing regardless of BeginFrame.
    for (size_t j = 0; j < 2; ++j) {
      StateMachine state(default_scheduler_settings);
      state.SetCanStart();
      state.UpdateState(state.NextAction());
      state.DidCreateAndInitializeOutputSurface();
      state.SetCommitState(all_commit_states[i]);
      state.SetVisible(false);
      state.SetNeedsRedraw(true);
      state.SetNeedsForcedRedraw(false);
      if (j == 1)
        state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());

      state.SetCanDraw(false);
      EXPECT_NE(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE,
                state.NextAction());
    }
  }
}

TEST(SchedulerStateMachineTest,
     TestCanRedrawWithWaitingForFirstDrawMakesProgress) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetCommitState(
      SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_DRAW);
  state.SetNeedsCommit();
  state.SetNeedsRedraw(true);
  state.SetVisible(true);
  state.SetCanDraw(false);
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
}

TEST(SchedulerStateMachineTest, TestsetNeedsCommitIsNotLost) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetNeedsCommit();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Begin the frame.
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
  state.UpdateState(state.NextAction());
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
            state.CommitState());

  // Now, while the frame is in progress, set another commit.
  state.SetNeedsCommit();
  EXPECT_TRUE(state.NeedsCommit());

  // Let the frame finish.
  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_READY_TO_COMMIT,
            state.CommitState());

  // Expect to commit regardless of BeginFrame state.
  state.DidLeaveBeginFrame();
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());

  // Commit and make sure we draw on next BeginFrame
  state.UpdateState(SchedulerStateMachine::ACTION_COMMIT);
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_DRAW,
            state.CommitState());
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE);
  state.DidDrawIfPossibleCompleted(true);

  // Verify that another commit will begin.
  state.DidLeaveBeginFrame();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
}

TEST(SchedulerStateMachineTest, TestFullCycle) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Start clean and set commit.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());

  // Begin the frame.
  state.UpdateState(
      SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD);
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
            state.CommitState());
  EXPECT_FALSE(state.NeedsCommit());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Tell the scheduler the frame finished.
  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_READY_TO_COMMIT,
            state.CommitState());
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());

  // Commit.
  state.UpdateState(SchedulerStateMachine::ACTION_COMMIT);
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_DRAW,
            state.CommitState());
  EXPECT_TRUE(state.NeedsRedraw());

  // Expect to do nothing until BeginFrame.
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // At BeginFrame, draw.
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE);
  state.DidDrawIfPossibleCompleted(true);
  state.DidLeaveBeginFrame();

  // Should be synchronized, no draw needed, no action needed.
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_IDLE, state.CommitState());
  EXPECT_FALSE(state.NeedsRedraw());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
}

TEST(SchedulerStateMachineTest, TestFullCycleWithCommitRequestInbetween) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Start clean and set commit.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());

  // Begin the frame.
  state.UpdateState(
      SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD);
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
            state.CommitState());
  EXPECT_FALSE(state.NeedsCommit());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Request another commit while the commit is in flight.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Tell the scheduler the frame finished.
  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_READY_TO_COMMIT,
            state.CommitState());
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());

  // Commit.
  state.UpdateState(SchedulerStateMachine::ACTION_COMMIT);
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_DRAW,
            state.CommitState());
  EXPECT_TRUE(state.NeedsRedraw());

  // Expect to do nothing until BeginFrame.
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // At BeginFrame, draw.
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.UpdateState(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE);
  state.DidDrawIfPossibleCompleted(true);
  state.DidLeaveBeginFrame();

  // Should be synchronized, no draw needed, no action needed.
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_IDLE, state.CommitState());
  EXPECT_FALSE(state.NeedsRedraw());
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
}

TEST(SchedulerStateMachineTest, TestRequestCommitInvisible) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
}

TEST(SchedulerStateMachineTest, TestGoesInvisibleBeforeFinishCommit) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Start clean and set commit.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());

  // Begin the frame while visible.
  state.UpdateState(
      SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD);
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
            state.CommitState());
  EXPECT_FALSE(state.NeedsCommit());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Become invisible and abort the main thread's begin frame.
  state.SetVisible(false);
  state.BeginFrameAbortedByMainThread(false);

  // We should now be back in the idle state as if we didn't start a frame at
  // all.
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_IDLE, state.CommitState());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Become visible again.
  state.SetVisible(true);

  // Although we have aborted on this frame and haven't cancelled the commit
  // (i.e. need another), don't send another begin frame yet.
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_IDLE, state.CommitState());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  EXPECT_TRUE(state.NeedsCommit());

  // Start a new frame.
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());

  // Begin the frame.
  state.UpdateState(state.NextAction());

  // We should be starting the commit now.
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
            state.CommitState());
}

TEST(SchedulerStateMachineTest, AbortBeginFrameAndCancelCommit) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Get into a begin frame / commit state.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
  state.UpdateState(
      SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD);
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
            state.CommitState());
  EXPECT_FALSE(state.NeedsCommit());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Abort the commit, cancelling future commits.
  state.BeginFrameAbortedByMainThread(true);

  // Verify that another commit doesn't start on the same frame.
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_IDLE, state.CommitState());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  EXPECT_FALSE(state.NeedsCommit());

  // Start a new frame; draw because this is the first frame since output
  // surface init'd.
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.DidLeaveBeginFrame();

  // Verify another commit doesn't start on another frame either.
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_IDLE, state.CommitState());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  EXPECT_FALSE(state.NeedsCommit());

  // Verify another commit can start if requested, though.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_IDLE, state.CommitState());
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
}

TEST(SchedulerStateMachineTest, TestFirstContextCreation) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.SetVisible(true);
  state.SetCanDraw(true);

  EXPECT_EQ(SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION,
            state.NextAction());
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Check that the first init does not SetNeedsCommit.
  state.SetNeedsCommit();
  EXPECT_NE(SchedulerStateMachine::ACTION_NONE, state.NextAction());
}

TEST(SchedulerStateMachineTest, TestContextLostWhenCompletelyIdle) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();

  state.SetVisible(true);
  state.SetCanDraw(true);

  EXPECT_NE(SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION,
            state.NextAction());
  state.DidLoseOutputSurface();

  EXPECT_EQ(SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION,
            state.NextAction());
  state.UpdateState(state.NextAction());

  // Once context recreation begins, nothing should happen.
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Recreate the context.
  state.DidCreateAndInitializeOutputSurface();

  // When the context is recreated, we should begin a commit.
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
  state.UpdateState(state.NextAction());
}

TEST(SchedulerStateMachineTest,
     TestContextLostWhenIdleAndCommitRequestedWhileRecreating) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  EXPECT_NE(SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION,
            state.NextAction());
  state.DidLoseOutputSurface();

  EXPECT_EQ(SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION,
            state.NextAction());
  state.UpdateState(state.NextAction());

  // Once context recreation begins, nothing should happen.
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // While context is recreating, commits shouldn't begin.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Recreate the context
  state.DidCreateAndInitializeOutputSurface();
  EXPECT_FALSE(state.RedrawPending());

  // When the context is recreated, we should begin a commit
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
  state.UpdateState(state.NextAction());
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
            state.CommitState());
  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  state.UpdateState(state.NextAction());
  // Finishing the first commit after initializing an output surface should
  // automatically cause a redraw.
  EXPECT_TRUE(state.RedrawPending());

  // Once the context is recreated, whether we draw should be based on
  // SetCanDraw.
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.SetCanDraw(false);
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  state.SetCanDraw(true);
  state.DidLeaveBeginFrame();
}

TEST(SchedulerStateMachineTest, TestContextLostWhileCommitInProgress) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Get a commit in flight.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
  state.UpdateState(state.NextAction());

  // Set damage and expect a draw.
  state.SetNeedsRedraw(true);
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.UpdateState(state.NextAction());
  state.DidLeaveBeginFrame();

  // Cause a lost context while the begin frame is in flight
  // for the main thread.
  state.DidLoseOutputSurface();

  // Ask for another draw. Expect nothing happens.
  state.SetNeedsRedraw(true);
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Finish the frame, and commit.
  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  state.UpdateState(state.NextAction());

  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_DRAW,
            state.CommitState());

  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.UpdateState(state.NextAction());

  // Expect to be told to begin context recreation, independent of
  // BeginFrame state.
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION,
            state.NextAction());
  state.DidLeaveBeginFrame();
  EXPECT_EQ(SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION,
            state.NextAction());
}

TEST(SchedulerStateMachineTest,
     TestContextLostWhileCommitInProgressAndAnotherCommitRequested) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Get a commit in flight.
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
  state.UpdateState(state.NextAction());

  // Set damage and expect a draw.
  state.SetNeedsRedraw(true);
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.UpdateState(state.NextAction());
  state.DidLeaveBeginFrame();

  // Cause a lost context while the begin frame is in flight
  // for the main thread.
  state.DidLoseOutputSurface();

  // Ask for another draw and also set needs commit. Expect nothing happens.
  state.SetNeedsRedraw(true);
  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  // Finish the frame, and commit.
  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  state.UpdateState(state.NextAction());

  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_DRAW,
            state.CommitState());

  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_IF_POSSIBLE, state.NextAction());
  state.UpdateState(state.NextAction());

  // Expect to be told to begin context recreation, independent of
  // BeginFrame state
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION,
            state.NextAction());
  state.DidLeaveBeginFrame();
  EXPECT_EQ(SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION,
            state.NextAction());
}

TEST(SchedulerStateMachineTest, TestFinishAllRenderingWhileContextLost) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Cause a lost context lost.
  state.DidLoseOutputSurface();

  // Ask a forced redraw and verify it ocurrs.
  state.SetNeedsForcedRedraw(true);
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_FORCED, state.NextAction());
  state.DidLeaveBeginFrame();

  // Clear the forced redraw bit.
  state.SetNeedsForcedRedraw(false);

  // Expect to be told to begin context recreation, independent of
  // BeginFrame state
  EXPECT_EQ(SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION,
            state.NextAction());
  state.UpdateState(state.NextAction());

  // Ask a forced redraw and verify it ocurrs.
  state.SetNeedsForcedRedraw(true);
  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_FORCED, state.NextAction());
  state.DidLeaveBeginFrame();
}

TEST(SchedulerStateMachineTest, DontDrawBeforeCommitAfterLostOutputSurface) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  state.SetNeedsRedraw(true);

  // Cause a lost output surface, and restore it.
  state.DidLoseOutputSurface();
  EXPECT_EQ(SchedulerStateMachine::ACTION_BEGIN_OUTPUT_SURFACE_CREATION,
            state.NextAction());
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();

  EXPECT_FALSE(state.RedrawPending());
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
}

TEST(SchedulerStateMachineTest,
    TestSendBeginFrameToMainThreadWhenInvisibleAndForceCommit) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(false);
  state.SetNeedsCommit();
  state.SetNeedsForcedCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
}

TEST(SchedulerStateMachineTest,
    TestSendBeginFrameToMainThreadWhenCanStartFalseAndForceCommit) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetVisible(true);
  state.SetCanDraw(true);
  state.SetNeedsCommit();
  state.SetNeedsForcedCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
}

TEST(SchedulerStateMachineTest, TestFinishCommitWhenCommitInProgress) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(false);
  state.SetCommitState(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS);
  state.SetNeedsCommit();

  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  state.UpdateState(state.NextAction());

  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_DRAW,
            state.CommitState());

  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
}

TEST(SchedulerStateMachineTest, TestFinishCommitWhenForcedCommitInProgress) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(false);
  state.SetCommitState(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS);
  state.SetNeedsCommit();
  state.SetNeedsForcedCommit();

  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  state.UpdateState(state.NextAction());

  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_FORCED_DRAW,
            state.CommitState());

  // If we are waiting for forced draw then we know a begin frame is already
  // in flight for the main thread.
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
}

TEST(SchedulerStateMachineTest, TestSendBeginFrameToMainThreadWhenContextLost) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);
  state.SetNeedsCommit();
  state.SetNeedsForcedCommit();
  state.DidLoseOutputSurface();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
}

TEST(SchedulerStateMachineTest, TestImmediateFinishCommit) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Schedule a forced frame, commit it, draw it.
  state.SetNeedsCommit();
  state.SetNeedsForcedCommit();
  state.UpdateState(state.NextAction());
  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_READY_TO_COMMIT,
            state.CommitState());
  state.UpdateState(state.NextAction());

  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_FORCED_DRAW,
            state.CommitState());

  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  state.SetNeedsForcedRedraw(true);
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_FORCED, state.NextAction());
  state.UpdateState(state.NextAction());
  state.DidDrawIfPossibleCompleted(true);
  state.DidLeaveBeginFrame();

  // Should be waiting for the normal begin frame from the main thread.
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
            state.CommitState());
}

TEST(SchedulerStateMachineTest, TestImmediateFinishCommitDuringCommit) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  // Start a normal commit.
  state.SetNeedsCommit();
  state.UpdateState(state.NextAction());

  // Schedule a forced frame, commit it, draw it.
  state.SetNeedsCommit();
  state.SetNeedsForcedCommit();
  state.UpdateState(state.NextAction());
  state.FinishCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_READY_TO_COMMIT,
            state.CommitState());
  state.UpdateState(state.NextAction());

  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_FORCED_DRAW,
            state.CommitState());

  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  state.SetNeedsForcedRedraw(true);
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_FORCED, state.NextAction());
  state.UpdateState(state.NextAction());
  state.DidDrawIfPossibleCompleted(true);
  state.DidLeaveBeginFrame();

  // Should be waiting for the normal begin frame from the main thread.
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
            state.CommitState()) << state.ToString();
}

TEST(SchedulerStateMachineTest,
    ImmediateBeginFrameAbortedByMainThreadWhileInvisible) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(true);

  state.SetNeedsCommit();
  state.UpdateState(state.NextAction());

  state.SetNeedsCommit();
  state.SetNeedsForcedCommit();
  state.UpdateState(state.NextAction());
  state.FinishCommit();

  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_READY_TO_COMMIT,
            state.CommitState());
  state.UpdateState(state.NextAction());

  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_FORCED_DRAW,
            state.CommitState());

  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  state.SetNeedsForcedRedraw(true);
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_FORCED, state.NextAction());
  state.UpdateState(state.NextAction());
  state.DidDrawIfPossibleCompleted(true);
  state.DidLeaveBeginFrame();

  // Should be waiting for the main thread's begin frame.
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_FRAME_IN_PROGRESS,
            state.CommitState()) << state.ToString();

  // Become invisible and abort the main thread's begin frame.
  state.SetVisible(false);
  state.BeginFrameAbortedByMainThread(false);

  // Should be back in the idle state, but needing a commit.
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_IDLE, state.CommitState());
  EXPECT_TRUE(state.NeedsCommit());
}

TEST(SchedulerStateMachineTest, ImmediateFinishCommitWhileCantDraw) {
  SchedulerSettings default_scheduler_settings;
  StateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetVisible(true);
  state.SetCanDraw(false);

  state.SetNeedsCommit();
  state.UpdateState(state.NextAction());

  state.SetNeedsCommit();
  state.SetNeedsForcedCommit();
  state.UpdateState(state.NextAction());
  state.FinishCommit();

  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());
  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_READY_TO_COMMIT,
            state.CommitState());
  state.UpdateState(state.NextAction());

  EXPECT_EQ(SchedulerStateMachine::COMMIT_STATE_WAITING_FOR_FIRST_FORCED_DRAW,
            state.CommitState());

  state.DidEnterBeginFrame(BeginFrameArgs::CreateForTesting());
  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  state.SetNeedsForcedRedraw(true);
  EXPECT_EQ(SchedulerStateMachine::ACTION_DRAW_FORCED, state.NextAction());
  state.UpdateState(state.NextAction());
  state.DidDrawIfPossibleCompleted(true);
  state.DidLeaveBeginFrame();
}

TEST(SchedulerStateMachineTest, ReportIfNotDrawing) {
  SchedulerSettings default_scheduler_settings;
  SchedulerStateMachine state(default_scheduler_settings);

  state.SetCanDraw(true);
  state.SetVisible(true);
  EXPECT_FALSE(state.DrawSuspendedUntilCommit());

  state.SetCanDraw(false);
  state.SetVisible(true);
  EXPECT_TRUE(state.DrawSuspendedUntilCommit());

  state.SetCanDraw(true);
  state.SetVisible(false);
  EXPECT_TRUE(state.DrawSuspendedUntilCommit());

  state.SetCanDraw(false);
  state.SetVisible(false);
  EXPECT_TRUE(state.DrawSuspendedUntilCommit());

  state.SetCanDraw(true);
  state.SetVisible(true);
  EXPECT_FALSE(state.DrawSuspendedUntilCommit());
}

TEST(SchedulerStateMachineTest, ReportIfNotDrawingFromAcquiredTextures) {
  SchedulerSettings default_scheduler_settings;
  SchedulerStateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetCanDraw(true);
  state.SetVisible(true);
  EXPECT_FALSE(state.DrawSuspendedUntilCommit());

  state.SetMainThreadNeedsLayerTextures();
  EXPECT_EQ(
      SchedulerStateMachine::ACTION_ACQUIRE_LAYER_TEXTURES_FOR_MAIN_THREAD,
      state.NextAction());
  state.UpdateState(state.NextAction());
  EXPECT_TRUE(state.DrawSuspendedUntilCommit());

  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());

  state.UpdateState(state.NextAction());
  EXPECT_TRUE(state.DrawSuspendedUntilCommit());

  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  state.FinishCommit();
  EXPECT_TRUE(state.DrawSuspendedUntilCommit());

  EXPECT_EQ(SchedulerStateMachine::ACTION_COMMIT, state.NextAction());

  state.UpdateState(state.NextAction());
  EXPECT_FALSE(state.DrawSuspendedUntilCommit());
}

TEST(SchedulerStateMachineTest, AcquireTexturesWithAbort) {
  SchedulerSettings default_scheduler_settings;
  SchedulerStateMachine state(default_scheduler_settings);
  state.SetCanStart();
  state.UpdateState(state.NextAction());
  state.DidCreateAndInitializeOutputSurface();
  state.SetCanDraw(true);
  state.SetVisible(true);

  state.SetMainThreadNeedsLayerTextures();
  EXPECT_EQ(
      SchedulerStateMachine::ACTION_ACQUIRE_LAYER_TEXTURES_FOR_MAIN_THREAD,
      state.NextAction());
  state.UpdateState(state.NextAction());
  EXPECT_TRUE(state.DrawSuspendedUntilCommit());

  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  state.SetNeedsCommit();
  EXPECT_EQ(SchedulerStateMachine::ACTION_SEND_BEGIN_FRAME_TO_MAIN_THREAD,
            state.NextAction());
  state.UpdateState(state.NextAction());
  EXPECT_TRUE(state.DrawSuspendedUntilCommit());

  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());

  state.BeginFrameAbortedByMainThread(true);

  EXPECT_EQ(SchedulerStateMachine::ACTION_NONE, state.NextAction());
  EXPECT_FALSE(state.DrawSuspendedUntilCommit());
}

}  // namespace
}  // namespace cc
