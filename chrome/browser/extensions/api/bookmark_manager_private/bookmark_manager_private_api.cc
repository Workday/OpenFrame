// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/bookmark_manager_private/bookmark_manager_private_api.h"

#include <vector>

#include "base/json/json_writer.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/bookmark_node_data.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/extensions/api/bookmark_manager_private/bookmark_manager_private_api_constants.h"
#include "chrome/browser/extensions/api/bookmarks/bookmark_api_constants.h"
#include "chrome/browser/extensions/api/bookmarks/bookmark_api_helpers.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_function_dispatcher.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_web_ui.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/bookmarks/bookmark_drag_drop.h"
#include "chrome/common/extensions/api/bookmark_manager_private.h"
#include "chrome/common/pref_names.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "content/public/browser/web_ui.h"
#include "extensions/browser/view_type_utils.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/webui/web_ui_util.h"

#if defined(OS_WIN)
#include "win8/util/win8_util.h"
#endif  // OS_WIN

namespace extensions {

namespace bookmark_keys = bookmark_api_constants;
namespace CanPaste = api::bookmark_manager_private::CanPaste;
namespace Copy = api::bookmark_manager_private::Copy;
namespace Cut = api::bookmark_manager_private::Cut;
namespace Drop = api::bookmark_manager_private::Drop;
namespace GetSubtree = api::bookmark_manager_private::GetSubtree;
namespace manager_keys = bookmark_manager_api_constants;
namespace Paste = api::bookmark_manager_private::Paste;
namespace RemoveTrees = api::bookmark_manager_private::RemoveTrees;
namespace SortChildren = api::bookmark_manager_private::SortChildren;
namespace StartDrag = api::bookmark_manager_private::StartDrag;

using content::WebContents;

namespace {

// Returns a single bookmark node from the argument ID.
// This returns NULL in case of failure.
const BookmarkNode* GetNodeFromString(
    BookmarkModel* model, const std::string& id_string) {
  int64 id;
  if (!base::StringToInt64(id_string, &id))
    return NULL;
  return model->GetNodeByID(id);
}

// Gets a vector of bookmark nodes from the argument list of IDs.
// This returns false in the case of failure.
bool GetNodesFromVector(BookmarkModel* model,
                        const std::vector<std::string>& id_strings,
                        std::vector<const BookmarkNode*>* nodes) {

  if (id_strings.empty())
    return false;

  for (size_t i = 0; i < id_strings.size(); ++i) {
    const BookmarkNode* node = GetNodeFromString(model, id_strings[i]);
    if (!node)
      return false;
    nodes->push_back(node);
  }

  return true;
}

// Recursively adds a node to a list. This is by used |BookmarkNodeDataToJSON|
// when the data comes from the current profile. In this case we have a
// BookmarkNode since we got the data from the current profile.
void AddNodeToList(base::ListValue* list, const BookmarkNode& node) {
  base::DictionaryValue* dict = new base::DictionaryValue();

  // Add id and parentId so we can associate the data with existing nodes on the
  // client side.
  std::string id_string = base::Int64ToString(node.id());
  dict->SetString(bookmark_keys::kIdKey, id_string);

  std::string parent_id_string = base::Int64ToString(node.parent()->id());
  dict->SetString(bookmark_keys::kParentIdKey, parent_id_string);

  if (node.is_url())
    dict->SetString(bookmark_keys::kUrlKey, node.url().spec());

  dict->SetString(bookmark_keys::kTitleKey, node.GetTitle());

  base::ListValue* children = new base::ListValue();
  for (int i = 0; i < node.child_count(); ++i)
    AddNodeToList(children, *node.GetChild(i));
  dict->Set(bookmark_keys::kChildrenKey, children);

  list->Append(dict);
}

// Recursively adds an element to a list. This is used by
// |BookmarkNodeDataToJSON| when the data comes from a different profile. When
// the data comes from a different profile we do not have any IDs or parent IDs.
void AddElementToList(base::ListValue* list,
                      const BookmarkNodeData::Element& element) {
  base::DictionaryValue* dict = new base::DictionaryValue();

  if (element.is_url)
    dict->SetString(bookmark_keys::kUrlKey, element.url.spec());

  dict->SetString(bookmark_keys::kTitleKey, element.title);

  base::ListValue* children = new base::ListValue();
  for (size_t i = 0; i < element.children.size(); ++i)
    AddElementToList(children, element.children[i]);
  dict->Set(bookmark_keys::kChildrenKey, children);

  list->Append(dict);
}

// Builds the JSON structure based on the BookmarksDragData.
void BookmarkNodeDataToJSON(Profile* profile, const BookmarkNodeData& data,
                            base::ListValue* args) {
  bool same_profile = data.IsFromProfile(profile);
  base::DictionaryValue* value = new base::DictionaryValue();
  value->SetBoolean(manager_keys::kSameProfileKey, same_profile);

  base::ListValue* list = new base::ListValue();
  if (same_profile) {
    std::vector<const BookmarkNode*> nodes = data.GetNodes(profile);
    for (size_t i = 0; i < nodes.size(); ++i)
      AddNodeToList(list, *nodes[i]);
  } else {
    // We do not have an node IDs when the data comes from a different profile.
    std::vector<BookmarkNodeData::Element> elements = data.elements;
    for (size_t i = 0; i < elements.size(); ++i)
      AddElementToList(list, elements[i]);
  }
  value->Set(manager_keys::kElementsKey, list);

  args->Append(value);
}

}  // namespace

BookmarkManagerPrivateEventRouter::BookmarkManagerPrivateEventRouter(
    Profile* profile,
    content::WebContents* web_contents)
    : profile_(profile),
      web_contents_(web_contents) {
  BookmarkTabHelper* bookmark_tab_helper =
      BookmarkTabHelper::FromWebContents(web_contents_);
  bookmark_tab_helper->set_bookmark_drag_delegate(this);
}

BookmarkManagerPrivateEventRouter::~BookmarkManagerPrivateEventRouter() {
  BookmarkTabHelper* bookmark_tab_helper =
      BookmarkTabHelper::FromWebContents(web_contents_);
  if (bookmark_tab_helper->bookmark_drag_delegate() == this)
    bookmark_tab_helper->set_bookmark_drag_delegate(NULL);
}

void BookmarkManagerPrivateEventRouter::DispatchEvent(
    const char* event_name,
    scoped_ptr<base::ListValue> args) {
  if (!ExtensionSystem::Get(profile_)->event_router())
    return;

  scoped_ptr<Event> event(new Event(event_name, args.Pass()));
  ExtensionSystem::Get(profile_)->event_router()->BroadcastEvent(event.Pass());
}

void BookmarkManagerPrivateEventRouter::DispatchDragEvent(
    const BookmarkNodeData& data,
    const char* event_name) {
  if (data.size() == 0)
    return;

  scoped_ptr<base::ListValue> args(new base::ListValue());
  BookmarkNodeDataToJSON(profile_, data, args.get());
  DispatchEvent(event_name, args.Pass());
}

void BookmarkManagerPrivateEventRouter::OnDragEnter(
    const BookmarkNodeData& data) {
  DispatchDragEvent(data, manager_keys::kOnBookmarkDragEnter);
}

void BookmarkManagerPrivateEventRouter::OnDragOver(
    const BookmarkNodeData& data) {
  // Intentionally empty since these events happens too often and floods the
  // message queue. We do not need this event for the bookmark manager anyway.
}

void BookmarkManagerPrivateEventRouter::OnDragLeave(
    const BookmarkNodeData& data) {
  DispatchDragEvent(data, manager_keys::kOnBookmarkDragLeave);
}

void BookmarkManagerPrivateEventRouter::OnDrop(const BookmarkNodeData& data) {
  DispatchDragEvent(data, manager_keys::kOnBookmarkDrop);

  // Make a copy that is owned by this instance.
  ClearBookmarkNodeData();
  bookmark_drag_data_ = data;
}

const BookmarkNodeData*
BookmarkManagerPrivateEventRouter::GetBookmarkNodeData() {
  if (bookmark_drag_data_.is_valid())
    return &bookmark_drag_data_;
  return NULL;
}

void BookmarkManagerPrivateEventRouter::ClearBookmarkNodeData() {
  bookmark_drag_data_.Clear();
}

bool ClipboardBookmarkManagerFunction::CopyOrCut(bool cut,
    const std::vector<std::string>& id_list) {
  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile());
  std::vector<const BookmarkNode*> nodes;
  EXTENSION_FUNCTION_VALIDATE(GetNodesFromVector(model, id_list, &nodes));
  bookmark_utils::CopyToClipboard(model, nodes, cut);
  return true;
}

