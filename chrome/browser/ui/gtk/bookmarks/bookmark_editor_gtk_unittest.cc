// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/bookmarks/bookmark_editor_gtk.h"

#include <gtk/gtk.h>

#include <string>

#include "base/compiler_specific.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/ui/gtk/bookmarks/bookmark_tree_model.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Time;
using base::TimeDelta;
using bookmark_utils::GetTitleFromTreeIter;
using content::BrowserThread;

// Base class for bookmark editor tests. This class is a copy from
// bookmark_editor_view_unittest.cc, and all the tests in this file are
// GTK-ifications of the corresponding views tests. Testing here is really
// important because on Linux, we make round trip copies from chrome's
// BookmarkModel class to GTK's native GtkTreeStore.
class BookmarkEditorGtkTest : public testing::Test {
 public:
  BookmarkEditorGtkTest()
      : model_(NULL),
        ui_thread_(BrowserThread::UI, &message_loop_),
        file_thread_(BrowserThread::FILE, &message_loop_) {
  }

  virtual void SetUp() OVERRIDE {
    profile_.reset(new TestingProfile());
    profile_->CreateBookmarkModel(true);

    model_ = BookmarkModelFactory::GetForProfile(profile_.get());
    ui_test_utils::WaitForBookmarkModelToLoad(model_);

    AddTestData();
  }

  virtual void TearDown() OVERRIDE {
  }

 protected:
  std::string base_path() const { return "file:///c:/tmp/"; }

  const BookmarkNode* GetNode(const std::string& name) {
    return model_->GetMostRecentlyAddedNodeForURL(GURL(base_path() + name));
  }

  BookmarkModel* model_;
  scoped_ptr<TestingProfile> profile_;

 private:
  // Creates the following structure:
  // bookmark bar node
  //   a
  //   F1
  //    f1a
  //    F11
  //     f11a
  //   F2
  // other node
  //   oa
  //   OF1
  //     of1a
  // mobile node
  //   sa
  void AddTestData() {
    std::string test_base = base_path();

    model_->AddURL(model_->bookmark_bar_node(), 0, ASCIIToUTF16("a"),
                   GURL(test_base + "a"));
    const BookmarkNode* f1 =
        model_->AddFolder(model_->bookmark_bar_node(), 1, ASCIIToUTF16("F1"));
    model_->AddURL(f1, 0, ASCIIToUTF16("f1a"), GURL(test_base + "f1a"));
    const BookmarkNode* f11 = model_->AddFolder(f1, 1, ASCIIToUTF16("F11"));
    model_->AddURL(f11, 0, ASCIIToUTF16("f11a"), GURL(test_base + "f11a"));
    model_->AddFolder(model_->bookmark_bar_node(), 2, ASCIIToUTF16("F2"));

    // Children of the other node.
    model_->AddURL(model_->other_node(), 0, ASCIIToUTF16("oa"),
                   GURL(test_base + "oa"));
    const BookmarkNode* of1 =
        model_->AddFolder(model_->other_node(), 1, ASCIIToUTF16("OF1"));
    model_->AddURL(of1, 0, ASCIIToUTF16("of1a"), GURL(test_base + "of1a"));

    // Children of the mobile node.
    model_->AddURL(model_->mobile_node(), 0, ASCIIToUTF16("sa"),
                   GURL(test_base + "sa"));
  }

  base::MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_thread_;
};

