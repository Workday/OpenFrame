// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/task_manager_gtk.h"

#include <gdk/gdkkeysyms.h>

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/memory_purger.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/ui/gtk/gtk_chrome_link_button.h"
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/gtk_theme_service.h"
#include "chrome/browser/ui/gtk/gtk_tree.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/gtk/menu_gtk.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "grit/chromium_strings.h"
#include "grit/ui_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/gtk/gtk_hig_constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/gtk_util.h"
#include "ui/gfx/image/image.h"

namespace {

// The task manager window default size.
const int kDefaultWidth = 460;
const int kDefaultHeight = 270;

// The resource id for the 'End process' button.
const gint kTaskManagerResponseKill = 1;

// The resource id for the 'Stats for nerds' link button.
const gint kTaskManagerAboutMemoryLink = 2;

// The resource id for the 'Purge Memory' button
const gint kTaskManagerPurgeMemory = 3;

enum TaskManagerColumn {
  kTaskManagerIcon,
  kTaskManagerTask,
  kTaskManagerProfileName,
  kTaskManagerSharedMem,
  kTaskManagerPrivateMem,
  kTaskManagerCPU,
  kTaskManagerNetwork,
  kTaskManagerProcessID,
  kTaskManagerJavaScriptMemory,
  kTaskManagerWebCoreImageCache,
  kTaskManagerWebCoreScriptsCache,
  kTaskManagerWebCoreCssCache,
  kTaskManagerVideoMemory,
  kTaskManagerFPS,
  kTaskManagerSqliteMemoryUsed,
  kTaskManagerGoatsTeleported,
  kTaskManagerColumnCount,
};

const TaskManagerColumn kTaskManagerLastVisibleColumn =
    kTaskManagerGoatsTeleported;

TaskManagerColumn TaskManagerResourceIDToColumnID(int id) {
  switch (id) {
    case IDS_TASK_MANAGER_TASK_COLUMN:
      return kTaskManagerTask;
    case IDS_TASK_MANAGER_PROFILE_NAME_COLUMN:
      return kTaskManagerProfileName;
    case IDS_TASK_MANAGER_SHARED_MEM_COLUMN:
      return kTaskManagerSharedMem;
    case IDS_TASK_MANAGER_PRIVATE_MEM_COLUMN:
      return kTaskManagerPrivateMem;
    case IDS_TASK_MANAGER_CPU_COLUMN:
      return kTaskManagerCPU;
    case IDS_TASK_MANAGER_NET_COLUMN:
      return kTaskManagerNetwork;
    case IDS_TASK_MANAGER_PROCESS_ID_COLUMN:
      return kTaskManagerProcessID;
    case IDS_TASK_MANAGER_JAVASCRIPT_MEMORY_ALLOCATED_COLUMN:
      return kTaskManagerJavaScriptMemory;
    case IDS_TASK_MANAGER_WEBCORE_IMAGE_CACHE_COLUMN:
      return kTaskManagerWebCoreImageCache;
    case IDS_TASK_MANAGER_WEBCORE_SCRIPTS_CACHE_COLUMN:
      return kTaskManagerWebCoreScriptsCache;
    case IDS_TASK_MANAGER_WEBCORE_CSS_CACHE_COLUMN:
      return kTaskManagerWebCoreCssCache;
    case IDS_TASK_MANAGER_VIDEO_MEMORY_COLUMN:
      return kTaskManagerVideoMemory;
    case IDS_TASK_MANAGER_FPS_COLUMN:
      return kTaskManagerFPS;
    case IDS_TASK_MANAGER_SQLITE_MEMORY_USED_COLUMN:
      return kTaskManagerSqliteMemoryUsed;
    case IDS_TASK_MANAGER_GOATS_TELEPORTED_COLUMN:
      return kTaskManagerGoatsTeleported;
    default:
      NOTREACHED();
      return static_cast<TaskManagerColumn>(-1);
  }
}

int TaskManagerColumnIDToResourceID(int id) {
  switch (id) {
    case kTaskManagerTask:
      return IDS_TASK_MANAGER_TASK_COLUMN;
    case kTaskManagerProfileName:
      return IDS_TASK_MANAGER_PROFILE_NAME_COLUMN;
    case kTaskManagerSharedMem:
      return IDS_TASK_MANAGER_SHARED_MEM_COLUMN;
    case kTaskManagerPrivateMem:
      return IDS_TASK_MANAGER_PRIVATE_MEM_COLUMN;
    case kTaskManagerCPU:
      return IDS_TASK_MANAGER_CPU_COLUMN;
    case kTaskManagerNetwork:
      return IDS_TASK_MANAGER_NET_COLUMN;
    case kTaskManagerProcessID:
      return IDS_TASK_MANAGER_PROCESS_ID_COLUMN;
    case kTaskManagerJavaScriptMemory:
      return IDS_TASK_MANAGER_JAVASCRIPT_MEMORY_ALLOCATED_COLUMN;
    case kTaskManagerWebCoreImageCache:
      return IDS_TASK_MANAGER_WEBCORE_IMAGE_CACHE_COLUMN;
    case kTaskManagerWebCoreScriptsCache:
      return IDS_TASK_MANAGER_WEBCORE_SCRIPTS_CACHE_COLUMN;
    case kTaskManagerWebCoreCssCache:
      return IDS_TASK_MANAGER_WEBCORE_CSS_CACHE_COLUMN;
    case kTaskManagerVideoMemory:
      return IDS_TASK_MANAGER_VIDEO_MEMORY_COLUMN;
    case kTaskManagerFPS:
      return IDS_TASK_MANAGER_FPS_COLUMN;
    case kTaskManagerSqliteMemoryUsed:
      return IDS_TASK_MANAGER_SQLITE_MEMORY_USED_COLUMN;
    case kTaskManagerGoatsTeleported:
      return IDS_TASK_MANAGER_GOATS_TELEPORTED_COLUMN;
    default:
      NOTREACHED();
      return -1;
  }
}

// Should be used for all gtk_tree_view functions that require a column index on
// input.
//
// We need colid - 1 because the gtk_tree_view function is asking for the
// column index, not the column id, and both kTaskManagerIcon and
// kTaskManagerTask are in the same column index, so all column IDs are off by
// one.
int TreeViewColumnIndexFromID(TaskManagerColumn colid) {
  return colid - 1;
}

// Shows or hides a treeview column.
void TreeViewColumnSetVisible(GtkWidget* treeview, TaskManagerColumn colid,
                              bool visible) {
  GtkTreeViewColumn* column = gtk_tree_view_get_column(
      GTK_TREE_VIEW(treeview), TreeViewColumnIndexFromID(colid));
  gtk_tree_view_column_set_visible(column, visible);
}

bool TreeViewColumnIsVisible(GtkWidget* treeview, TaskManagerColumn colid) {
  GtkTreeViewColumn* column = gtk_tree_view_get_column(
      GTK_TREE_VIEW(treeview), TreeViewColumnIndexFromID(colid));
  return gtk_tree_view_column_get_visible(column);
}

// The task column is special because it has an icon and it gets special
// treatment with respect to resizing the columns.
void TreeViewInsertTaskColumn(GtkWidget* treeview, int resid) {
  int colid = TaskManagerResourceIDToColumnID(resid);
  GtkTreeViewColumn* column = gtk_tree_view_column_new();
  gtk_tree_view_column_set_title(column,
                                 l10n_util::GetStringUTF8(resid).c_str());
  gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(treeview), colid);
  GtkCellRenderer* image_renderer = gtk_cell_renderer_pixbuf_new();
  gtk_tree_view_column_pack_start(column, image_renderer, FALSE);
  gtk_tree_view_column_add_attribute(column, image_renderer,
                                     "pixbuf", kTaskManagerIcon);
  GtkCellRenderer* text_renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
  gtk_tree_view_column_add_attribute(column, text_renderer, "markup", colid);
  gtk_tree_view_column_set_resizable(column, TRUE);
  // This is temporary: we'll turn expanding off after getting the size.
  gtk_tree_view_column_set_expand(column, TRUE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_sort_column_id(column, colid);
}

// Inserts a column with a column id of |colid| and |name|.
void TreeViewInsertColumnWithName(GtkWidget* treeview,
                                  TaskManagerColumn colid, const char* name) {
  GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_insert_column_with_attributes(
      GTK_TREE_VIEW(treeview), -1,
      name, renderer,
      "text", colid,
      NULL);
  GtkTreeViewColumn* column = gtk_tree_view_get_column(
      GTK_TREE_VIEW(treeview), TreeViewColumnIndexFromID(colid));
  gtk_tree_view_column_set_resizable(column, TRUE);
  gtk_tree_view_column_set_sort_column_id(column, colid);
}

// Loads the column name from |resid| and uses the corresponding
// TaskManagerColumn value as the column id to insert into the treeview.
void TreeViewInsertColumn(GtkWidget* treeview, int resid) {
  TreeViewInsertColumnWithName(treeview, TaskManagerResourceIDToColumnID(resid),
                               l10n_util::GetStringUTF8(resid).c_str());
}

// Set the current width of the column without forcing a fixed or maximum
// width as gtk_tree_view_column_set_[fixed|maximum]_width() would. This would
// basically be gtk_tree_view_column_set_width() except that there is no such
// function. It turns out that other applications have done similar hacks to do
// the same thing - search the web for that nonexistent function name! :)
void TreeViewColumnSetWidth(GtkTreeViewColumn* column, gint width) {
  column->width = width;
  column->resized_width = width;
  column->use_resized_width = TRUE;
  // Needed for use_resized_width to be effective.
  gtk_widget_queue_resize(column->tree_view);
}

}  // namespace