bool BookmarkManagerPrivateCopyFunction::RunImpl() {
  scoped_ptr<Copy::Params> params(Copy::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  return CopyOrCut(false, params->id_list);
}

bool BookmarkManagerPrivateCutFunction::RunImpl() {
  if (!EditBookmarksEnabled())
    return false;

  scoped_ptr<Cut::Params> params(Cut::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  return CopyOrCut(true, params->id_list);
}

bool BookmarkManagerPrivatePasteFunction::RunImpl() {
  if (!EditBookmarksEnabled())
    return false;

  scoped_ptr<Paste::Params> params(Paste::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile());
  const BookmarkNode* parent_node = GetNodeFromString(model, params->parent_id);
  if (!parent_node) {
    error_ = bookmark_keys::kNoParentError;
    return false;
  }
  bool can_paste = bookmark_utils::CanPasteFromClipboard(parent_node);
  if (!can_paste)
    return false;

  // We want to use the highest index of the selected nodes as a destination.
  std::vector<const BookmarkNode*> nodes;
  // No need to test return value, if we got an empty list, we insert at end.
  if (params->selected_id_list)
    GetNodesFromVector(model, *params->selected_id_list, &nodes);
  int highest_index = -1;  // -1 means insert at end of list.
  for (size_t i = 0; i < nodes.size(); ++i) {
    // + 1 so that we insert after the selection.
    int index = parent_node->GetIndexOf(nodes[i]) + 1;
    if (index > highest_index)
      highest_index = index;
  }

  bookmark_utils::PasteFromClipboard(model, parent_node, highest_index);
  return true;
}

bool BookmarkManagerPrivateCanPasteFunction::RunImpl() {
  if (!EditBookmarksEnabled())
    return false;

  scoped_ptr<CanPaste::Params> params(CanPaste::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile());
  const BookmarkNode* parent_node = GetNodeFromString(model, params->parent_id);
  if (!parent_node) {
    error_ = bookmark_keys::kNoParentError;
    return false;
  }
  bool can_paste = bookmark_utils::CanPasteFromClipboard(parent_node);
  SetResult(new base::FundamentalValue(can_paste));
  return true;
}

bool BookmarkManagerPrivateSortChildrenFunction::RunImpl() {
  if (!EditBookmarksEnabled())
    return false;

  scoped_ptr<SortChildren::Params> params(SortChildren::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile());
  const BookmarkNode* parent_node = GetNodeFromString(model, params->parent_id);
  if (!parent_node) {
    error_ = bookmark_keys::kNoParentError;
    return false;
  }
  model->SortChildren(parent_node);
  return true;
}

bool BookmarkManagerPrivateGetStringsFunction::RunImpl() {
  base::DictionaryValue* localized_strings = new base::DictionaryValue();

  localized_strings->SetString("title",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_TITLE));
  localized_strings->SetString("search_button",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_SEARCH_BUTTON));
  localized_strings->SetString("organize_menu",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_ORGANIZE_MENU));
  localized_strings->SetString("show_in_folder",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_SHOW_IN_FOLDER));
  localized_strings->SetString("sort",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_SORT));
  localized_strings->SetString("import_menu",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_IMPORT_MENU));
  localized_strings->SetString("export_menu",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_EXPORT_MENU));
  localized_strings->SetString("rename_folder",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_RENAME_FOLDER));
  localized_strings->SetString("edit",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_EDIT));
  localized_strings->SetString("should_open_all",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_SHOULD_OPEN_ALL));
  localized_strings->SetString("open_incognito",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_OPEN_INCOGNITO));
  localized_strings->SetString("open_in_new_tab",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_OPEN_IN_NEW_TAB));
  localized_strings->SetString("open_in_new_window",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_OPEN_IN_NEW_WINDOW));
  localized_strings->SetString("add_new_bookmark",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_ADD_NEW_BOOKMARK));
  localized_strings->SetString("new_folder",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_NEW_FOLDER));
  localized_strings->SetString("open_all",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_OPEN_ALL));
  localized_strings->SetString("open_all_new_window",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_OPEN_ALL_NEW_WINDOW));
  localized_strings->SetString("open_all_incognito",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_OPEN_ALL_INCOGNITO));
  localized_strings->SetString("remove",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BAR_REMOVE));
  localized_strings->SetString("copy",
      l10n_util::GetStringUTF16(IDS_CONTENT_CONTEXT_COPY));
  localized_strings->SetString("cut",
      l10n_util::GetStringUTF16(IDS_CONTENT_CONTEXT_CUT));
  localized_strings->SetString("paste",
      l10n_util::GetStringUTF16(IDS_CONTENT_CONTEXT_PASTE));
  localized_strings->SetString("delete",
      l10n_util::GetStringUTF16(IDS_CONTENT_CONTEXT_DELETE));
  localized_strings->SetString("undo_delete",
      l10n_util::GetStringUTF16(IDS_UNDO_DELETE));
  localized_strings->SetString("new_folder_name",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_EDITOR_NEW_FOLDER_NAME));
  localized_strings->SetString("name_input_placeholder",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_NAME_INPUT_PLACE_HOLDER));
  localized_strings->SetString("url_input_placeholder",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_URL_INPUT_PLACE_HOLDER));
  localized_strings->SetString("invalid_url",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_INVALID_URL));
  localized_strings->SetString("recent",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_RECENT));
  localized_strings->SetString("search",
      l10n_util::GetStringUTF16(IDS_BOOKMARK_MANAGER_SEARCH));
  localized_strings->SetString("save",
      l10n_util::GetStringUTF16(IDS_SAVE));
  localized_strings->SetString("cancel",
      l10n_util::GetStringUTF16(IDS_CANCEL));

  webui::SetFontAndTextDirection(localized_strings);

  SetResult(localized_strings);

  // This is needed because unlike the rest of these functions, this class
  // inherits from AsyncFunction directly, rather than BookmarkFunction.
  SendResponse(true);

  return true;
}

