// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/bookmarks/bookmark_model_test_utils.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

// Helper to verify the two given bookmark nodes.
// The IDs of the bookmark nodes are compared only if check_ids is true.
void AssertNodesEqual(const BookmarkNode* expected,
                      const BookmarkNode* actual,
                      bool check_ids) {
  ASSERT_TRUE(expected);
  ASSERT_TRUE(actual);
  if (check_ids)
    EXPECT_EQ(expected->id(), actual->id());
  EXPECT_EQ(expected->GetTitle(), actual->GetTitle());
  EXPECT_EQ(expected->type(), actual->type());
  EXPECT_TRUE(expected->date_added() == actual->date_added());
  if (expected->is_url()) {
    EXPECT_EQ(expected->url(), actual->url());
  } else {
    EXPECT_TRUE(expected->date_folder_modified() ==
                actual->date_folder_modified());
    ASSERT_EQ(expected->child_count(), actual->child_count());
    for (int i = 0; i < expected->child_count(); ++i)
      AssertNodesEqual(expected->GetChild(i), actual->GetChild(i), check_ids);
  }
}

// Helper function which does the actual work of creating the nodes for
// a particular level in the hierarchy.
std::string::size_type AddNodesFromString(BookmarkModel* model,
                                          const BookmarkNode* node,
                                          const std::string& model_string,
                                          std::string::size_type start_pos) {
  DCHECK(node);
  int index = node->child_count();
  static const std::string folder_tell(":[");
  std::string::size_type end_pos = model_string.find(' ', start_pos);
  while (end_pos != std::string::npos) {
    std::string::size_type part_length = end_pos - start_pos;
    std::string node_name = model_string.substr(start_pos, part_length);
    // Are we at the end of a folder group?
    if (node_name != "]") {
      // No, is it a folder?
      std::string tell;
      if (part_length > 2)
        tell = node_name.substr(part_length - 2, 2);
      if (tell == folder_tell) {
        node_name = node_name.substr(0, part_length - 2);
        const BookmarkNode* new_node =
            model->AddFolder(node, index, UTF8ToUTF16(node_name));
        end_pos = AddNodesFromString(model, new_node, model_string,
                                     end_pos + 1);
      } else {
        std::string url_string("http://");
        url_string += std::string(node_name.begin(), node_name.end());
        url_string += ".com";
        model->AddURL(node, index, UTF8ToUTF16(node_name), GURL(url_string));
        ++end_pos;
      }
      ++index;
      start_pos = end_pos;
      end_pos = model_string.find(' ', start_pos);
    } else {
      ++end_pos;
      break;
    }
  }
  return end_pos;
}

}  // namespace

// static
void BookmarkModelTestUtils::AssertModelsEqual(BookmarkModel* expected,
                                               BookmarkModel* actual,
                                               bool check_ids) {
  AssertNodesEqual(expected->bookmark_bar_node(),
                   actual->bookmark_bar_node(),
                   check_ids);
  AssertNodesEqual(expected->other_node(), actual->other_node(), check_ids);
  AssertNodesEqual(expected->mobile_node(), actual->mobile_node(), check_ids);
}

// static
std::string BookmarkModelTestUtils::ModelStringFromNode(
    const BookmarkNode* node) {
  // Since the children of the node are not available as a vector,
  // we'll just have to do it the hard way.
  int child_count = node->child_count();
  std::string child_string;
  for (int i = 0; i < child_count; ++i) {
    const BookmarkNode* child = node->GetChild(i);
    if (child->is_folder()) {
      child_string += UTF16ToUTF8(child->GetTitle()) + ":[ " +
          ModelStringFromNode(child) + "] ";
    } else {
      child_string += UTF16ToUTF8(child->GetTitle()) + " ";
    }
  }
  return child_string;
}

// static
void BookmarkModelTestUtils::AddNodesFromModelString(
    BookmarkModel* model,
    const BookmarkNode* node,
    const std::string& model_string) {
  DCHECK(node);
  std::string::size_type start_pos = 0;
  std::string::size_type end_pos =
      AddNodesFromString(model, node, model_string, start_pos);
  DCHECK(end_pos == std::string::npos);
}