class TaskManagerGtk::ContextMenuController
    : public ui::SimpleMenuModel::Delegate {
 public:
  explicit ContextMenuController(TaskManagerGtk* task_manager)
      : task_manager_(task_manager) {
    menu_model_.reset(new ui::SimpleMenuModel(this));
    for (int i = kTaskManagerTask; i <= kTaskManagerLastVisibleColumn; i++) {
      menu_model_->AddCheckItemWithStringId(
          i, TaskManagerColumnIDToResourceID(i));
    }
    menu_.reset(new MenuGtk(NULL, menu_model_.get()));
  }

  virtual ~ContextMenuController() {}

  void RunMenu(const gfx::Point& point, guint32 event_time) {
    menu_->PopupAsContext(point, event_time);
  }

  void Cancel() {
    task_manager_ = NULL;
    menu_->Cancel();
  }

 private:
  // ui::SimpleMenuModel::Delegate implementation:
  virtual bool IsCommandIdEnabled(int command_id) const OVERRIDE {
    if (!task_manager_)
      return false;

    return true;
  }

  virtual bool IsCommandIdChecked(int command_id) const OVERRIDE {
    if (!task_manager_)
      return false;

    TaskManagerColumn colid = static_cast<TaskManagerColumn>(command_id);
    return TreeViewColumnIsVisible(task_manager_->treeview_, colid);
  }

  virtual bool GetAcceleratorForCommandId(
      int command_id,
      ui::Accelerator* accelerator) OVERRIDE {
    return false;
  }

  virtual void ExecuteCommand(int command_id, int event_flags) OVERRIDE {
    if (!task_manager_)
      return;

    TaskManagerColumn colid = static_cast<TaskManagerColumn>(command_id);
    bool visible = !TreeViewColumnIsVisible(task_manager_->treeview_, colid);
    TreeViewColumnSetVisible(task_manager_->treeview_, colid, visible);
  }

  // The model and view for the right click context menu.
  scoped_ptr<ui::SimpleMenuModel> menu_model_;
  scoped_ptr<MenuGtk> menu_;

  // The TaskManager the context menu was brought up for. Set to NULL when the
  // menu is canceled.
  TaskManagerGtk* task_manager_;

  DISALLOW_COPY_AND_ASSIGN(ContextMenuController);
};