bool BookmarkManagerPrivateStartDragFunction::RunImpl() {
  if (!EditBookmarksEnabled())
    return false;

  scoped_ptr<StartDrag::Params> params(StartDrag::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile());
  std::vector<const BookmarkNode*> nodes;
  EXTENSION_FUNCTION_VALIDATE(
      GetNodesFromVector(model, params->id_list, &nodes));

  WebContents* web_contents =
      WebContents::FromRenderViewHost(render_view_host_);
  if (GetViewType(web_contents) == VIEW_TYPE_TAB_CONTENTS) {
    WebContents* web_contents =
        dispatcher()->delegate()->GetAssociatedWebContents();
    CHECK(web_contents);
    chrome::DragBookmarks(profile(), nodes,
                          web_contents->GetView()->GetNativeView());

    return true;
  } else {
    NOTREACHED();
    return false;
  }
}

bool BookmarkManagerPrivateDropFunction::RunImpl() {
  if (!EditBookmarksEnabled())
    return false;

  scoped_ptr<Drop::Params> params(Drop::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile());

  const BookmarkNode* drop_parent = GetNodeFromString(model, params->parent_id);
  if (!drop_parent) {
    error_ = bookmark_keys::kNoParentError;
    return false;
  }

  int drop_index;
  if (params->index)
    drop_index = *params->index;
  else
    drop_index = drop_parent->child_count();

  WebContents* web_contents =
      WebContents::FromRenderViewHost(render_view_host_);
  if (GetViewType(web_contents) == VIEW_TYPE_TAB_CONTENTS) {
    WebContents* web_contents =
        dispatcher()->delegate()->GetAssociatedWebContents();
    CHECK(web_contents);
    ExtensionWebUI* web_ui =
        static_cast<ExtensionWebUI*>(web_contents->GetWebUI()->GetController());
    CHECK(web_ui);
    BookmarkManagerPrivateEventRouter* router =
        web_ui->bookmark_manager_private_event_router();

    DCHECK(router);
    const BookmarkNodeData* drag_data = router->GetBookmarkNodeData();
    if (drag_data == NULL) {
      NOTREACHED() <<"Somehow we're dropping null bookmark data";
      return false;
    }
    chrome::DropBookmarks(profile(), *drag_data, drop_parent, drop_index);

    router->ClearBookmarkNodeData();
    return true;
  } else {
    NOTREACHED();
    return false;
  }
}