// Makes sure the tree model matches that of the bookmark bar model.
TEST_F(BookmarkEditorGtkTest, ModelsMatch) {
  BookmarkEditorGtk editor(
      NULL,
      profile_.get(),
      NULL,
      BookmarkEditor::EditDetails::AddNodeInFolder(
          NULL, -1, GURL(), string16()),
      BookmarkEditor::SHOW_TREE);

  // The root should have two or three children, one for the bookmark bar node,
  // another for the 'other bookmarks' folder, and depending on the visib
  GtkTreeModel* store = GTK_TREE_MODEL(editor.tree_store_);
  GtkTreeIter toplevel;
  ASSERT_TRUE(gtk_tree_model_get_iter_first(store, &toplevel));
  GtkTreeIter bookmark_bar_node = toplevel;
  ASSERT_TRUE(gtk_tree_model_iter_next(store, &toplevel));
  GtkTreeIter other_node = toplevel;
  if (model_->mobile_node()->IsVisible()) {
    // If we have a mobile node, then the iterator should find one element after
    // "other bookmarks"
    ASSERT_TRUE(gtk_tree_model_iter_next(store, &toplevel));
    ASSERT_FALSE(gtk_tree_model_iter_next(store, &toplevel));
  } else {
    ASSERT_FALSE(gtk_tree_model_iter_next(store, &toplevel));
  }

  // The bookmark bar should have 2 nodes: folder F1 and F2.
  GtkTreeIter f1_iter;
  GtkTreeIter child;
  ASSERT_EQ(2, gtk_tree_model_iter_n_children(store, &bookmark_bar_node));
  ASSERT_TRUE(gtk_tree_model_iter_children(store, &child, &bookmark_bar_node));
  f1_iter = child;
  ASSERT_EQ("F1", UTF16ToUTF8(GetTitleFromTreeIter(store, &child)));
  ASSERT_TRUE(gtk_tree_model_iter_next(store, &child));
  ASSERT_EQ("F2", UTF16ToUTF8(GetTitleFromTreeIter(store, &child)));
  ASSERT_FALSE(gtk_tree_model_iter_next(store, &child));

  // F1 should have one child, F11
  ASSERT_EQ(1, gtk_tree_model_iter_n_children(store, &f1_iter));
  ASSERT_TRUE(gtk_tree_model_iter_children(store, &child, &f1_iter));
  ASSERT_EQ("F11", UTF16ToUTF8(GetTitleFromTreeIter(store, &child)));
  ASSERT_FALSE(gtk_tree_model_iter_next(store, &child));

  // Other node should have one child (OF1).
  ASSERT_EQ(1, gtk_tree_model_iter_n_children(store, &other_node));
  ASSERT_TRUE(gtk_tree_model_iter_children(store, &child, &other_node));
  ASSERT_EQ("OF1", UTF16ToUTF8(GetTitleFromTreeIter(store, &child)));
  ASSERT_FALSE(gtk_tree_model_iter_next(store, &child));
}

// Changes the title and makes sure parent/visual order doesn't change.
TEST_F(BookmarkEditorGtkTest, EditTitleKeepsPosition) {
  BookmarkEditorGtk editor(NULL, profile_.get(), NULL,
                           BookmarkEditor::EditDetails::EditNode(GetNode("a")),
                           BookmarkEditor::SHOW_TREE);
  gtk_entry_set_text(GTK_ENTRY(editor.name_entry_), "new_a");

  GtkTreeIter bookmark_bar_node;
  GtkTreeModel* store = GTK_TREE_MODEL(editor.tree_store_);
  ASSERT_TRUE(gtk_tree_model_get_iter_first(store, &bookmark_bar_node));
  editor.ApplyEdits(&bookmark_bar_node);

  const BookmarkNode* bb_node = model_->bookmark_bar_node();
  ASSERT_EQ(ASCIIToUTF16("new_a"), bb_node->GetChild(0)->GetTitle());
  // The URL shouldn't have changed.
  ASSERT_TRUE(GURL(base_path() + "a") == bb_node->GetChild(0)->url());
}

// Changes the url and makes sure parent/visual order doesn't change.
TEST_F(BookmarkEditorGtkTest, EditURLKeepsPosition) {
  Time node_time = GetNode("a")->date_added();
  BookmarkEditorGtk editor(NULL, profile_.get(), NULL,
                           BookmarkEditor::EditDetails::EditNode(GetNode("a")),
                           BookmarkEditor::SHOW_TREE);
  gtk_entry_set_text(GTK_ENTRY(editor.url_entry_),
                     GURL(base_path() + "new_a").spec().c_str());

  GtkTreeIter bookmark_bar_node;
  GtkTreeModel* store = GTK_TREE_MODEL(editor.tree_store_);
  ASSERT_TRUE(gtk_tree_model_get_iter_first(store, &bookmark_bar_node));
  editor.ApplyEdits(&bookmark_bar_node);

  const BookmarkNode* bb_node = model_->bookmark_bar_node();
  ASSERT_EQ(ASCIIToUTF16("a"), bb_node->GetChild(0)->GetTitle());
  // The URL should have changed.
  ASSERT_TRUE(GURL(base_path() + "new_a") == bb_node->GetChild(0)->url());
  ASSERT_TRUE(node_time == bb_node->GetChild(0)->date_added());
}

