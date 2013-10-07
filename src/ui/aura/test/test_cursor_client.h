// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_TEST_TEST_CURSOR_CLIENT_H_
#define UI_AURA_TEST_TEST_CURSOR_CLIENT_H_

#include "base/compiler_specific.h"
#include "base/observer_list.h"
#include "ui/aura/client/cursor_client.h"

namespace aura {
namespace test {

class TestCursorClient : public aura::client::CursorClient {
 public:
  explicit TestCursorClient(aura::Window* root_window);
  virtual ~TestCursorClient();

  // Overridden from aura::client::CursorClient:
  virtual void SetCursor(gfx::NativeCursor cursor) OVERRIDE;
  virtual void ShowCursor() OVERRIDE;
  virtual void HideCursor() OVERRIDE;
  virtual void SetScale(float scale) OVERRIDE;
  virtual bool IsCursorVisible() const OVERRIDE;
  virtual void EnableMouseEvents() OVERRIDE;
  virtual void DisableMouseEvents() OVERRIDE;
  virtual bool IsMouseEventsEnabled() const OVERRIDE;
  virtual void SetDisplay(const gfx::Display& display) OVERRIDE;
  virtual void LockCursor() OVERRIDE;
  virtual void UnlockCursor() OVERRIDE;
  virtual void AddObserver(
      aura::client::CursorClientObserver* observer) OVERRIDE;
  virtual void RemoveObserver(
      aura::client::CursorClientObserver* observer) OVERRIDE;

 private:
  bool visible_;
  bool mouse_events_enabled_;
  ObserverList<aura::client::CursorClientObserver> observers_;
  aura::Window* root_window_;

  DISALLOW_COPY_AND_ASSIGN(TestCursorClient);
};

}  // namespace test
}  // namespace aura

#endif // UI_AURA_TEST_TEST_CURSOR_CLIENT_H_
