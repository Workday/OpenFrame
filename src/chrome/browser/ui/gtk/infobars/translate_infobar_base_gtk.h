// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_INFOBARS_TRANSLATE_INFOBAR_BASE_GTK_H_
#define CHROME_BROWSER_UI_GTK_INFOBARS_TRANSLATE_INFOBAR_BASE_GTK_H_

#include "base/compiler_specific.h"
#include "chrome/browser/ui/gtk/infobars/infobar_gtk.h"
#include "ui/base/animation/animation_delegate.h"

class TranslateInfoBarDelegate;

// This class contains some of the base functionality that translate infobars
// use.
class TranslateInfoBarBase : public InfoBarGtk {
 protected:
  TranslateInfoBarBase(InfoBarService* owner,
                       TranslateInfoBarDelegate* delegate);
  virtual ~TranslateInfoBarBase();

  // InfoBarGtk:
  virtual void AnimationProgressed(const ui::Animation* animation) OVERRIDE;
  virtual void GetTopColor(InfoBarDelegate::Type type,
                           double* r, double* g, double* b) OVERRIDE;
  virtual void GetBottomColor(InfoBarDelegate::Type type,
                              double* r, double* g, double* b) OVERRIDE;
  virtual void InitWidgets() OVERRIDE;

  // Sub-classes that want to have the options menu button showing should
  // override and return true.
  virtual bool ShowOptionsMenuButton() const;

  // Creates a combobox that displays the languages currently available.
  // |selected_language| is the language index (as used in the
  // TranslateInfoBarDelegate) that should be selected initially.
  // |exclude_language| is the language index of the language that should not be
  // included in the list (TranslateInfoBarDelegate::kNoIndex means no language
  // excluded).
  GtkWidget* CreateLanguageCombobox(size_t selected_language,
                                    size_t exclude_language);

  // Given an above-constructed combobox, returns the currently selected
  // language id.
  static size_t GetLanguageComboboxActiveId(GtkComboBox* combo);

  // Convenience to retrieve the TranslateInfoBarDelegate for this infobar.
  TranslateInfoBarDelegate* GetDelegate();

 private:
  // To be able to map from language id <-> entry in the combo box, we
  // store the language id in the combo box data model in addition to the
  // displayed name.
  enum {
    LANGUAGE_COMBO_COLUMN_ID,
    LANGUAGE_COMBO_COLUMN_NAME,
    LANGUAGE_COMBO_COLUMN_COUNT
  };

  CHROMEGTK_CALLBACK_0(TranslateInfoBarBase, void, OnOptionsClicked);

  // A percentage to average the normal page action background with the error
  // background. When 0, the infobar background should be pure PAGE_ACTION_TYPE.
  // When 1, the infobar background should be pure WARNING_TYPE.
  double background_error_percent_;

  // Changes the color of the background from normal to error color and back.
  scoped_ptr<ui::SlideAnimation> background_color_animation_;

  // The model for the current menu displayed.
  scoped_ptr<ui::MenuModel> menu_model_;

  DISALLOW_COPY_AND_ASSIGN(TranslateInfoBarBase);
};

#endif  // CHROME_BROWSER_UI_GTK_INFOBARS_TRANSLATE_INFOBAR_BASE_GTK_H_
