// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/bookmarks/bookmark_utils_gtk.h"

#include "base/pickle.h"
#include "base/strings/string16.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_node_data.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/gtk/gtk_chrome_button.h"
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "grit/ui_strings.h"
#include "net/base/net_util.h"
#include "ui/base/dragdrop/gtk_dnd_util.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/gtk/gtk_screen_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/text/text_elider.h"
#include "ui/gfx/canvas_skia_paint.h"
#include "ui/gfx/font.h"
#include "ui/gfx/image/image.h"

namespace {

// Spacing between the favicon and the text.
const int kBarButtonPadding = 4;

// Used in gtk_selection_data_set(). (I assume from this parameter that gtk has
// to some really exotic hardware...)
const int kBitsInAByte = 8;

// Maximum number of characters on a bookmark button.
const size_t kMaxCharsOnAButton = 15;

// Maximum number of characters on a menu label.
const int kMaxCharsOnAMenuLabel = 50;

// Padding between the chrome button highlight border and the contents (favicon,
// text).
const int kButtonPaddingTop = 0;
const int kButtonPaddingBottom = 0;
const int kButtonPaddingLeft = 5;
const int kButtonPaddingRight = 0;

void* AsVoid(const BookmarkNode* node) {
  return const_cast<BookmarkNode*>(node);
}

// Creates the widget hierarchy for a bookmark button.
void PackButton(GdkPixbuf* pixbuf, const string16& title, bool ellipsize,
                GtkThemeService* provider, GtkWidget* button) {
  GtkWidget* former_child = gtk_bin_get_child(GTK_BIN(button));
  if (former_child)
    gtk_container_remove(GTK_CONTAINER(button), former_child);

  // We pack the button manually (rather than using gtk_button_set_*) so that
  // we can have finer control over its label.
  GtkWidget* image = gtk_image_new_from_pixbuf(pixbuf);

  GtkWidget* box = gtk_hbox_new(FALSE, kBarButtonPadding);
  gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

  std::string label_string = UTF16ToUTF8(title);
  if (!label_string.empty()) {
    GtkWidget* label = gtk_label_new(label_string.c_str());
    // Until we switch to vector graphics, force the font size.
    if (!provider->UsingNativeTheme())
      gtk_util::ForceFontSizePixels(label, 13.4);  // 13.4px == 10pt @ 96dpi

    // Ellipsize long bookmark names.
    if (ellipsize) {
      gtk_label_set_max_width_chars(GTK_LABEL(label), kMaxCharsOnAButton);
      gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    }

    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    bookmark_utils::SetButtonTextColors(label, provider);
  }

  GtkWidget* alignment = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
  // If we are not showing the label, don't set any padding, so that the icon
  // will just be centered.
  if (label_string.c_str()) {
    gtk_alignment_set_padding(GTK_ALIGNMENT(alignment),
        kButtonPaddingTop, kButtonPaddingBottom,
        kButtonPaddingLeft, kButtonPaddingRight);
  }
  gtk_container_add(GTK_CONTAINER(alignment), box);
  gtk_container_add(GTK_CONTAINER(button), alignment);

  gtk_widget_show_all(alignment);
}

const int kDragRepresentationWidth = 140;

struct DragRepresentationData {
 public:
  GdkPixbuf* favicon;
  string16 text;
  SkColor text_color;

  DragRepresentationData(GdkPixbuf* favicon,
                         const string16& text,
                         SkColor text_color)
      : favicon(favicon),
        text(text),
        text_color(text_color) {
    g_object_ref(favicon);
  }

