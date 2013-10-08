// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/libgtk2ui/gtk2_util.h"

#include <gtk/gtk.h>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/memory/scoped_ptr.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/events/event_constants.h"
#include "ui/base/keycodes/keyboard_code_conversion_x.h"
#include "ui/gfx/size.h"

namespace {

void CommonInitFromCommandLine(const CommandLine& command_line,
                               void (*init_func)(gint*, gchar***)) {
  const std::vector<std::string>& args = command_line.argv();
  int argc = args.size();
  scoped_ptr<char *[]> argv(new char *[argc + 1]);
  for (size_t i = 0; i < args.size(); ++i) {
    // TODO(piman@google.com): can gtk_init modify argv? Just being safe
    // here.
    argv[i] = strdup(args[i].c_str());
  }
  argv[argc] = NULL;
  char **argv_pointer = argv.get();

  init_func(&argc, &argv_pointer);
  for (size_t i = 0; i < args.size(); ++i) {
    free(argv[i]);
  }
}

}  // namespace

namespace libgtk2ui {

void GtkInitFromCommandLine(const CommandLine& command_line) {
  CommonInitFromCommandLine(command_line, gtk_init);
}

// TODO(erg): This method was copied out of shell_integration_linux.cc. Because
// of how this library is structured as a stand alone .so, we can't call code
// from browser and above.
std::string GetDesktopName(base::Environment* env) {
#if defined(GOOGLE_CHROME_BUILD)
  return "google-chrome.desktop";
#else  // CHROMIUM_BUILD
  // Allow $CHROME_DESKTOP to override the built-in value, so that development
  // versions can set themselves as the default without interfering with
  // non-official, packaged versions using the built-in value.
  std::string name;
  if (env->GetVar("CHROME_DESKTOP", &name) && !name.empty())
    return name;
  return "chromium-browser.desktop";
#endif
}

void SetAlwaysShowImage(GtkWidget* image_menu_item) {
  gtk_image_menu_item_set_always_show_image(
      GTK_IMAGE_MENU_ITEM(image_menu_item), TRUE);
}

guint GetGdkKeyCodeForAccelerator(const ui::Accelerator& accelerator) {
  // The second parameter is false because accelerator keys are expressed in
  // terms of the non-shift-modified key.
  return XKeysymForWindowsKeyCode(accelerator.key_code(), false);
}

GdkModifierType GetGdkModifierForAccelerator(
    const ui::Accelerator& accelerator) {
  int event_flag = accelerator.modifiers();
  int modifier = 0;
  if (event_flag & ui::EF_SHIFT_DOWN)
    modifier |= GDK_SHIFT_MASK;
  if (event_flag & ui::EF_CONTROL_DOWN)
    modifier |= GDK_CONTROL_MASK;
  if (event_flag & ui::EF_ALT_DOWN)
    modifier |= GDK_MOD1_MASK;
  return static_cast<GdkModifierType>(modifier);
}

int EventFlagsFromGdkState(guint state) {
  int flags = ui::EF_NONE;
  flags |= (state & GDK_LOCK_MASK) ? ui::EF_CAPS_LOCK_DOWN : ui::EF_NONE;
  flags |= (state & GDK_CONTROL_MASK) ? ui::EF_CONTROL_DOWN : ui::EF_NONE;
  flags |= (state & GDK_SHIFT_MASK) ? ui::EF_SHIFT_DOWN : ui::EF_NONE;
  flags |= (state & GDK_MOD1_MASK) ? ui::EF_ALT_DOWN : ui::EF_NONE;
  flags |= (state & GDK_BUTTON1_MASK) ? ui::EF_LEFT_MOUSE_BUTTON : ui::EF_NONE;
  flags |=
      (state & GDK_BUTTON2_MASK) ? ui::EF_MIDDLE_MOUSE_BUTTON : ui::EF_NONE;
  flags |= (state & GDK_BUTTON3_MASK) ? ui::EF_RIGHT_MOUSE_BUTTON : ui::EF_NONE;
  return flags;
}

}  // namespace libgtk2ui