// Moves 'a' to be a child of the other node.
TEST_F(BookmarkEditorGtkTest, ChangeParent) {
  BookmarkEditorGtk editor(NULL, profile_.get(), NULL,
                           BookmarkEditor::EditDetails::EditNode(GetNode("a")),
                           BookmarkEditor::SHOW_TREE);

  GtkTreeModel* store = GTK_TREE_MODEL(editor.tree_store_);
  GtkTreeIter gtk_other_node;
  ASSERT_TRUE(gtk_tree_model_get_iter_first(store, &gtk_other_node));
  ASSERT_TRUE(gtk_tree_model_iter_next(store, &gtk_other_node));
  editor.ApplyEdits(&gtk_other_node);

  const BookmarkNode* other_node = model_->other_node();
  ASSERT_EQ(ASCIIToUTF16("a"), other_node->GetChild(2)->GetTitle());
  ASSERT_TRUE(GURL(base_path() + "a") == other_node->GetChild(2)->url());
}

// Moves 'a' to be a child of the other node.
// Moves 'a' to be a child of the other node and changes its url to new_a.
TEST_F(BookmarkEditorGtkTest, ChangeParentAndURL) {
  Time node_time = GetNode("a")->date_added();
  BookmarkEditorGtk editor(NULL, profile_.get(), NULL,
                           BookmarkEditor::EditDetails::EditNode(GetNode("a")),
                           BookmarkEditor::SHOW_TREE);

  gtk_entry_set_text(GTK_ENTRY(editor.url_entry_),
                     GURL(base_path() + "new_a").spec().c_str());

  GtkTreeModel* store = GTK_TREE_MODEL(editor.tree_store_);
  GtkTreeIter gtk_other_node;
  ASSERT_TRUE(gtk_tree_model_get_iter_first(store, &gtk_other_node));
  ASSERT_TRUE(gtk_tree_model_iter_next(store, &gtk_other_node));
  editor.ApplyEdits(&gtk_other_node);

  const BookmarkNode* other_node = model_->other_node();
  ASSERT_EQ(ASCIIToUTF16("a"), other_node->GetChild(2)->GetTitle());
  ASSERT_TRUE(GURL(base_path() + "new_a") == other_node->GetChild(2)->url());
  ASSERT_TRUE(node_time == other_node->GetChild(2)->date_added());
}

// Creates a new folder and moves a node to it.
TEST_F(BookmarkEditorGtkTest, MoveToNewParent) {
  BookmarkEditorGtk editor(NULL, profile_.get(), NULL,
                           BookmarkEditor::EditDetails::EditNode(GetNode("a")),
                           BookmarkEditor::SHOW_TREE);

  GtkTreeIter bookmark_bar_node;
  GtkTreeModel* store = GTK_TREE_MODEL(editor.tree_store_);
  ASSERT_TRUE(gtk_tree_model_get_iter_first(store, &bookmark_bar_node));

  // The bookmark bar should have 2 nodes: folder F1 and F2.
  GtkTreeIter f2_iter;
  ASSERT_EQ(2, gtk_tree_model_iter_n_children(store, &bookmark_bar_node));
  ASSERT_TRUE(gtk_tree_model_iter_children(store, &f2_iter,
                                           &bookmark_bar_node));
  ASSERT_TRUE(gtk_tree_model_iter_next(store, &f2_iter));

  // Create two nodes: "F21" as a child of "F2" and "F211" as a child of "F21".
  GtkTreeIter f21_iter;
  editor.AddNewFolder(&f2_iter, &f21_iter);
  gtk_tree_store_set(editor.tree_store_, &f21_iter,
                     bookmark_utils::FOLDER_NAME, "F21", -1);
  GtkTreeIter f211_iter;
  editor.AddNewFolder(&f21_iter, &f211_iter);
  gtk_tree_store_set(editor.tree_store_, &f211_iter,
                     bookmark_utils::FOLDER_NAME, "F211", -1);

  ASSERT_EQ(1, gtk_tree_model_iter_n_children(store, &f2_iter));

  editor.ApplyEdits(&f2_iter);

  const BookmarkNode* bb_node = model_->bookmark_bar_node();
  const BookmarkNode* mf2 = bb_node->GetChild(1);

  // F2 in the model should have two children now: F21 and the node edited.
  ASSERT_EQ(2, mf2->child_count());
  // F21 should be first.
  ASSERT_EQ(ASCIIToUTF16("F21"), mf2->GetChild(0)->GetTitle());
  // Then a.
  ASSERT_EQ(ASCIIToUTF16("a"), mf2->GetChild(1)->GetTitle());

  // F21 should have one child, F211.
  const BookmarkNode* mf21 = mf2->GetChild(0);
  ASSERT_EQ(1, mf21->child_count());
  ASSERT_EQ(ASCIIToUTF16("F211"), mf21->GetChild(0)->GetTitle());
}