TaskManagerGtk::TaskManagerGtk()
  : task_manager_(TaskManager::GetInstance()),
    model_(TaskManager::GetInstance()->model()),
    dialog_(NULL),
    treeview_(NULL),
    process_list_(NULL),
    process_count_(0),
    ignore_selection_changed_(false) {
  Init();
}

// static
TaskManagerGtk* TaskManagerGtk::instance_ = NULL;

TaskManagerGtk::~TaskManagerGtk() {
  model_->RemoveObserver(this);
  task_manager_->OnWindowClosed();

  gtk_accel_group_disconnect_key(accel_group_, GDK_w, GDK_CONTROL_MASK);
  gtk_window_remove_accel_group(GTK_WINDOW(dialog_), accel_group_);
  g_object_unref(accel_group_);
  accel_group_ = NULL;

  // Disconnect the destroy signal so it doesn't delete |this|.
  g_signal_handler_disconnect(G_OBJECT(dialog_), destroy_handler_id_);
  gtk_widget_destroy(dialog_);
}

////////////////////////////////////////////////////////////////////////////////
// TaskManagerGtk, TaskManagerModelObserver implementation:

void TaskManagerGtk::OnModelChanged() {
  // Nothing to do.
}

void TaskManagerGtk::OnItemsChanged(int start, int length) {
  GtkTreeIter iter;
  if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(process_list_), &iter,
                                     NULL, start)) {
    NOTREACHED() << "Can't get child " << start <<
        " from GTK_TREE_MODEL(process_list_)";
  }

  for (int i = start; i < start + length; i++) {
    SetRowDataFromModel(i, &iter);
    if (i != start + length - 1) {
      if (!gtk_tree_model_iter_next(GTK_TREE_MODEL(process_list_), &iter)) {
        NOTREACHED() << "Can't get next GtkTreeIter object from process_list_ "
                        "iterator at position " << i;
      }
    }
  }
}

void TaskManagerGtk::OnItemsAdded(int start, int length) {
  base::AutoReset<bool> autoreset(&ignore_selection_changed_, true);

  GtkTreeIter iter;
  if (start == 0) {
    gtk_list_store_prepend(process_list_, &iter);
  } else if (start >= process_count_) {
    gtk_list_store_append(process_list_, &iter);
  } else {
    GtkTreeIter sibling;
    gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(process_list_), &sibling,
                                  NULL, start);
    gtk_list_store_insert_before(process_list_, &iter, &sibling);
  }

  SetRowDataFromModel(start, &iter);

  for (int i = start + 1; i < start + length; i++) {
    gtk_list_store_insert_after(process_list_, &iter, &iter);
    SetRowDataFromModel(i, &iter);
  }

  process_count_ += length;
}

