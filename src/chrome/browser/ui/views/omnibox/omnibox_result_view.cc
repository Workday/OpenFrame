// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// For WinDDK ATL compatibility, these ATL headers must come first.
#include "build/build_config.h"
#if defined(OS_WIN)
#include <atlbase.h>  // NOLINT
#include <atlwin.h>  // NOLINT
#endif

#include "chrome/browser/ui/views/omnibox/omnibox_result_view.h"

#include <algorithm>  // NOLINT

#include "base/i18n/bidi_line_iterator.h"
#include "base/memory/scoped_vector.h"
#include "chrome/browser/ui/omnibox/omnibox_popup_model.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_result_view_model.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/text_elider.h"
#include "ui/base/theme_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/render_text.h"
#include "ui/native_theme/native_theme.h"

#if defined(OS_WIN)
#include "ui/native_theme/native_theme_win.h"
#endif

#if defined(USE_AURA)
#include "ui/native_theme/native_theme_aura.h"
#endif

namespace {

const char16 kEllipsis[] = { 0x2026, 0x0 };

// The minimum distance between the top and bottom of the {icon|text} and the
// top or bottom of the row.
const int kMinimumIconVerticalPadding = 2;
const int kMinimumTextVerticalPadding = 3;

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// OmniboxResultView, public:

// Precalculated data used to draw a complete visual run within the match.
// This will include all or part of at least one, and possibly several,
// classifications.
struct OmniboxResultView::RunData {
  RunData() : run_start(0), visual_order(0), is_rtl(false), pixel_width(0) {}

  size_t run_start;  // Offset within the match text where this run begins.
  int visual_order;  // Where this run occurs in visual order.  The earliest
  // run drawn is run 0.
  bool is_rtl;
  int pixel_width;

  // Styled text classification pieces within this run, in logical order.
  Classifications classifications;
};

// This class is a utility class for calculations affected by whether the result
// view is horizontally mirrored.  The drawing functions can be written as if
// all drawing occurs left-to-right, and then use this class to get the actual
// coordinates to begin drawing onscreen.
class OmniboxResultView::MirroringContext {
 public:
  MirroringContext() : center_(0), right_(0) {}

  // Tells the mirroring context to use the provided range as the physical
  // bounds of the drawing region.  When coordinate mirroring is needed, the
  // mirror point will be the center of this range.
  void Initialize(int x, int width) {
    center_ = x + width / 2;
    right_ = x + width;
  }

  // Given a logical range within the drawing region, returns the coordinate of
  // the possibly-mirrored "left" side.  (This functions exactly like
  // View::MirroredLeftPointForRect().)
  int mirrored_left_coord(int left, int right) const {
    return base::i18n::IsRTL() ? (center_ + (center_ - right)) : left;
  }

  // Given a logical coordinate within the drawing region, returns the remaining
  // width available.
  int remaining_width(int x) const {
    return right_ - x;
  }

 private:
  int center_;
  int right_;

