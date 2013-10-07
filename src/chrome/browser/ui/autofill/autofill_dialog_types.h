// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTOFILL_AUTOFILL_DIALOG_TYPES_H_
#define CHROME_BROWSER_UI_AUTOFILL_AUTOFILL_DIALOG_TYPES_H_

#include <map>
#include <vector>

#include "base/callback_forward.h"
#include "base/strings/string16.h"
#include "components/autofill/core/browser/autofill_metrics.h"
#include "components/autofill/core/browser/field_types.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/font.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/text_constants.h"

namespace autofill {

class AutofillField;

// The time (in milliseconds) to show the splash page when the dialog is first
// started.
extern int const kSplashDisplayDurationMs;
// The time (in milliseconds) spend fading out the splash image.
extern int const kSplashFadeOutDurationMs;
// The time (in milliseconds) spend fading in the dialog (after the splash image
// has been faded out).
extern int const kSplashFadeInDialogDurationMs;

// This struct describes a single input control for the imperative autocomplete
// dialog.
struct DetailInput {
  // Multiple DetailInput structs with the same row_id go on the same row. The
  // actual order of the rows is determined by their order of appearance in
  // kBillingInputs. If negative, don't show the input at all (leave it hidden
  // at all times).
  int row_id;
  ServerFieldType type;
  // Placeholder text resource ID.
  int placeholder_text_rid;
  // A number between 0 and 1.0 that describes how much of the horizontal space
  // in the row should be allotted to this input. 0 is equivalent to 1.
  float expand_weight;
  // When non-empty, indicates the starting value for this input. This will be
  // used when the user is editing existing data.
  string16 initial_value;
  // Whether the input is able to be edited (e.g. text changed in textfields,
  // index changed in comboboxes).
  bool editable;
};

// Determines whether |input| and |field| match.
typedef base::Callback<bool(const DetailInput& input,
                            const AutofillField& field)>
    InputFieldComparator;

// Sections of the dialog --- all fields that may be shown to the user fit under
// one of these sections.
enum DialogSection {
  // Lower boundary value for looping over all sections.
  SECTION_MIN,

  // The Autofill-backed dialog uses separate CC and billing sections.
  SECTION_CC = SECTION_MIN,
  SECTION_BILLING,
  // The wallet-backed dialog uses a combined CC and billing section.
  SECTION_CC_BILLING,
  SECTION_SHIPPING,
  SECTION_EMAIL,

  // Upper boundary value for looping over all sections.
  SECTION_MAX = SECTION_EMAIL
};

// A notification to show in the autofill dialog. Ranges from information to
// seriously scary security messages, and will give you the color it should be
// displayed (if you ask it).
class DialogNotification {
 public:
  enum Type {
    NONE,
    AUTOCHECKOUT_ERROR,
    AUTOCHECKOUT_SUCCESS,
    DEVELOPER_WARNING,
    EXPLANATORY_MESSAGE,
    REQUIRED_ACTION,
    SECURITY_WARNING,
    VALIDATION_ERROR,
    WALLET_ERROR,
    WALLET_USAGE_CONFIRMATION,
  };

  DialogNotification();
  DialogNotification(Type type, const string16& display_text);

  // Returns the appropriate background, border, or text color for the view's
  // notification area based on |type_|.
  SkColor GetBackgroundColor() const;
  SkColor GetBorderColor() const;
  SkColor GetTextColor() const;

  // Whether this notification has an arrow pointing up at the account chooser.
  bool HasArrow() const;

  // Whether this notifications has the "Save details to wallet" checkbox.
  bool HasCheckbox() const;

  Type type() const { return type_; }
  const string16& display_text() const { return display_text_; }

  void set_tooltip_text(const string16& tooltip_text) {
    tooltip_text_ = tooltip_text;
  }
  const string16& tooltip_text() const { return tooltip_text_; }

  void set_checked(bool checked) { checked_ = checked; }
  bool checked() const { return checked_; }

  void set_interactive(bool interactive) { interactive_ = interactive; }
  bool interactive() const { return interactive_; }