void TaskManagerGtk::OnItemsRemoved(int start, int length) {
  {
    base::AutoReset<bool> autoreset(&ignore_selection_changed_, true);

    GtkTreeIter iter;
    gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(process_list_), &iter,
                                  NULL, start);

    for (int i = 0; i < length; i++) {
      // |iter| is moved to the next valid node when the current node is
      // removed.
      gtk_list_store_remove(process_list_, &iter);
    }

    process_count_ -= length;
  }

  // It is possible that we have removed the current selection; run selection
  // changed to detect that case.
  OnSelectionChanged(gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview_)));
}

////////////////////////////////////////////////////////////////////////////////
// TaskManagerGtk, public:

void TaskManagerGtk::Close() {
  // Blow away our dialog - this will cause TaskManagerGtk to free itself.
  gtk_widget_destroy(dialog_);
  DCHECK(!instance_);
}

// static
void TaskManagerGtk::Show() {
  if (instance_) {
    // If there's a Task manager window open already, just activate it.
    gtk_util::PresentWindow(instance_->dialog_, 0);
  } else {
    instance_ = new TaskManagerGtk();
    instance_->model_->StartUpdating();
  }
}

////////////////////////////////////////////////////////////////////////////////
// TaskManagerGtk, private:

void TaskManagerGtk::Init() {
  dialog_ = gtk_dialog_new_with_buttons(
      l10n_util::GetStringUTF8(IDS_TASK_MANAGER_TITLE).c_str(),
      // Task Manager window is shared between all browsers.
      NULL,
      GTK_DIALOG_NO_SEPARATOR,
      NULL);

  // Allow browser windows to go in front of the task manager dialog in
  // metacity.
  gtk_window_set_type_hint(GTK_WINDOW(dialog_), GDK_WINDOW_TYPE_HINT_NORMAL);

  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kPurgeMemoryButton)) {
    gtk_dialog_add_button(GTK_DIALOG(dialog_),
        l10n_util::GetStringUTF8(IDS_TASK_MANAGER_PURGE_MEMORY).c_str(),
        kTaskManagerPurgeMemory);
  }

  if (browser_defaults::kShowCancelButtonInTaskManager) {
    gtk_dialog_add_button(GTK_DIALOG(dialog_),
        l10n_util::GetStringUTF8(IDS_CLOSE).c_str(),
        GTK_RESPONSE_DELETE_EVENT);
  }

  gtk_dialog_add_button(GTK_DIALOG(dialog_),
      l10n_util::GetStringUTF8(IDS_TASK_MANAGER_KILL).c_str(),
      kTaskManagerResponseKill);

  // The response button should not be sensitive when the dialog is first opened
  // because the selection is initially empty.
  gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog_),
                                    kTaskManagerResponseKill, FALSE);

  GtkWidget* link = gtk_chrome_link_button_new(
      l10n_util::GetStringUTF8(IDS_TASK_MANAGER_ABOUT_MEMORY_LINK).c_str());
  gtk_dialog_add_action_widget(GTK_DIALOG(dialog_), link,
                               kTaskManagerAboutMemoryLink);

  // Setting the link widget to secondary positions the button on the left side
  // of the action area (vice versa for RTL layout).
  GtkWidget* action_area = gtk_dialog_get_action_area(GTK_DIALOG(dialog_));
  gtk_button_box_set_child_secondary(GTK_BUTTON_BOX(action_area), link, TRUE);

  ConnectAccelerators();

  GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog_));
  gtk_box_set_spacing(GTK_BOX(content_area), ui::kContentAreaSpacing);

  destroy_handler_id_ = g_signal_connect(dialog_, "destroy",
                                         G_CALLBACK(OnDestroyThunk), this);
  g_signal_connect(dialog_, "response", G_CALLBACK(OnResponseThunk), this);
  g_signal_connect(dialog_, "button-press-event",
                   G_CALLBACK(OnButtonEventThunk), this);
  gtk_widget_add_events(dialog_,
                        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

  // Wrap the treeview widget in a scrolled window in order to have a frame.
  GtkWidget* scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled),
                                      GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  gtk_container_add(GTK_CONTAINER(content_area), scrolled);

  CreateTaskManagerTreeview();
  gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(treeview_), TRUE);
  g_signal_connect(treeview_, "row-activated",
                   G_CALLBACK(OnRowActivatedThunk), this);
  g_signal_connect(treeview_, "button-press-event",
                   G_CALLBACK(OnButtonEventThunk), this);

  // |selection| is owned by |treeview_|.
  GtkTreeSelection* selection = gtk_tree_view_get_selection(
      GTK_TREE_VIEW(treeview_));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);
  g_signal_connect(selection, "changed",
                   G_CALLBACK(OnSelectionChangedThunk), this);

  gtk_container_add(GTK_CONTAINER(scrolled), treeview_);

  SetInitialDialogSize();
  gtk_util::ShowDialog(dialog_);

  // If the model already has resources, we need to add them before we start
  // observing events.
  if (model_->ResourceCount() > 0)
    OnItemsAdded(0, model_->ResourceCount());

  model_->AddObserver(this);
}