  ~DragRepresentationData() {
    g_object_unref(favicon);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DragRepresentationData);
};

gboolean OnDragIconExpose(GtkWidget* sender,
                          GdkEventExpose* event,
                          DragRepresentationData* data) {
  // Clear the background.
  cairo_t* cr = gdk_cairo_create(event->window);
  gdk_cairo_rectangle(cr, &event->area);
  cairo_clip(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  gdk_cairo_set_source_pixbuf(cr, data->favicon, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);

  GtkAllocation allocation;
  gtk_widget_get_allocation(sender, &allocation);

  // Paint the title text.
  gfx::CanvasSkiaPaint canvas(event, false);
  int text_x = gdk_pixbuf_get_width(data->favicon) + kBarButtonPadding;
  int text_width = allocation.width - text_x;
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  const gfx::Font& base_font = rb.GetFont(ui::ResourceBundle::BaseFont);
  canvas.DrawStringInt(data->text, base_font, data->text_color,
                       text_x, 0, text_width, allocation.height,
                       gfx::Canvas::NO_SUBPIXEL_RENDERING);

  return TRUE;
}

void OnDragIconDestroy(GtkWidget* drag_icon,
                       DragRepresentationData* data) {
  g_object_unref(drag_icon);
  delete data;
}

}  // namespace

namespace bookmark_utils {

const char kBookmarkNode[] = "bookmark-node";

GdkPixbuf* GetPixbufForNode(const BookmarkNode* node, BookmarkModel* model,
                            bool native) {
  GdkPixbuf* pixbuf;

  if (node->is_url()) {
    const gfx::Image& favicon = model->GetFavicon(node);
    if (!favicon.IsEmpty()) {
      pixbuf = favicon.CopyGdkPixbuf();
    } else {
      pixbuf = GtkThemeService::GetDefaultFavicon(native).ToGdkPixbuf();
      g_object_ref(pixbuf);
    }
  } else {
    pixbuf = GtkThemeService::GetFolderIcon(native).ToGdkPixbuf();
    g_object_ref(pixbuf);
  }

  return pixbuf;
}

GtkWidget* GetDragRepresentation(GdkPixbuf* pixbuf,
                                 const string16& title,
                                 GtkThemeService* provider) {
  GtkWidget* window = gtk_window_new(GTK_WINDOW_POPUP);

  if (ui::IsScreenComposited() &&
      gtk_util::AddWindowAlphaChannel(window)) {
    DragRepresentationData* data = new DragRepresentationData(
        pixbuf, title,
        provider->GetColor(ThemeProperties::COLOR_BOOKMARK_TEXT));
    g_signal_connect(window, "expose-event", G_CALLBACK(OnDragIconExpose),
                     data);
    g_object_ref(window);
    g_signal_connect(window, "destroy", G_CALLBACK(OnDragIconDestroy), data);

    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    const gfx::Font& base_font = rb.GetFont(ui::ResourceBundle::BaseFont);
    gtk_widget_set_size_request(window, kDragRepresentationWidth,
                                base_font.GetHeight());
  } else {
    if (!provider->UsingNativeTheme()) {
      GdkColor color = provider->GetGdkColor(
          ThemeProperties::COLOR_TOOLBAR);
      gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &color);
    }
    gtk_widget_realize(window);

    GtkWidget* frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
    gtk_container_add(GTK_CONTAINER(window), frame);

    GtkWidget* floating_button = provider->BuildChromeButton();
    PackButton(pixbuf, title, true, provider, floating_button);
    gtk_container_add(GTK_CONTAINER(frame), floating_button);
    gtk_widget_show_all(frame);
  }

