// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_OPEN_PDF_IN_READER_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_OPEN_PDF_IN_READER_VIEW_H_

#include "ui/views/controls/image_view.h"
#include "ui/views/widget/widget_observer.h"

class LocationBarView;
class OpenPDFInReaderBubbleView;
class OpenPDFInReaderPromptDelegate;

namespace content {
class WebContents;
}

// A Page Action image view for the "Open PDF in Reader" bubble.
class OpenPDFInReaderView : public views::ImageView,
                            public views::WidgetObserver {
 public:
  explicit OpenPDFInReaderView(LocationBarView* location_bar_view);
  virtual ~OpenPDFInReaderView();

  void Update(content::WebContents* web_contents);

 private:
  void ShowBubble();

  // views::ImageView:
  virtual void GetAccessibleState(ui::AccessibleViewState* state) OVERRIDE;
  virtual bool OnMousePressed(const ui::MouseEvent& event) OVERRIDE;
  virtual void OnMouseReleased(const ui::MouseEvent& event) OVERRIDE;
  virtual bool OnKeyPressed(const ui::KeyEvent& event) OVERRIDE;

  // views::WidgetObserver:
  virtual void OnWidgetDestroying(views::Widget* widget) OVERRIDE;

  // The LocationBarView hosting this view.
  LocationBarView* location_bar_view_;

  OpenPDFInReaderBubbleView* bubble_;

  // Weak pointer; owned by the PDFTabHelper of the currently active tab.
  OpenPDFInReaderPromptDelegate* model_;

  DISALLOW_COPY_AND_ASSIGN(OpenPDFInReaderView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_OPEN_PDF_IN_READER_VIEW_H_
