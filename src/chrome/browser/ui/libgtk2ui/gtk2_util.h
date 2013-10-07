// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_LIBGTK2UI_GTK2_UTIL_H_
#define CHROME_BROWSER_UI_LIBGTK2UI_GTK2_UTIL_H_

#include <gtk/gtk.h>
#include <string>

class CommandLine;
class SkBitmap;

namespace base {
class Environment;
}

namespace ui {
class Accelerator;
}

namespace libgtk2ui {

void GtkInitFromCommandLine(const CommandLine& command_line);

// Returns the name of the ".desktop" file associated with our running process.
std::string GetDesktopName(base::Environment* env);

// Show the image for the given menu item, even if the user's default is to not
// show images. Only to be used for favicons or other menus where the image is
// crucial to its functionality.
void SetAlwaysShowImage(GtkWidget* image_menu_item);

guint GetGdkKeyCodeForAccelerator(const ui::Accelerator& accelerator);

GdkModifierType GetGdkModifierForAccelerator(
    const ui::Accelerator& accelerator);

// Translates event flags into plaform independent event flags.
int EventFlagsFromGdkState(guint state);

}  // namespace libgtk2ui

#endif  // CHROME_BROWSER_UI_LIBGTK2UI_GTK2_UTIL_H_
