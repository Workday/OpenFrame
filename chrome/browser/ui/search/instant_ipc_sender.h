// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SEARCH_INSTANT_IPC_SENDER_H_
#define CHROME_BROWSER_UI_SEARCH_INSTANT_IPC_SENDER_H_

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "chrome/common/instant_types.h"
#include "chrome/common/omnibox_focus_state.h"
#include "content/public/browser/web_contents_observer.h"

namespace gfx {
class Rect;
}

namespace IPC {
class Sender;
}

class InstantIPCSender : public content::WebContentsObserver {
 public:
  // Creates a new instance of InstantIPCSender. If |is_incognito| is true,
  // the instance will only send appropriate IPCs for incognito profiles.
  static scoped_ptr<InstantIPCSender> Create(bool is_incognito);

  virtual ~InstantIPCSender() {}

  // Sets |web_contents| as the receiver of IPCs.
  void SetContents(content::WebContents* web_contents);

  // Tells the page that the user pressed Enter in the omnibox.
  virtual void Submit(const string16& text) {}

  // Tells the page the left and right margins of the omnibox. This is used by
  // the page to align text or assets properly with the omnibox.
  virtual void SetOmniboxBounds(const gfx::Rect& bounds) {}

  // Tells the page about the font information.
  virtual void SetFontInformation(const string16& omnibox_font_name,
                                  size_t omnibox_font_size) {}

  // Tells the page information it needs to display promos.
  virtual void SetPromoInformation(bool is_app_launcher_enabled) {}

  // Tells the page about the current theme background.
  virtual void SendThemeBackgroundInfo(
      const ThemeBackgroundInfo& theme_info) {}

  // Tells the page that the omnibox focus has changed.
  virtual void FocusChanged(OmniboxFocusState state,
                            OmniboxFocusChangeReason reason) {}

  // Tells the page that user input started or stopped.
  virtual void SetInputInProgress(bool input_in_progress) {}

  // Tells the page about new Most Visited data.
  virtual void SendMostVisitedItems(
      const std::vector<InstantMostVisitedItem>& items) {}

  // Tells the page to toggle voice search.
  virtual void ToggleVoiceSearch() {}

 protected:
  InstantIPCSender() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InstantIPCSender);
};

#endif  // CHROME_BROWSER_UI_SEARCH_INSTANT_IPC_SENDER_H_
