// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/chromeos/pointer_handler.h"

#include "base/basictypes.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace chromeos {
namespace options {

PointerHandler::PointerHandler()
  : has_touchpad_(false),
    has_mouse_(false) {
}

PointerHandler::~PointerHandler() {
}

void PointerHandler::GetLocalizedValues(DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "pointerOverlayTitleTouchpadOnly",
        IDS_OPTIONS_POINTER_TOUCHPAD_OVERLAY_TITLE },
    { "pointerOverlayTitleMouseOnly",
        IDS_OPTIONS_POINTER_MOUSE_OVERLAY_TITLE },
    { "pointerOverlayTitleTouchpadMouse",
        IDS_OPTIONS_POINTER_TOUCHPAD_MOUSE_OVERLAY_TITLE },
    { "pointerOverlaySectionTitleTouchpad",
      IDS_OPTIONS_POINTER_OVERLAY_SECTION_TITLE_TOUCHPAD },
    { "pointerOverlaySectionTitleMouse",
      IDS_OPTIONS_POINTER_OVERLAY_SECTION_TITLE_MOUSE },
    { "enableTapToClick",
      IDS_OPTIONS_SETTINGS_TAP_TO_CLICK_ENABLED_DESCRIPTION },
    { "primaryMouseRight",
      IDS_OPTIONS_SETTINGS_PRIMARY_MOUSE_RIGHT_DESCRIPTION },
    { "traditionalScroll",
      IDS_OPTIONS_SETTINGS_TRADITIONAL_SCROLL_DESCRIPTION },
  };

  localized_strings->SetString("naturalScroll",
      l10n_util::GetStringFUTF16(
          IDS_OPTIONS_SETTINGS_NATURAL_SCROLL_DESCRIPTION,
          ASCIIToUTF16(chrome::kNaturalScrollHelpURL)));

  RegisterStrings(localized_strings, resources, arraysize(resources));
}


void PointerHandler::TouchpadExists(bool exists) {
  has_touchpad_ = exists;
  base::FundamentalValue val(exists);
  web_ui()->CallJavascriptFunction("PointerOverlay.showTouchpadControls", val);
  UpdateTitle();
}

void PointerHandler::MouseExists(bool exists) {
  has_mouse_ = exists;
  base::FundamentalValue val(exists);
  web_ui()->CallJavascriptFunction("PointerOverlay.showMouseControls", val);
  UpdateTitle();
}

void PointerHandler::UpdateTitle() {
  std::string label;
  if (has_touchpad_) {
    label = has_mouse_ ? "pointerOverlayTitleTouchpadMouse" :
        "pointerOverlayTitleTouchpadOnly";
  } else {
    label = has_mouse_ ? "pointerOverlayTitleMouseOnly" : "";
  }
  base::StringValue val(label);
  web_ui()->CallJavascriptFunction("PointerOverlay.setTitle", val);
}

}  // namespace options
}  // namespace chromeos