  return window;
}

GtkWidget* GetDragRepresentationForNode(const BookmarkNode* node,
                                        BookmarkModel* model,
                                        GtkThemeService* provider) {
  GdkPixbuf* pixbuf = GetPixbufForNode(
      node, model, provider->UsingNativeTheme());
  GtkWidget* widget = GetDragRepresentation(pixbuf, node->GetTitle(), provider);
  g_object_unref(pixbuf);
  return widget;
}

void ConfigureButtonForNode(const BookmarkNode* node, BookmarkModel* model,
                            GtkWidget* button, GtkThemeService* provider) {
  GdkPixbuf* pixbuf = bookmark_utils::GetPixbufForNode(
      node, model, provider->UsingNativeTheme());
  PackButton(pixbuf, node->GetTitle(), node != model->other_node(), provider,
             button);
  g_object_unref(pixbuf);

  std::string tooltip = BuildTooltipFor(node);
  if (!tooltip.empty())
    gtk_widget_set_tooltip_markup(button, tooltip.c_str());

  g_object_set_data(G_OBJECT(button), bookmark_utils::kBookmarkNode,
                    AsVoid(node));
}

void ConfigureAppsShortcutButton(GtkWidget* button, GtkThemeService* provider) {
  GdkPixbuf* pixbuf = ui::ResourceBundle::GetSharedInstance().
      GetNativeImageNamed(IDR_BOOKMARK_BAR_APPS_SHORTCUT,
                          ui::ResourceBundle::RTL_ENABLED).ToGdkPixbuf();
  const string16& label = l10n_util::GetStringUTF16(
      IDS_BOOKMARK_BAR_APPS_SHORTCUT_NAME);
  PackButton(pixbuf, label, false, provider, button);
}

std::string BuildTooltipFor(const BookmarkNode* node) {
  if (node->is_folder())
    return std::string();

  return gtk_util::BuildTooltipTitleFor(node->GetTitle(), node->url());
}

std::string BuildMenuLabelFor(const BookmarkNode* node) {
  // This breaks on word boundaries. Ideally we would break on character
  // boundaries.
  std::string elided_name = UTF16ToUTF8(
      ui::TruncateString(node->GetTitle(), kMaxCharsOnAMenuLabel));

  if (elided_name.empty()) {
    elided_name = UTF16ToUTF8(ui::TruncateString(
        UTF8ToUTF16(node->url().possibly_invalid_spec()),
        kMaxCharsOnAMenuLabel));
  }

  return elided_name;
}

const BookmarkNode* BookmarkNodeForWidget(GtkWidget* widget) {
  return reinterpret_cast<const BookmarkNode*>(
      g_object_get_data(G_OBJECT(widget), bookmark_utils::kBookmarkNode));
}

void SetButtonTextColors(GtkWidget* label, GtkThemeService* provider) {
  if (provider->UsingNativeTheme()) {
    gtk_util::SetLabelColor(label, NULL);
  } else {
    GdkColor color = provider->GetGdkColor(
        ThemeProperties::COLOR_BOOKMARK_TEXT);
    gtk_widget_modify_fg(label, GTK_STATE_NORMAL, &color);
    gtk_widget_modify_fg(label, GTK_STATE_INSENSITIVE, &color);

    // Because the prelight state is a white image that doesn't change by the
    // theme, force the text color to black when it would be used.
    gtk_widget_modify_fg(label, GTK_STATE_ACTIVE, &ui::kGdkBlack);
    gtk_widget_modify_fg(label, GTK_STATE_PRELIGHT, &ui::kGdkBlack);
  }
}

// DnD-related -----------------------------------------------------------------

int GetCodeMask(bool folder) {
  int rv = ui::CHROME_BOOKMARK_ITEM;
  if (!folder) {
    rv |= ui::TEXT_URI_LIST |
          ui::TEXT_HTML |
          ui::TEXT_PLAIN |
          ui::NETSCAPE_URL;
  }
  return rv;
}

void WriteBookmarkToSelection(const BookmarkNode* node,
                              GtkSelectionData* selection_data,
                              guint target_type,
                              Profile* profile) {
  DCHECK(node);
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(node);
  WriteBookmarksToSelection(nodes, selection_data, target_type, profile);
}

void WriteBookmarksToSelection(const std::vector<const BookmarkNode*>& nodes,
                               GtkSelectionData* selection_data,
                               guint target_type,
                               Profile* profile) {
  switch (target_type) {
    case ui::CHROME_BOOKMARK_ITEM: {
      BookmarkNodeData data(nodes);
      Pickle pickle;
      data.WriteToPickle(profile, &pickle);

      gtk_selection_data_set(selection_data,
                             gtk_selection_data_get_target(selection_data),
                             kBitsInAByte,
                             static_cast<const guchar*>(pickle.data()),
                             pickle.size());
      break;
    }
    case ui::NETSCAPE_URL: {
      // _NETSCAPE_URL format is URL + \n + title.
      std::string utf8_text = nodes[0]->url().spec() + "\n" +
          UTF16ToUTF8(nodes[0]->GetTitle());
      gtk_selection_data_set(selection_data,
                             gtk_selection_data_get_target(selection_data),
                             kBitsInAByte,
                             reinterpret_cast<const guchar*>(utf8_text.c_str()),
                             utf8_text.length());
      break;
    }
    case ui::TEXT_URI_LIST: {
      gchar** uris = reinterpret_cast<gchar**>(malloc(sizeof(gchar*) *
                                               (nodes.size() + 1)));
      for (size_t i = 0; i < nodes.size(); ++i) {
        // If the node is a folder, this will be empty. TODO(estade): figure out
        // if there are any ramifications to passing an empty URI. After a
        // little testing, it seems fine.
        const GURL& url = nodes[i]->url();
        // This const cast should be safe as gtk_selection_data_set_uris()
        // makes copies.
        uris[i] = const_cast<gchar*>(url.spec().c_str());
      }
      uris[nodes.size()] = NULL;

      gtk_selection_data_set_uris(selection_data, uris);
      free(uris);
      break;
    }
    case ui::TEXT_HTML: {
      std::string utf8_title = UTF16ToUTF8(nodes[0]->GetTitle());
      std::string utf8_html = base::StringPrintf("<a href=\"%s\">%s</a>",
                                                 nodes[0]->url().spec().c_str(),
                                                 utf8_title.c_str());
      gtk_selection_data_set(selection_data,
                             GetAtomForTarget(ui::TEXT_HTML),
                             kBitsInAByte,
                             reinterpret_cast<const guchar*>(utf8_html.data()),
                             utf8_html.size());
      break;
    }
    case ui::TEXT_PLAIN: {
      gtk_selection_data_set_text(selection_data,
                                  nodes[0]->url().spec().c_str(), -1);
      break;
    }
    default: {
      DLOG(ERROR) << "Unsupported drag get type!";
    }
  }
}

std::vector<const BookmarkNode*> GetNodesFromSelection(
    GdkDragContext* context,
    GtkSelectionData* selection_data,
    guint target_type,
    Profile* profile,
    gboolean* delete_selection_data,
    gboolean* dnd_success) {
  if (delete_selection_data)
    *delete_selection_data = FALSE;
  if (dnd_success)
    *dnd_success = FALSE;

  if (selection_data) {
    gint length = gtk_selection_data_get_length(selection_data);
    if (length > 0) {
      if (context && delete_selection_data &&
          context->action == GDK_ACTION_MOVE)
        *delete_selection_data = TRUE;

      switch (target_type) {
        case ui::CHROME_BOOKMARK_ITEM: {
          if (dnd_success)
            *dnd_success = TRUE;
          Pickle pickle(reinterpret_cast<const char*>(
              gtk_selection_data_get_data(selection_data)), length);
          BookmarkNodeData drag_data;
          drag_data.ReadFromPickle(&pickle);
          return drag_data.GetNodes(profile);
        }
        default: {
          DLOG(ERROR) << "Unsupported drag received type: " << target_type;
        }
      }
    }
  }

  return std::vector<const BookmarkNode*>();
}

bool CreateNewBookmarkFromNamedUrl(GtkSelectionData* selection_data,
    BookmarkModel* model, const BookmarkNode* parent, int idx) {
  GURL url;
  string16 title;
  if (!ui::ExtractNamedURL(selection_data, &url, &title))
    return false;

  model->AddURL(parent, idx, title, url);
  return true;
}

bool CreateNewBookmarksFromURIList(GtkSelectionData* selection_data,
    BookmarkModel* model, const BookmarkNode* parent, int idx) {
  std::vector<GURL> urls;
  ui::ExtractURIList(selection_data, &urls);
  for (size_t i = 0; i < urls.size(); ++i) {
    string16 title = GetNameForURL(urls[i]);
    model->AddURL(parent, idx++, title, urls[i]);
  }
  return true;
}

bool CreateNewBookmarkFromNetscapeURL(GtkSelectionData* selection_data,
    BookmarkModel* model, const BookmarkNode* parent, int idx) {
  GURL url;
  string16 title;
  if (!ui::ExtractNetscapeURL(selection_data, &url, &title))
    return false;

  model->AddURL(parent, idx, title, url);
  return true;
}

string16 GetNameForURL(const GURL& url) {
  if (url.is_valid()) {
    return net::GetSuggestedFilename(url,
                                     std::string(),
                                     std::string(),
                                     std::string(),
                                     std::string(),
                                     std::string());
  } else {
    return l10n_util::GetStringUTF16(IDS_APP_UNTITLED_SHORTCUT_FILE_NAME);
  }
}

}  // namespace bookmark_utils