void TaskManagerGtk::SetInitialDialogSize() {
  // Hook up to the realize event so we can size the task column to the
  // size of the leftover space after packing the other columns.
  g_signal_connect(treeview_, "realize",
                   G_CALLBACK(OnTreeViewRealizeThunk), this);
  // If we previously saved the dialog's bounds, use them.
  if (g_browser_process->local_state()) {
    const DictionaryValue* placement_pref =
        g_browser_process->local_state()->GetDictionary(
            prefs::kTaskManagerWindowPlacement);
    int top = 0, left = 0, bottom = 1, right = 1;
    if (placement_pref &&
        placement_pref->GetInteger("top", &top) &&
        placement_pref->GetInteger("left", &left) &&
        placement_pref->GetInteger("bottom", &bottom) &&
        placement_pref->GetInteger("right", &right)) {
      gtk_window_resize(GTK_WINDOW(dialog_),
                        std::max(1, right - left),
                        std::max(1, bottom - top));
      return;
    }
  }

  // Otherwise, just set a default size (GTK will override this if it's not
  // large enough to hold the window's contents).
  gtk_window_set_default_size(
      GTK_WINDOW(dialog_), kDefaultWidth, kDefaultHeight);
}

void TaskManagerGtk::ConnectAccelerators() {
  accel_group_ = gtk_accel_group_new();
  gtk_window_add_accel_group(GTK_WINDOW(dialog_), accel_group_);

  gtk_accel_group_connect(accel_group_,
                          GDK_w, GDK_CONTROL_MASK, GtkAccelFlags(0),
                          g_cclosure_new(G_CALLBACK(OnGtkAcceleratorThunk),
                                         this, NULL));
}

void TaskManagerGtk::CreateTaskManagerTreeview() {
  process_list_ = gtk_list_store_new(kTaskManagerColumnCount,
      GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING);

  // Support sorting on all columns.
  process_list_sort_ = gtk_tree_model_sort_new_with_model(
      GTK_TREE_MODEL(process_list_));
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerTask,
                                  ComparePage, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerTask,
                                  CompareProfileName, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerSharedMem,
                                  CompareSharedMemory, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerPrivateMem,
                                  ComparePrivateMemory, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerJavaScriptMemory,
                                  CompareV8Memory, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerCPU,
                                  CompareCPU, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerNetwork,
                                  CompareNetwork, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerProcessID,
                                  CompareProcessID, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerWebCoreImageCache,
                                  CompareWebCoreImageCache, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerWebCoreScriptsCache,
                                  CompareWebCoreScriptsCache, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerWebCoreCssCache,
                                  CompareWebCoreCssCache, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerVideoMemory,
                                  CompareVideoMemory, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerFPS,
                                  CompareFPS, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerSqliteMemoryUsed,
                                  CompareSqliteMemoryUsed, this, NULL);
  gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(process_list_sort_),
                                  kTaskManagerGoatsTeleported,
                                  CompareGoatsTeleported, this, NULL);
  treeview_ = gtk_tree_view_new_with_model(process_list_sort_);

  // Insert all the columns.
  TreeViewInsertTaskColumn(treeview_, IDS_TASK_MANAGER_TASK_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_PROFILE_NAME_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_SHARED_MEM_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_PRIVATE_MEM_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_CPU_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_NET_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_PROCESS_ID_COLUMN);
  TreeViewInsertColumn(treeview_,
                       IDS_TASK_MANAGER_JAVASCRIPT_MEMORY_ALLOCATED_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_WEBCORE_IMAGE_CACHE_COLUMN);
  TreeViewInsertColumn(treeview_,
                       IDS_TASK_MANAGER_WEBCORE_SCRIPTS_CACHE_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_WEBCORE_CSS_CACHE_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_VIDEO_MEMORY_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_FPS_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_SQLITE_MEMORY_USED_COLUMN);
  TreeViewInsertColumn(treeview_, IDS_TASK_MANAGER_GOATS_TELEPORTED_COLUMN);

  // Hide some columns by default.
  TreeViewColumnSetVisible(treeview_, kTaskManagerProfileName, false);
  TreeViewColumnSetVisible(treeview_, kTaskManagerSharedMem, false);
  TreeViewColumnSetVisible(treeview_, kTaskManagerJavaScriptMemory, false);
  TreeViewColumnSetVisible(treeview_, kTaskManagerWebCoreImageCache, false);
  TreeViewColumnSetVisible(treeview_, kTaskManagerWebCoreScriptsCache, false);
  TreeViewColumnSetVisible(treeview_, kTaskManagerWebCoreCssCache, false);
  TreeViewColumnSetVisible(treeview_, kTaskManagerVideoMemory, false);
  TreeViewColumnSetVisible(treeview_, kTaskManagerSqliteMemoryUsed, false);
  TreeViewColumnSetVisible(treeview_, kTaskManagerGoatsTeleported, false);

  g_object_unref(process_list_);
  g_object_unref(process_list_sort_);
}