  DISALLOW_COPY_AND_ASSIGN(MirroringContext);
};

OmniboxResultView::OmniboxResultView(
    OmniboxResultViewModel* model,
    int model_index,
    LocationBarView* location_bar_view,
    const gfx::FontList& font_list)
    : edge_item_padding_(LocationBarView::GetItemPadding()),
      item_padding_(LocationBarView::GetItemPadding()),
      minimum_text_vertical_padding_(kMinimumTextVerticalPadding),
      model_(model),
      model_index_(model_index),
      location_bar_view_(location_bar_view),
      font_list_(font_list),
      font_height_(std::max(font_list.GetHeight(),
                            font_list.DeriveFontList(gfx::BOLD).GetHeight())),
      ellipsis_width_(font_list.GetPrimaryFont().GetStringWidth(
          string16(kEllipsis))),
      mirroring_context_(new MirroringContext()),
      keyword_icon_(new views::ImageView()),
      animation_(new ui::SlideAnimation(this)) {
  CHECK_GE(model_index, 0);
  if (default_icon_size_ == 0) {
    default_icon_size_ =
        location_bar_view_->GetThemeProvider()->GetImageSkiaNamed(
            AutocompleteMatch::TypeToIcon(
                AutocompleteMatchType::URL_WHAT_YOU_TYPED))->width();
  }
  keyword_icon_->set_owned_by_client();
  keyword_icon_->EnableCanvasFlippingForRTLUI(true);
  keyword_icon_->SetImage(GetKeywordIcon());
  keyword_icon_->SizeToPreferredSize();
}

OmniboxResultView::~OmniboxResultView() {
}

SkColor OmniboxResultView::GetColor(
    ResultViewState state,
    ColorKind kind) const {
  const ui::NativeTheme* theme = GetNativeTheme();
#if defined(OS_WIN)
  if (theme == ui::NativeThemeWin::instance()) {
    static bool win_initialized = false;
    static SkColor win_colors[NUM_STATES][NUM_KINDS];
    if (!win_initialized) {
      win_colors[NORMAL][BACKGROUND] = color_utils::GetSysSkColor(COLOR_WINDOW);
      win_colors[SELECTED][BACKGROUND] =
          color_utils::GetSysSkColor(COLOR_HIGHLIGHT);
      win_colors[NORMAL][TEXT] = color_utils::GetSysSkColor(COLOR_WINDOWTEXT);
      win_colors[SELECTED][TEXT] =
          color_utils::GetSysSkColor(COLOR_HIGHLIGHTTEXT);
      CommonInitColors(theme, win_colors);
      win_initialized = true;
    }
    return win_colors[state][kind];
  }
#endif
  static bool initialized = false;
  static SkColor colors[NUM_STATES][NUM_KINDS];
  if (!initialized) {
    colors[NORMAL][BACKGROUND] = theme->GetSystemColor(
        ui::NativeTheme::kColorId_TextfieldDefaultBackground);
    colors[NORMAL][TEXT] = theme->GetSystemColor(
        ui::NativeTheme::kColorId_TextfieldDefaultColor);
    colors[NORMAL][URL] = SkColorSetARGB(0xff, 0x00, 0x99, 0x33);
    colors[SELECTED][BACKGROUND] = theme->GetSystemColor(
        ui::NativeTheme::kColorId_TextfieldSelectionBackgroundFocused);
    colors[SELECTED][TEXT] = theme->GetSystemColor(
        ui::NativeTheme::kColorId_TextfieldSelectionColor);
    colors[SELECTED][URL] = SkColorSetARGB(0xff, 0x00, 0x66, 0x22);
    colors[HOVERED][URL] = SkColorSetARGB(0xff, 0x00, 0x66, 0x22);
    CommonInitColors(theme, colors);
    initialized = true;
  }
  return colors[state][kind];
}

void OmniboxResultView::SetMatch(const AutocompleteMatch& match) {
  match_ = match;
  animation_->Reset();

  if (match.associated_keyword.get()) {
    keyword_icon_->SetImage(GetKeywordIcon());

    if (!keyword_icon_->parent())
      AddChildView(keyword_icon_.get());
  } else if (keyword_icon_->parent()) {
    RemoveChildView(keyword_icon_.get());
  }

  Layout();
}

void OmniboxResultView::ShowKeyword(bool show_keyword) {
  if (show_keyword)
    animation_->Show();
  else
    animation_->Hide();
}

void OmniboxResultView::Invalidate() {
  keyword_icon_->SetImage(GetKeywordIcon());
  SchedulePaint();
}

gfx::Size OmniboxResultView::GetPreferredSize() {
  return gfx::Size(0, std::max(
      default_icon_size_ + (kMinimumIconVerticalPadding * 2),
      GetTextHeight() + (minimum_text_vertical_padding_ * 2)));
}

////////////////////////////////////////////////////////////////////////////////
// OmniboxResultView, protected:

OmniboxResultView::ResultViewState OmniboxResultView::GetState() const {
  if (model_->IsSelectedIndex(model_index_))
    return SELECTED;
  return model_->IsHoveredIndex(model_index_) ? HOVERED : NORMAL;
}

int OmniboxResultView::GetTextHeight() const {
  return font_height_;
}

void OmniboxResultView::PaintMatch(gfx::Canvas* canvas,
                                   const AutocompleteMatch& match,
                                   int x) {
  x = DrawString(canvas, match.contents, match.contents_class, false, x,
                 text_bounds_.y());

  // Paint the description.
  // TODO(pkasting): Because we paint in multiple separate pieces, we can wind
  // up with no space even for an ellipsis for one or both of these pieces.
  // Instead, we should paint the entire match as a single long string.  This
  // would also let us use a more properly-localizable string than we get with
  // just the IDS_AUTOCOMPLETE_MATCH_DESCRIPTION_SEPARATOR.
  if (!match.description.empty()) {
    string16 separator =
        l10n_util::GetStringUTF16(IDS_AUTOCOMPLETE_MATCH_DESCRIPTION_SEPARATOR);
    ACMatchClassifications classifications;
    classifications.push_back(
        ACMatchClassification(0, ACMatchClassification::NONE));
    x = DrawString(canvas, separator, classifications, true, x,
                   text_bounds_.y());

    DrawString(canvas, match.description, match.description_class, true, x,
               text_bounds_.y());
  }
}

// static
void OmniboxResultView::CommonInitColors(const ui::NativeTheme* theme,
                                         SkColor colors[][NUM_KINDS]) {
  colors[HOVERED][BACKGROUND] =
      color_utils::AlphaBlend(colors[SELECTED][BACKGROUND],
                              colors[NORMAL][BACKGROUND], 64);
  colors[HOVERED][TEXT] = colors[NORMAL][TEXT];
#if defined(USE_AURA)
  const bool is_aura = theme == ui::NativeThemeAura::instance();
#else
  const bool is_aura = false;
#endif
  for (int i = 0; i < NUM_STATES; ++i) {
    if (is_aura) {
      colors[i][TEXT] =
          color_utils::AlphaBlend(SK_ColorBLACK, colors[i][BACKGROUND], 0xdd);
      colors[i][DIMMED_TEXT] =
          color_utils::AlphaBlend(SK_ColorBLACK, colors[i][BACKGROUND], 0xbb);
    } else {
      colors[i][DIMMED_TEXT] =
          color_utils::AlphaBlend(colors[i][TEXT], colors[i][BACKGROUND], 128);
      colors[i][URL] = color_utils::GetReadableColor(SkColorSetRGB(0, 128, 0),
                                                     colors[i][BACKGROUND]);
    }

    // TODO(joi): Programmatically draw the dropdown border using
    // this color as well. (Right now it's drawn as black with 25%
    // alpha.)
    colors[i][DIVIDER] =
        color_utils::AlphaBlend(colors[i][TEXT], colors[i][BACKGROUND], 0x34);
  }
}

// static
bool OmniboxResultView::SortRunsLogically(const RunData& lhs,
                                          const RunData& rhs) {
  return lhs.run_start < rhs.run_start;
}

// static
bool OmniboxResultView::SortRunsVisually(const RunData& lhs,
                                         const RunData& rhs) {
  return lhs.visual_order < rhs.visual_order;
}

// static
int OmniboxResultView::default_icon_size_ = 0;

gfx::ImageSkia OmniboxResultView::GetIcon() const {
  const gfx::Image image = model_->GetIconIfExtensionMatch(model_index_);
  if (!image.IsEmpty())
    return image.AsImageSkia();

  int icon = match_.starred ?
      IDR_OMNIBOX_STAR : AutocompleteMatch::TypeToIcon(match_.type);
  if (GetState() == SELECTED) {
    switch (icon) {
      case IDR_OMNIBOX_EXTENSION_APP:
        icon = IDR_OMNIBOX_EXTENSION_APP_SELECTED;
        break;
      case IDR_OMNIBOX_HTTP:
        icon = IDR_OMNIBOX_HTTP_SELECTED;
        break;
      case IDR_OMNIBOX_SEARCH:
        icon = IDR_OMNIBOX_SEARCH_SELECTED;
        break;
      case IDR_OMNIBOX_STAR:
        icon = IDR_OMNIBOX_STAR_SELECTED;
        break;
      default:
        NOTREACHED();
        break;
    }
  }
  return *(location_bar_view_->GetThemeProvider()->GetImageSkiaNamed(icon));
}

const gfx::ImageSkia* OmniboxResultView::GetKeywordIcon() const {
  // NOTE: If we ever begin returning icons of varying size, then callers need
  // to ensure that |keyword_icon_| is resized each time its image is reset.
  return location_bar_view_->GetThemeProvider()->GetImageSkiaNamed(
      (GetState() == SELECTED) ? IDR_OMNIBOX_TTS_SELECTED : IDR_OMNIBOX_TTS);
}

int OmniboxResultView::DrawString(
    gfx::Canvas* canvas,
    const string16& text,
    const ACMatchClassifications& classifications,
    bool force_dim,
    int x,
    int y) {
  if (text.empty())
    return x;

  // Check whether or not this text is a URL.  URLs are always displayed LTR
  // regardless of locale.
  bool is_url = true;
  for (ACMatchClassifications::const_iterator i(classifications.begin());
       i != classifications.end(); ++i) {
    if (!(i->style & ACMatchClassification::URL)) {
      is_url = false;
      break;
    }
  }

  // Split the text into visual runs.  We do this first so that we don't need to
  // worry about whether our eliding might change the visual display in
  // unintended ways, e.g. by removing directional markings or by adding an
  // ellipsis that's not enclosed in appropriate markings.
  base::i18n::BiDiLineIterator bidi_line;
  if (!bidi_line.Open(text, base::i18n::IsRTL(), is_url))
    return x;
  const int num_runs = bidi_line.CountRuns();
  ScopedVector<gfx::RenderText> render_texts;
  Runs runs;
  for (int run = 0; run < num_runs; ++run) {
    int run_start_int = 0, run_length_int = 0;
    // The index we pass to GetVisualRun corresponds to the position of the run
    // in the displayed text. For example, the string "Google in HEBREW" (where
    // HEBREW is text in the Hebrew language) has two runs: "Google in " which
    // is an LTR run, and "HEBREW" which is an RTL run. In an LTR context, the
    // run "Google in " has the index 0 (since it is the leftmost run
    // displayed). In an RTL context, the same run has the index 1 because it
    // is the rightmost run. This is why the order in which we traverse the
    // runs is different depending on the locale direction.
    const UBiDiDirection run_direction = bidi_line.GetVisualRun(
        (base::i18n::IsRTL() && !is_url) ? (num_runs - run - 1) : run,
        &run_start_int, &run_length_int);
    DCHECK_GT(run_length_int, 0);
    runs.push_back(RunData());
    RunData* current_run = &runs.back();
    current_run->run_start = run_start_int;
    const size_t run_end = current_run->run_start + run_length_int;
    current_run->visual_order = run;
    current_run->is_rtl = !is_url && (run_direction == UBIDI_RTL);

    // Compute classifications for this run.
    for (size_t i = 0; i < classifications.size(); ++i) {
      const size_t text_start =
          std::max(classifications[i].offset, current_run->run_start);
      if (text_start >= run_end)
        break;  // We're past the last classification in the run.

      const size_t text_end = (i < (classifications.size() - 1)) ?
          std::min(classifications[i + 1].offset, run_end) : run_end;
      if (text_end <= current_run->run_start)
        continue;  // We haven't reached the first classification in the run.

      render_texts.push_back(gfx::RenderText::CreateInstance());
      gfx::RenderText* render_text = render_texts.back();
      current_run->classifications.push_back(render_text);
      render_text->SetText(text.substr(text_start, text_end - text_start));
      render_text->SetFontList(font_list_);

      // Calculate style-related data.
      if (classifications[i].style & ACMatchClassification::MATCH)
        render_text->SetStyle(gfx::BOLD, true);
      const ResultViewState state = GetState();
      if (classifications[i].style & ACMatchClassification::URL)
        render_text->SetColor(GetColor(state, URL));
      else if (classifications[i].style & ACMatchClassification::DIM)
        render_text->SetColor(GetColor(state, DIMMED_TEXT));
      else
        render_text->SetColor(GetColor(state, force_dim ? DIMMED_TEXT : TEXT));

      current_run->pixel_width += render_text->GetStringSize().width();
    }
    DCHECK(!current_run->classifications.empty());
  }
  DCHECK(!runs.empty());

  // Sort into logical order so we can elide logically.
  std::sort(runs.begin(), runs.end(), &SortRunsLogically);

  // Now determine what to elide, if anything.  Several subtle points:
  //   * Because we have the run data, we can get edge cases correct, like
  //     whether to place an ellipsis before or after the end of a run when the
  //     text needs to be elided at the run boundary.
  //   * The "or one before it" comments below refer to cases where an earlier
  //     classification fits completely, but leaves too little space for an
  //     ellipsis that turns out to be needed later.  These cases are commented
  //     more completely in Elide().
  int remaining_width = mirroring_context_->remaining_width(x);
  for (Runs::iterator i(runs.begin()); i != runs.end(); ++i) {
    if (i->pixel_width > remaining_width) {
      // This run or one before it needs to be elided.
      for (Classifications::iterator j(i->classifications.begin());
           j != i->classifications.end(); ++j) {
        const int width = (*j)->GetStringSize().width();
        if (width > remaining_width) {
          // This classification or one before it needs to be elided.  Erase all
          // further classifications and runs so Elide() can simply reverse-
          // iterate over everything to find the specific classification to
          // elide.
          i->classifications.erase(++j, i->classifications.end());
          runs.erase(++i, runs.end());
          Elide(&runs, remaining_width);
          break;
        }
        remaining_width -= width;
      }
      break;
    }
    remaining_width -= i->pixel_width;
  }

  // Sort back into visual order so we can display the runs correctly.
  std::sort(runs.begin(), runs.end(), &SortRunsVisually);

  // Draw the runs.
  for (Runs::iterator i(runs.begin()); i != runs.end(); ++i) {
    const bool reverse_visible_order = (i->is_rtl != base::i18n::IsRTL());
    if (reverse_visible_order)
      std::reverse(i->classifications.begin(), i->classifications.end());
    for (Classifications::const_iterator j(i->classifications.begin());
         j != i->classifications.end(); ++j) {
      const gfx::Size size = (*j)->GetStringSize();
      // Align the text runs to a common baseline.
      const gfx::Rect rect(
          mirroring_context_->mirrored_left_coord(x, x + size.width()),
          y + font_list_.GetBaseline() - (*j)->GetBaseline(),
          size.width(), size.height());
      (*j)->SetDisplayRect(rect);
      (*j)->Draw(canvas);
      x += size.width();
    }
  }

  return x;
}

void OmniboxResultView::Elide(Runs* runs, int remaining_width) const {
  // The complexity of this function is due to edge cases like the following:
  // We have 100 px of available space, an initial classification that takes 86
  // px, and a font that has a 15 px wide ellipsis character.  Now if the first
  // classification is followed by several very narrow classifications (e.g. 3
  // px wide each), we don't know whether we need to elide or not at the time we
  // see the first classification -- it depends on how many subsequent
  // classifications follow, and some of those may be in the next run (or
  // several runs!).  This is why instead we let our caller move forward until
  // we know we definitely need to elide, and then in this function we move
  // backward again until we find a string that we can successfully do the
  // eliding on.
  bool first_classification = true;
  for (Runs::reverse_iterator i(runs->rbegin()); i != runs->rend(); ++i) {
    for (Classifications::reverse_iterator j(i->classifications.rbegin());
         j != i->classifications.rend(); ++j) {
      if (!first_classification) {
        // We also add this classification's width (sans ellipsis) back to the
        // available width since we want to consider the available space we'll
        // have when we draw this classification.
        remaining_width += (*j)->GetStringSize().width();

        // For all but the first classification we consider, we need to append
        // an ellipsis, since there isn't enough room to draw it after this
        // classification.
        (*j)->SetText((*j)->text() + kEllipsis);
      }
      first_classification = false;

      // Can we fit at least an ellipsis?
      gfx::Font font((*j)->GetStyle(gfx::BOLD) ?
          (*j)->GetPrimaryFont().DeriveFont(0, gfx::Font::BOLD) :
          (*j)->GetPrimaryFont());
      string16 elided_text(
          ui::ElideText((*j)->text(), font, remaining_width, ui::ELIDE_AT_END));
      Classifications::reverse_iterator prior(j + 1);
      const bool on_first_classification = (prior == i->classifications.rend());
      if (elided_text.empty() && (remaining_width >= ellipsis_width_) &&
          on_first_classification) {
        // Edge case: This classification is bold, we can't fit a bold ellipsis
        // but we can fit a normal one, and this is the first classification in
        // the run.  We should display a lone normal ellipsis, because appending
        // one to the end of the previous run might put it in the wrong visual
        // location (if the previous run is reversed from the normal visual
        // order).
        // NOTE: If this isn't the first classification in the run, we don't
        // need to bother with this; see note below.
        elided_text = kEllipsis;
      }
      if (!elided_text.empty()) {
        // Success.  Elide this classification and stop.
        (*j)->SetText(elided_text);

        // If we could only fit an ellipsis, then only make it bold if there was
        // an immediate prior classification in this run that was also bold, or
        // it will look orphaned.
        if ((*j)->GetStyle(gfx::BOLD) && (elided_text.length() == 1) &&
            (on_first_classification || !(*prior)->GetStyle(gfx::BOLD)))
          (*j)->SetStyle(gfx::BOLD, false);

        // Erase any other classifications that come after the elided one.
        i->classifications.erase(j.base(), i->classifications.end());
        runs->erase(i.base(), runs->end());
        return;
      }

      // We couldn't fit an ellipsis.  Move back one classification,
      // append an ellipsis, and try again.
      // NOTE: In the edge case that a bold ellipsis doesn't fit but a
      // normal one would, and we reach here, then there is a previous
      // classification in this run, and so either:
      //   * It's normal, and will be able to draw successfully with the
      //     ellipsis we'll append to it, or
      //   * It is also bold, in which case we don't want to fall back
      //     to a normal ellipsis anyway (see comment above).
    }
  }

  // We couldn't draw anything.
  runs->clear();
}

void OmniboxResultView::Layout() {
  const gfx::ImageSkia icon = GetIcon();

  icon_bounds_.SetRect(edge_item_padding_ +
      ((icon.width() == default_icon_size_) ?
          0 : LocationBarView::kIconInternalPadding),
      (height() - icon.height()) / 2, icon.width(), icon.height());

  int text_x = edge_item_padding_ + default_icon_size_ + item_padding_;
  int text_height = GetTextHeight();
  int text_width;

  if (match_.associated_keyword.get()) {
    const int kw_collapsed_size =
        keyword_icon_->width() + edge_item_padding_;
    const int max_kw_x = width() - kw_collapsed_size;
    const int kw_x =
        animation_->CurrentValueBetween(max_kw_x, edge_item_padding_);
    const int kw_text_x = kw_x + keyword_icon_->width() + item_padding_;

    text_width = kw_x - text_x - item_padding_;
    keyword_text_bounds_.SetRect(kw_text_x, 0,
        std::max(width() - kw_text_x - edge_item_padding_, 0), text_height);
    keyword_icon_->SetPosition(gfx::Point(kw_x,
        (height() - keyword_icon_->height()) / 2));
  } else {
    text_width = width() - text_x - edge_item_padding_;
  }

  text_bounds_.SetRect(text_x, std::max(0, (height() - text_height) / 2),
      std::max(text_width, 0), text_height);
}

void OmniboxResultView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  animation_->SetSlideDuration(width() / 4);
}

void OmniboxResultView::OnPaint(gfx::Canvas* canvas) {
  const ResultViewState state = GetState();
  if (state != NORMAL)
    canvas->DrawColor(GetColor(state, BACKGROUND));

  if (!match_.associated_keyword.get() ||
      keyword_icon_->x() > icon_bounds_.right()) {
    // Paint the icon.
    canvas->DrawImageInt(GetIcon(), GetMirroredXForRect(icon_bounds_),
                         icon_bounds_.y());

    // Paint the text.
    int x = GetMirroredXForRect(text_bounds_);
    mirroring_context_->Initialize(x, text_bounds_.width());
    PaintMatch(canvas, match_, x);
  }

  if (match_.associated_keyword.get()) {
    // Paint the keyword text.
    int x = GetMirroredXForRect(keyword_text_bounds_);
    mirroring_context_->Initialize(x, keyword_text_bounds_.width());
    PaintMatch(canvas, *match_.associated_keyword.get(), x);
  }
}

void OmniboxResultView::AnimationProgressed(const ui::Animation* animation) {
  Layout();
  SchedulePaint();
}