bool BookmarkManagerPrivateGetSubtreeFunction::RunImpl() {
  scoped_ptr<GetSubtree::Params> params(GetSubtree::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile());
  const BookmarkNode* node = NULL;

  if (params->id == "") {
    node = model->root_node();
  } else {
    int64 id;
    if (!base::StringToInt64(params->id, &id)) {
      error_ = bookmark_keys::kInvalidIdError;
      return false;
    }
    node = model->GetNodeByID(id);
  }

  if (!node) {
    error_ = bookmark_keys::kNoNodeError;
    return false;
  }

  scoped_ptr<base::ListValue> json(new base::ListValue());
  if (params->folders_only)
    bookmark_api_helpers::AddNodeFoldersOnly(node, json.get(), true);
  else
    bookmark_api_helpers::AddNode(node, json.get(), true);
  SetResult(json.release());
  return true;
}

bool BookmarkManagerPrivateCanEditFunction::RunImpl() {
  PrefService* prefs = user_prefs::UserPrefs::Get(profile_);
  SetResult(new base::FundamentalValue(
      prefs->GetBoolean(prefs::kEditBookmarksEnabled)));
  return true;
}

bool BookmarkManagerPrivateRecordLaunchFunction::RunImpl() {
  bookmark_utils::RecordBookmarkLaunch(bookmark_utils::LAUNCH_MANAGER);
  return true;
}

bool BookmarkManagerPrivateCanOpenNewWindowsFunction::RunImpl() {
  bool can_open_new_windows = true;

#if defined(OS_WIN)
  if (win8::IsSingleWindowMetroMode())
    can_open_new_windows = false;
#endif  // OS_WIN

  SetResult(new base::FundamentalValue(can_open_new_windows));
  return true;
}

bool BookmarkManagerPrivateRemoveTreesFunction::RunImpl() {
  scoped_ptr<RemoveTrees::Params> params(RemoveTrees::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  BookmarkModel* model = BookmarkModelFactory::GetForProfile(profile());
  int64 id;
  for (size_t i = 0; i < params->id_list.size(); ++i) {
    if (!base::StringToInt64(params->id_list[i], &id)) {
      error_ = bookmark_api_constants::kInvalidIdError;
      return false;
    }
    if (!bookmark_api_helpers::RemoveNode(model, id, true, &error_))
      return false;
  }

  return true;
}

}  // namespace extensions
