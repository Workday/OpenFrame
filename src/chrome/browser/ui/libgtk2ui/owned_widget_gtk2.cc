// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/libgtk2ui/owned_widget_gtk2.h"

#include <gtk/gtk.h>

#include "base/logging.h"

namespace libgtk2ui {

OwnedWidgetGtk::~OwnedWidgetGtk() {
  Destroy();
}

void OwnedWidgetGtk::Own(GtkWidget* widget) {
  if (!widget)
    return;

  DCHECK(!widget_);
  // We want to make sure that Own() was called properly, right after the
  // widget was created. There should be a floating reference.
  DCHECK(g_object_is_floating(widget));

  // Sink the floating reference, we should now own this reference.
  g_object_ref_sink(widget);
  widget_ = widget;
}

void OwnedWidgetGtk::Destroy() {
  if (!widget_)
    return;

  GtkWidget* widget = widget_;
  widget_ = NULL;
  gtk_widget_destroy(widget);

  DCHECK(!g_object_is_floating(widget));
  // NOTE: Assumes some implementation details about glib internals.
  DCHECK_EQ(G_OBJECT(widget)->ref_count, 1U);
  g_object_unref(widget);
}

}  // namespace libgtk2ui