// Brings up the editor, creating a new URL on the bookmark bar.
TEST_F(BookmarkEditorGtkTest, NewURL) {
  BookmarkEditorGtk editor(
      NULL,
      profile_.get(),
      NULL,
      BookmarkEditor::EditDetails::AddNodeInFolder(
          NULL, -1, GURL(), string16()),
      BookmarkEditor::SHOW_TREE);

  gtk_entry_set_text(GTK_ENTRY(editor.url_entry_),
                     GURL(base_path() + "a").spec().c_str());
  gtk_entry_set_text(GTK_ENTRY(editor.name_entry_), "new_a");

  GtkTreeIter bookmark_bar_node;
  GtkTreeModel* store = GTK_TREE_MODEL(editor.tree_store_);
  ASSERT_TRUE(gtk_tree_model_get_iter_first(store, &bookmark_bar_node));
  editor.ApplyEdits(&bookmark_bar_node);

  const BookmarkNode* bb_node = model_->bookmark_bar_node();
  ASSERT_EQ(4, bb_node->child_count());

  const BookmarkNode* new_node = bb_node->GetChild(3);
  EXPECT_EQ(ASCIIToUTF16("new_a"), new_node->GetTitle());
  EXPECT_TRUE(GURL(base_path() + "a") == new_node->url());
}

// Brings up the editor with no tree and modifies the url.
TEST_F(BookmarkEditorGtkTest, ChangeURLNoTree) {
  BookmarkEditorGtk editor(NULL, profile_.get(), NULL,
                           BookmarkEditor::EditDetails::EditNode(
                               model_->other_node()->GetChild(0)),
                           BookmarkEditor::NO_TREE);

  gtk_entry_set_text(GTK_ENTRY(editor.url_entry_),
                     GURL(base_path() + "a").spec().c_str());
  gtk_entry_set_text(GTK_ENTRY(editor.name_entry_), "new_a");

  editor.ApplyEdits(NULL);

  const BookmarkNode* other_node = model_->other_node();
  ASSERT_EQ(2, other_node->child_count());

  const BookmarkNode* new_node = other_node->GetChild(0);

  EXPECT_EQ(ASCIIToUTF16("new_a"), new_node->GetTitle());
  EXPECT_TRUE(GURL(base_path() + "a") == new_node->url());
}

// Brings up the editor with no tree and modifies only the title.
TEST_F(BookmarkEditorGtkTest, ChangeTitleNoTree) {
  BookmarkEditorGtk editor(NULL, profile_.get(), NULL,
                           BookmarkEditor::EditDetails::EditNode(
                               model_->other_node()->GetChild(0)),
                           BookmarkEditor::NO_TREE);
  gtk_entry_set_text(GTK_ENTRY(editor.name_entry_), "new_a");

  editor.ApplyEdits();

  const BookmarkNode* other_node = model_->other_node();
  ASSERT_EQ(2, other_node->child_count());

  const BookmarkNode* new_node = other_node->GetChild(0);
  EXPECT_EQ(ASCIIToUTF16("new_a"), new_node->GetTitle());
}
