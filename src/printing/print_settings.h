// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_PRINT_SETTINGS_H_
#define PRINTING_PRINT_SETTINGS_H_

#include <string>

#include "base/strings/string16.h"
#include "printing/page_range.h"
#include "printing/page_setup.h"
#include "printing/print_job_constants.h"
#include "printing/printing_export.h"
#include "ui/gfx/rect.h"

namespace printing {

// Returns true if color model is selected.
PRINTING_EXPORT bool isColorModelSelected(int model);

#if defined(USE_CUPS)
// Get the color model setting name and value for the |color_mode|.
PRINTING_EXPORT void GetColorModelForMode(int color_mode,
                                          std::string* color_setting_name,
                                          std::string* color_value);
#endif

// OS-independent print settings.
class PRINTING_EXPORT PrintSettings {
 public:
  PrintSettings();
  ~PrintSettings();

  // Reinitialize the settings to the default values.
  void Clear();

  // Set printer printable area in in device units.
  void SetPrinterPrintableArea(gfx::Size const& physical_size_device_units,
                               gfx::Rect const& printable_area_device_units,
                               int units_per_inch);

  void SetCustomMargins(const PageMargins& requested_margins_in_points);

  // Equality operator.
  // NOTE: printer_name is NOT tested for equality since it doesn't affect the
  // output.
  bool Equals(const PrintSettings& rhs) const;

  void set_landscape(bool landscape) { landscape_ = landscape; }
  void set_printer_name(const string16& printer_name) {
    printer_name_ = printer_name;
  }
  const string16& printer_name() const { return printer_name_; }
  void set_device_name(const string16& device_name) {
    device_name_ = device_name;
  }
  const string16& device_name() const { return device_name_; }
  void set_dpi(int dpi) { dpi_ = dpi; }
  int dpi() const { return dpi_; }
  void set_supports_alpha_blend(bool supports_alpha_blend) {
    supports_alpha_blend_ = supports_alpha_blend;
  }
  bool supports_alpha_blend() const { return supports_alpha_blend_; }
  const PageSetup& page_setup_device_units() const {
    return page_setup_device_units_;
  }
  int device_units_per_inch() const {
#if defined(OS_MACOSX)
    return 72;
#else  // defined(OS_MACOSX)
    return dpi();
#endif  // defined(OS_MACOSX)
  }

  // Multi-page printing. Each PageRange describes a from-to page combination.
  // This permits printing selected pages only.
  PageRanges ranges;

  // By imaging to a width a little wider than the available pixels, thin pages
  // will be scaled down a little, matching the way they print in IE and Camino.
  // This lets them use fewer sheets than they would otherwise, which is
  // presumably why other browsers do this. Wide pages will be scaled down more
  // than this.
  double min_shrink;

  // This number determines how small we are willing to reduce the page content
  // in order to accommodate the widest line. If the page would have to be
  // reduced smaller to make the widest line fit, we just clip instead (this
  // behavior matches MacIE and Mozilla, at least)
  double max_shrink;

  // Desired visible dots per inch rendering for output. Printing should be
  // scaled to ScreenDpi/dpix*desired_dpi.
  int desired_dpi;

  // Indicates if the user only wants to print the current selection.
  bool selection_only;

  // Indicates what kind of margins should be applied to the printable area.
  MarginType margin_type;

  // Cookie generator. It is used to initialize PrintedDocument with its
  // associated PrintSettings, to be sure that each generated PrintedPage is
  // correctly associated with its corresponding PrintedDocument.
  static int NewCookie();

  // Updates the orientation and flip the page if needed.
  void SetOrientation(bool landscape);

  // Strings to be printed as headers and footers if requested by the user.
  string16 date;
  string16 title;
  string16 url;

  // True if the user wants headers and footers to be displayed.
  bool display_header_footer;

  // True if the user wants to print CSS backgrounds.
  bool should_print_backgrounds;

 private:
  //////////////////////////////////////////////////////////////////////////////
  // Settings that can't be changed without side-effects.

  // Printer name as shown to the user.
  string16 printer_name_;

  // Printer device name as opened by the OS.
  string16 device_name_;

  // Page setup in device units.
  PageSetup page_setup_device_units_;

  // Printer's device effective dots per inch in both axis.
  int dpi_;

  // Is the orientation landscape or portrait.
  bool landscape_;

  // True if this printer supports AlphaBlend.
  bool supports_alpha_blend_;

  // If margin type is custom, this is what was requested.
  PageMargins requested_custom_margins_in_points_;
};

}  // namespace printing

#endif  // PRINTING_PRINT_SETTINGS_H_