std::string TaskManagerGtk::GetModelText(int row, int col_id) {
  return UTF16ToUTF8(model_->GetResourceById(row, col_id));
}

GdkPixbuf* TaskManagerGtk::GetModelIcon(int row) {
  SkBitmap icon = *model_->GetResourceIcon(row).bitmap();
  if (icon.pixelRef() ==
      ui::ResourceBundle::GetSharedInstance().GetImageNamed(
          IDR_DEFAULT_FAVICON).AsBitmap().pixelRef()) {
    return static_cast<GdkPixbuf*>(g_object_ref(
        GtkThemeService::GetDefaultFavicon(true).ToGdkPixbuf()));
  }

  return gfx::GdkPixbufFromSkBitmap(icon);
}

void TaskManagerGtk::SetRowDataFromModel(int row, GtkTreeIter* iter) {
  GdkPixbuf* icon = GetModelIcon(row);
  std::string task = GetModelText(row, IDS_TASK_MANAGER_TASK_COLUMN);
  std::string profile_name =
      GetModelText(row, IDS_TASK_MANAGER_PROFILE_NAME_COLUMN);
  gchar* task_markup = g_markup_escape_text(task.c_str(), task.length());
  std::string shared_mem =
      GetModelText(row, IDS_TASK_MANAGER_SHARED_MEM_COLUMN);
  std::string priv_mem = GetModelText(row, IDS_TASK_MANAGER_PRIVATE_MEM_COLUMN);
  std::string cpu = GetModelText(row, IDS_TASK_MANAGER_CPU_COLUMN);
  std::string net = GetModelText(row, IDS_TASK_MANAGER_NET_COLUMN);
  std::string procid = GetModelText(row, IDS_TASK_MANAGER_PROCESS_ID_COLUMN);

  // Querying the renderer metrics is slow as it has to do IPC, so only do it
  // when the columns are visible.
  std::string javascript_memory;
  if (TreeViewColumnIsVisible(treeview_, kTaskManagerJavaScriptMemory)) {
    javascript_memory =
        GetModelText(row, IDS_TASK_MANAGER_JAVASCRIPT_MEMORY_ALLOCATED_COLUMN);
  }
  std::string wk_img_cache;
  if (TreeViewColumnIsVisible(treeview_, kTaskManagerWebCoreImageCache)) {
    wk_img_cache =
        GetModelText(row, IDS_TASK_MANAGER_WEBCORE_IMAGE_CACHE_COLUMN);
  }
  std::string wk_scripts_cache;
  if (TreeViewColumnIsVisible(treeview_, kTaskManagerWebCoreScriptsCache)) {
    wk_scripts_cache =
        GetModelText(row, IDS_TASK_MANAGER_WEBCORE_SCRIPTS_CACHE_COLUMN);
  }
  std::string wk_css_cache;
  if (TreeViewColumnIsVisible(treeview_, kTaskManagerWebCoreCssCache)) {
    wk_css_cache =
        GetModelText(row, IDS_TASK_MANAGER_WEBCORE_CSS_CACHE_COLUMN);
  }
  std::string video_memory;
  if (TreeViewColumnIsVisible(treeview_, kTaskManagerVideoMemory))
    video_memory = GetModelText(row, IDS_TASK_MANAGER_VIDEO_MEMORY_COLUMN);
  std::string fps;
  if (TreeViewColumnIsVisible(treeview_, kTaskManagerFPS))
    fps = GetModelText(row, IDS_TASK_MANAGER_FPS_COLUMN);
  std::string sqlite_memory;
  if (TreeViewColumnIsVisible(treeview_, kTaskManagerSqliteMemoryUsed)) {
    sqlite_memory =
        GetModelText(row, IDS_TASK_MANAGER_SQLITE_MEMORY_USED_COLUMN);
  }

  std::string goats =
      GetModelText(row, IDS_TASK_MANAGER_GOATS_TELEPORTED_COLUMN);

  gtk_list_store_set(process_list_, iter,
                     kTaskManagerIcon, icon,
                     kTaskManagerTask, task_markup,
                     kTaskManagerProfileName, profile_name.c_str(),
                     kTaskManagerSharedMem, shared_mem.c_str(),
                     kTaskManagerPrivateMem, priv_mem.c_str(),
                     kTaskManagerCPU, cpu.c_str(),
                     kTaskManagerNetwork, net.c_str(),
                     kTaskManagerProcessID, procid.c_str(),
                     kTaskManagerJavaScriptMemory, javascript_memory.c_str(),
                     kTaskManagerWebCoreImageCache, wk_img_cache.c_str(),
                     kTaskManagerWebCoreScriptsCache, wk_scripts_cache.c_str(),
                     kTaskManagerWebCoreCssCache, wk_css_cache.c_str(),
                     kTaskManagerVideoMemory, video_memory.c_str(),
                     kTaskManagerFPS, fps.c_str(),
                     kTaskManagerSqliteMemoryUsed, sqlite_memory.c_str(),
                     kTaskManagerGoatsTeleported, goats.c_str(),
                     -1);
  g_object_unref(icon);
  g_free(task_markup);
}