 private:
  Type type_;
  string16 display_text_;

  // When non-empty, indicates that a tooltip should be shown on the end of
  // the notification.
  string16 tooltip_text_;

  // Whether the dialog notification's checkbox should be checked. Only applies
  // when |HasCheckbox()| is true.
  bool checked_;

  // When false, this disables user interaction with the notification. For
  // example, WALLET_USAGE_CONFIRMATION notifications set this to false after
  // the submit flow has started.
  bool interactive_;
};

// A notification to show in the autofill dialog. Ranges from information to
// seriously scary security messages, and will give you the color it should be
// displayed (if you ask it).
class DialogAutocheckoutStep {
 public:
  DialogAutocheckoutStep(AutocheckoutStepType type,
                         AutocheckoutStepStatus status);

  // Returns the appropriate color for the display text based on |status_|.
  SkColor GetTextColor() const;

  // Returns the appropriate font for the display text based on |status_|.
  gfx::Font GetTextFont() const;

  // Returns whether the icon for the view should be visable based on |status_|.
  bool IsIconVisible() const;

  // Returns the display text based on |type_| and |status_|.
  string16 GetDisplayText() const;

  AutocheckoutStepStatus status() { return status_; }

  AutocheckoutStepType type() { return type_; }

 private:
  AutocheckoutStepType type_;
  AutocheckoutStepStatus status_;
};

extern SkColor const kWarningColor;

enum DialogSignedInState {
  REQUIRES_RESPONSE,
  REQUIRES_SIGN_IN,
  REQUIRES_PASSIVE_SIGN_IN,
  SIGNED_IN,
  SIGN_IN_DISABLED,
};

// Overall state of the Autocheckout flow.
enum AutocheckoutState {
  AUTOCHECKOUT_ERROR,        // There was an error in the flow.
  AUTOCHECKOUT_IN_PROGRESS,  // The flow is currently in.
  AUTOCHECKOUT_NOT_STARTED,  // The flow has not been initiated by the user yet.
  AUTOCHECKOUT_SUCCESS,      // The flow completed successfully.
};

struct SuggestionState {
  SuggestionState();
  SuggestionState(bool visible,
                  const string16& vertically_compact_text,
                  const string16& horizontally_compact_text,
                  const gfx::Image& icon,
                  const string16& extra_text,
                  const gfx::Image& extra_icon);
  ~SuggestionState();
  // Whether a suggestion should be shown.
  bool visible;
  // Text to be shown for the suggestion. This should be preferred over
  // |horizontally_compact_text| when there's enough horizontal space available
  // to display it. When there's not enough space, fall back to
  // |horizontally_compact_text|.
  base::string16 vertically_compact_text;
  base::string16 horizontally_compact_text;
  gfx::Image icon;
  string16 extra_text;
  gfx::Image extra_icon;
};

// A struct to describe a textual message within a dialog overlay.
struct DialogOverlayString {
  DialogOverlayString();
  ~DialogOverlayString();
  // TODO(estade): need to set a color as well.
  base::string16 text;
  SkColor text_color;
  gfx::Font font;
  gfx::HorizontalAlignment alignment;
};

// A struct to describe a dialog overlay. If |image| is empty, no overlay should
// be shown.
struct DialogOverlayState {
  DialogOverlayState();
  ~DialogOverlayState();
  // If empty, there should not be an overlay. If non-empty, an image that is
  // more or less front and center.
  gfx::Image image;
  // If non-empty, messages to display.
  std::vector<DialogOverlayString> strings;
  // If non-empty, holds text that should go on a button.
  base::string16 button_text;
};

enum ValidationType {
  VALIDATE_EDIT,   // Validate user edits. Allow for empty fields.
  VALIDATE_FINAL,  // Full form validation. Required fields can't be empty.
};

typedef std::vector<DetailInput> DetailInputs;
typedef std::map<const DetailInput*, string16> DetailOutputMap;

typedef std::map<ServerFieldType, string16> ValidityData;

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_AUTOFILL_AUTOFILL_DIALOG_TYPES_H_
