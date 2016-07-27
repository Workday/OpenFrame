// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DEFAULT_ACCESSIBILITY_DELEGATE_H_
#define ASH_DEFAULT_ACCESSIBILITY_DELEGATE_H_

#include "ash/accessibility_delegate.h"
#include "ash/ash_export.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"

namespace ash {

class ASH_EXPORT DefaultAccessibilityDelegate : public AccessibilityDelegate {
 public:
  DefaultAccessibilityDelegate();
  ~DefaultAccessibilityDelegate() override;

  bool IsSpokenFeedbackEnabled() const override;
  void ToggleHighContrast() override;
  bool IsHighContrastEnabled() const override;
  void SetMagnifierEnabled(bool enabled) override;
  void SetMagnifierType(ui::MagnifierType type) override;
  bool IsMagnifierEnabled() const override;
  ui::MagnifierType GetMagnifierType() const override;
  void SetLargeCursorEnabled(bool enabled) override;
  bool IsLargeCursorEnabled() const override;
  void SetAutoclickEnabled(bool enabled) override;
  bool IsAutoclickEnabled() const override;
  void SetVirtualKeyboardEnabled(bool enabled) override;
  bool IsVirtualKeyboardEnabled() const override;
  bool ShouldShowAccessibilityMenu() const override;
  bool IsBrailleDisplayConnected() const override;
  void SilenceSpokenFeedback() const override;
  void ToggleSpokenFeedback(
      ui::AccessibilityNotificationVisibility notify) override;
  void SaveScreenMagnifierScale(double scale) override;
  double GetSavedScreenMagnifierScale() override;
  void TriggerAccessibilityAlert(ui::AccessibilityAlert alert) override;
  ui::AccessibilityAlert GetLastAccessibilityAlert() override;
  void PlayEarcon(int sound_key) override;
  base::TimeDelta PlayShutdownSound() const override;

 private:
  bool spoken_feedback_enabled_;
  bool high_contrast_enabled_;
  bool screen_magnifier_enabled_;
  ui::MagnifierType screen_magnifier_type_;
  bool large_cursor_enabled_;
  bool autoclick_enabled_;
  bool virtual_keyboard_enabled_;
  ui::AccessibilityAlert accessibility_alert_;
  DISALLOW_COPY_AND_ASSIGN(DefaultAccessibilityDelegate);
};

}  // namespace ash

#endif  // DEFAULT_ACCESSIBILITY_DELEGATE_H_