void TaskManagerGtk::KillSelectedProcesses() {
  GtkTreeSelection* selection = gtk_tree_view_get_selection(
      GTK_TREE_VIEW(treeview_));

  GtkTreeModel* model;
  GList* paths = gtk_tree_selection_get_selected_rows(selection, &model);
  for (GList* item = paths; item; item = item->next) {
    GtkTreePath* path = gtk_tree_model_sort_convert_path_to_child_path(
        GTK_TREE_MODEL_SORT(process_list_sort_),
        reinterpret_cast<GtkTreePath*>(item->data));
    int row = gtk_tree::GetRowNumForPath(path);
    gtk_tree_path_free(path);
    task_manager_->KillProcess(row);
  }
  g_list_foreach(paths, reinterpret_cast<GFunc>(gtk_tree_path_free), NULL);
  g_list_free(paths);
}

void TaskManagerGtk::ShowContextMenu(const gfx::Point& point,
                                     guint32 event_time) {
  if (!menu_controller_.get())
    menu_controller_.reset(new ContextMenuController(this));

  menu_controller_->RunMenu(point, event_time);
}

void TaskManagerGtk::OnLinkActivated() {
  task_manager_->OpenAboutMemory(chrome::HOST_DESKTOP_TYPE_NATIVE);
}

gint TaskManagerGtk::CompareImpl(GtkTreeModel* model, GtkTreeIter* a,
                                 GtkTreeIter* b, int id) {
  int row1 = gtk_tree::GetRowNumForIter(model, b);
  int row2 = gtk_tree::GetRowNumForIter(model, a);

  // Otherwise, make sure grouped resources are shown together.
  TaskManagerModel::GroupRange group_range1 =
      model_->GetGroupRangeForResource(row1);
  TaskManagerModel::GroupRange group_range2 =
      model_->GetGroupRangeForResource(row2);

  if (group_range1 == group_range2) {
    // Sort within groups.
    // We want the first-in-group row at the top, whether we are sorting up or
    // down.
    GtkSortType sort_type;
    gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(process_list_sort_),
                                         NULL, &sort_type);
    if (row1 == group_range1.first)
      return sort_type == GTK_SORT_ASCENDING ? -1 : 1;
    if (row2 == group_range2.first)
      return sort_type == GTK_SORT_ASCENDING ? 1 : -1;

    return model_->CompareValues(row1, row2, id);
  } else {
    // Sort between groups.
    // Compare by the first-in-group rows so that the groups will stay together.
    return model_->CompareValues(group_range1.first, group_range2.first, id);
  }
}

void TaskManagerGtk::OnDestroy(GtkWidget* dialog) {
  instance_ = NULL;
  delete this;
}

void TaskManagerGtk::OnResponse(GtkWidget* dialog, int response_id) {
  if (response_id == GTK_RESPONSE_DELETE_EVENT) {
    // Store the dialog's size so we can restore it the next time it's opened.
    if (g_browser_process->local_state()) {
      gfx::Rect dialog_bounds = gtk_util::GetDialogBounds(GTK_WIDGET(dialog));

      DictionaryPrefUpdate update(g_browser_process->local_state(),
                                  prefs::kTaskManagerWindowPlacement);
      DictionaryValue* placement_pref = update.Get();
      // Note that we store left/top for consistency with Windows, but that we
      // *don't* restore them.
      placement_pref->SetInteger("left", dialog_bounds.x());
      placement_pref->SetInteger("top", dialog_bounds.y());
      placement_pref->SetInteger("right", dialog_bounds.right());
      placement_pref->SetInteger("bottom", dialog_bounds.bottom());
      placement_pref->SetBoolean("maximized", false);
    }

    instance_ = NULL;
    delete this;
  } else if (response_id == kTaskManagerResponseKill) {
    KillSelectedProcesses();
  } else if (response_id == kTaskManagerAboutMemoryLink) {
    OnLinkActivated();
  } else if (response_id == kTaskManagerPurgeMemory) {
    MemoryPurger::PurgeAll();
  }
}

