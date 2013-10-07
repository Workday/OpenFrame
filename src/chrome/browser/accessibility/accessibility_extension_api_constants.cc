// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/accessibility/accessibility_extension_api_constants.h"

namespace extension_accessibility_api_constants {

// String keys for AccessibilityObject properties.
const char kTypeKey[] = "type";
const char kNameKey[] = "name";
const char kContextKey[] = "context";
const char kDetailsKey[] = "details";
const char kValueKey[] = "details.value";
const char kPasswordKey[] = "details.isPassword";
const char kItemCountKey[] = "details.itemCount";
const char kItemIndexKey[] = "details.itemIndex";
const char kSelectionStartKey[] = "details.selectionStart";
const char kSelectionEndKey[] = "details.selectionEnd";
const char kCheckedKey[] = "details.isChecked";
const char kHasSubmenuKey[] = "details.hasSubmenu";
const char kMessageKey[] = "message";
const char kStringValueKey[] = "details.stringValue";

// Events.
const char kOnWindowOpened[] = "experimental.accessibility.onWindowOpened";
const char kOnWindowClosed[] = "experimental.accessibility.onWindowClosed";
const char kOnControlFocused[] = "experimental.accessibility.onControlFocused";
const char kOnControlAction[] = "experimental.accessibility.onControlAction";
const char kOnTextChanged[] = "experimental.accessibility.onTextChanged";
const char kOnMenuOpened[] = "experimental.accessibility.onMenuOpened";
const char kOnMenuClosed[] = "experimental.accessibility.onMenuClosed";

// Types of controls that can receive accessibility events.
const char kTypeButton[] = "button";
const char kTypeCheckbox[] = "checkbox";
const char kTypeComboBox[] = "combobox";
const char kTypeLink[] = "link";
const char kTypeListBox[] = "listbox";
const char kTypeMenu[] = "menu";
const char kTypeMenuItem[] = "menuitem";
const char kTypeRadioButton[] = "radiobutton";
const char kTypeSlider[] = "slider";
const char kTypeTab[] = "tab";
const char kTypeTextBox[] = "textbox";
const char kTypeWindow[] = "window";

}  // namespace extension_accessibility_api_constants