void TaskManagerGtk::OnTreeViewRealize(GtkTreeView* treeview) {
  // Five columns show by default: the task column, the memory column, the CPU
  // column, the network column, and the FPS column. Initially we set the task
  // tolumn to take all the extra space, with the other columns being sized to
  // fit the column names. Here we turn off the expand property of the first
  // column (to make the table behave sanely when the user resizes it), and set
  // the effective sizes of all five default columns to the automatically chosen
  // sizes before any rows are added. This causes them to stay at those sizes
  // even if the data would overflow, preventing a horizontal scroll bar from
  // appearing due to the row data.
  static const TaskManagerColumn dfl_columns[] = {kTaskManagerPrivateMem,
                                                  kTaskManagerCPU,
                                                  kTaskManagerNetwork,
                                                  kTaskManagerFPS};
  GtkTreeViewColumn* column = NULL;
  gint width;
  for (size_t i = 0; i < arraysize(dfl_columns); ++i) {
    column = gtk_tree_view_get_column(treeview,
        TreeViewColumnIndexFromID(dfl_columns[i]));
    width = gtk_tree_view_column_get_width(column);
    TreeViewColumnSetWidth(column, width);
  }
  // Do the task column separately since it's a little different.
  column = gtk_tree_view_get_column(treeview,
      TreeViewColumnIndexFromID(kTaskManagerTask));
  width = gtk_tree_view_column_get_width(column);
  // Turn expanding back off to make resizing columns behave sanely.
  gtk_tree_view_column_set_expand(column, FALSE);
  TreeViewColumnSetWidth(column, width);
}

void TaskManagerGtk::OnSelectionChanged(GtkTreeSelection* selection) {
  if (ignore_selection_changed_)
    return;
  base::AutoReset<bool> autoreset(&ignore_selection_changed_, true);

  // The set of groups that should be selected.
  std::set<TaskManagerModel::GroupRange> ranges;
  bool selection_contains_browser_process = false;

  GtkTreeModel* model;
  GList* paths = gtk_tree_selection_get_selected_rows(selection, &model);
  for (GList* item = paths; item; item = item->next) {
    GtkTreePath* path = gtk_tree_model_sort_convert_path_to_child_path(
        GTK_TREE_MODEL_SORT(process_list_sort_),
        reinterpret_cast<GtkTreePath*>(item->data));
    int row = gtk_tree::GetRowNumForPath(path);
    gtk_tree_path_free(path);
    if (task_manager_->IsBrowserProcess(row))
      selection_contains_browser_process = true;
    ranges.insert(model_->GetGroupRangeForResource(row));
  }
  g_list_foreach(paths, reinterpret_cast<GFunc>(gtk_tree_path_free), NULL);
  g_list_free(paths);

  for (std::set<TaskManagerModel::GroupRange>::iterator iter = ranges.begin();
       iter != ranges.end(); ++iter) {
    for (int i = 0; i < iter->second; ++i) {
      GtkTreePath* child_path = gtk_tree_path_new_from_indices(iter->first + i,
                                                               -1);
      GtkTreePath* sort_path = gtk_tree_model_sort_convert_child_path_to_path(
        GTK_TREE_MODEL_SORT(process_list_sort_), child_path);
      gtk_tree_selection_select_path(selection, sort_path);
      gtk_tree_path_free(child_path);
      gtk_tree_path_free(sort_path);
    }
  }

  bool sensitive = (paths != NULL) && !selection_contains_browser_process;
  gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog_),
                                    kTaskManagerResponseKill, sensitive);
}

void TaskManagerGtk::OnRowActivated(GtkWidget* widget,
                                    GtkTreePath* path,
                                    GtkTreeViewColumn* column) {
  GtkTreePath* child_path = gtk_tree_model_sort_convert_path_to_child_path(
      GTK_TREE_MODEL_SORT(process_list_sort_), path);
  int row = gtk_tree::GetRowNumForPath(child_path);
  gtk_tree_path_free(child_path);
  task_manager_->ActivateProcess(row);
}

gboolean TaskManagerGtk::OnButtonEvent(GtkWidget* widget,
                                       GdkEventButton* event) {
  // GTK does menu on mouse-up while views does menu on mouse-down,
  // so this function can be called from either signal.
  if (event->button == 3) {
    ShowContextMenu(gfx::Point(event->x_root, event->y_root),
                    event->time);
    return TRUE;
  }

  return FALSE;
}

gboolean TaskManagerGtk::OnGtkAccelerator(GtkAccelGroup* accel_group,
                                          GObject* acceleratable,
                                          guint keyval,
                                          GdkModifierType modifier) {
  if (keyval == GDK_w && modifier == GDK_CONTROL_MASK) {
    // The GTK_RESPONSE_DELETE_EVENT response must be sent before the widget
    // is destroyed.  The deleted object will receive gtk signals otherwise.
    gtk_dialog_response(GTK_DIALOG(dialog_), GTK_RESPONSE_DELETE_EVENT);
  }

  return TRUE;
}

namespace chrome {

// Declared in browser_dialogs.h.
void ShowTaskManager(Browser* browser) {
  TaskManagerGtk::Show();
}

}  // namespace chrome
