// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BOOKMARKS_BOOKMARK_MODEL_H_
#define CHROME_BROWSER_BOOKMARKS_BOOKMARK_MODEL_H_

#include <set>
#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/strings/string16.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/browser/bookmarks/bookmark_service.h"
#include "chrome/common/cancelable_task_tracker.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/base/models/tree_node_model.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

class BookmarkExpandedStateTracker;
class BookmarkIndex;
class BookmarkLoadDetails;
class BookmarkModel;
class BookmarkModelObserver;
class BookmarkStorage;
struct BookmarkTitleMatch;
class Profile;

namespace base {
class SequencedTaskRunner;
}

namespace chrome {
struct FaviconImageResult;
}

// BookmarkNode ---------------------------------------------------------------

// BookmarkNode contains information about a starred entry: title, URL, favicon,
// id and type. BookmarkNodes are returned from BookmarkModel.
class BookmarkNode : public ui::TreeNode<BookmarkNode> {
 public:
  enum Type {
    URL,
    FOLDER,
    BOOKMARK_BAR,
    OTHER_NODE,
    MOBILE
  };

  enum FaviconState {
    INVALID_FAVICON,
    LOADING_FAVICON,
    LOADED_FAVICON,
  };

  // Creates a new node with an id of 0 and |url|.
  explicit BookmarkNode(const GURL& url);
  // Creates a new node with |id| and |url|.
  BookmarkNode(int64 id, const GURL& url);

  virtual ~BookmarkNode();

  // Set the node's internal title. Note that this neither invokes observers
  // nor updates any bookmark model this node may be in. For that functionality,
  // BookmarkModel::SetTitle(..) should be used instead.
  virtual void SetTitle(const string16& title) OVERRIDE;

  // Returns an unique id for this node.
  // For bookmark nodes that are managed by the bookmark model, the IDs are
  // persisted across sessions.
  int64 id() const { return id_; }
  void set_id(int64 id) { id_ = id; }

  const GURL& url() const { return url_; }
  void set_url(const GURL& url) { url_ = url; }

  // Returns the favicon's URL. Returns an empty URL if there is no favicon
  // associated with this bookmark.
  const GURL& icon_url() const { return icon_url_; }

  Type type() const { return type_; }
  void set_type(Type type) { type_ = type; }

  // Returns the time the node was added.
  const base::Time& date_added() const { return date_added_; }
  void set_date_added(const base::Time& date) { date_added_ = date; }

  // Returns the last time the folder was modified. This is only maintained
  // for folders (including the bookmark bar and other folder).
  const base::Time& date_folder_modified() const {
    return date_folder_modified_;
  }
  void set_date_folder_modified(const base::Time& date) {
    date_folder_modified_ = date;
  }

  // Convenience for testing if this node represents a folder. A folder is a
  // node whose type is not URL.
  bool is_folder() const { return type_ != URL; }
  bool is_url() const { return type_ == URL; }

  bool is_favicon_loaded() const { return favicon_state_ == LOADED_FAVICON; }

  // Accessor method for controlling the visibility of a bookmark node/sub-tree.
  // Note that visibility is not propagated down the tree hierarchy so if a
  // parent node is marked as invisible, a child node may return "Visible". This
  // function is primarily useful when traversing the model to generate a UI
  // representation but we may want to suppress some nodes.
  virtual bool IsVisible() const;

  // Gets/sets/deletes value of |key| in the meta info represented by
  // |meta_info_str_|. Return true if key is found in meta info for gets or
  // meta info is changed indeed for sets/deletes.
  bool GetMetaInfo(const std::string& key, std::string* value) const;
  bool SetMetaInfo(const std::string& key, const std::string& value);
  bool DeleteMetaInfo(const std::string& key);
  void set_meta_info_str(const std::string& meta_info_str) {
    meta_info_str_.reserve(meta_info_str.size());
    meta_info_str_ = meta_info_str.substr(0);
  }
  const std::string& meta_info_str() const { return meta_info_str_; }

  // TODO(sky): Consider adding last visit time here, it'll greatly simplify
  // HistoryContentsProvider.

 private:
  friend class BookmarkModel;

  // A helper function to initialize various fields during construction.
  void Initialize(int64 id);

  // Called when the favicon becomes invalid.
  void InvalidateFavicon();

  // Sets the favicon's URL.
  void set_icon_url(const GURL& icon_url) {
    icon_url_ = icon_url;
  }

  const gfx::Image& favicon() const { return favicon_; }
  void set_favicon(const gfx::Image& icon) { favicon_ = icon; }

  FaviconState favicon_state() const { return favicon_state_; }
  void set_favicon_state(FaviconState state) { favicon_state_ = state; }

  CancelableTaskTracker::TaskId favicon_load_task_id() const {
    return favicon_load_task_id_;
  }
  void set_favicon_load_task_id(CancelableTaskTracker::TaskId id) {
    favicon_load_task_id_ = id;
  }

  // The unique identifier for this node.
  int64 id_;

  // The URL of this node. BookmarkModel maintains maps off this URL, so changes
  // to the URL must be done through the BookmarkModel.
  GURL url_;

  // The type of this node. See enum above.
  Type type_;

  // Date of when this node was created.
  base::Time date_added_;

  // Date of the last modification. Only used for folders.
  base::Time date_folder_modified_;

  // The favicon of this node.
  gfx::Image favicon_;

  // The URL of the node's favicon.
  GURL icon_url_;

  // The loading state of the favicon.
  FaviconState favicon_state_;

  // If not CancelableTaskTracker::kBadTaskId, it indicates we're loading the
  // favicon and the task is tracked by CancelabelTaskTracker.
  CancelableTaskTracker::TaskId favicon_load_task_id_;

  // A JSON string representing a DictionaryValue that stores arbitrary meta
  // information about the node. Use serialized format to save memory.
  std::string meta_info_str_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkNode);
};

// BookmarkPermanentNode -------------------------------------------------------

// Node used for the permanent folders (excluding the root).
class BookmarkPermanentNode : public BookmarkNode {
 public:
  explicit BookmarkPermanentNode(int64 id);
  virtual ~BookmarkPermanentNode();

  // WARNING: this code is used for other projects. Contact noyau@ for details.
  void set_visible(bool value) { visible_ = value; }

  // BookmarkNode overrides:
  virtual bool IsVisible() const OVERRIDE;

 private:
  bool visible_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkPermanentNode);
};

// BookmarkModel --------------------------------------------------------------

// BookmarkModel provides a directed acyclic graph of URLs and folders.
// Three graphs are provided for the three entry points: those on the 'bookmarks
// bar', those in the 'other bookmarks' folder and those in the 'mobile' folder.
//
// An observer may be attached to observe relevant events.
//
// You should NOT directly create a BookmarkModel, instead go through the
// BookmarkModelFactory.
class BookmarkModel : public content::NotificationObserver,
                      public BookmarkService,
                      public BrowserContextKeyedService {
 public:
  explicit BookmarkModel(Profile* profile);
  virtual ~BookmarkModel();

  // Invoked prior to destruction to release any necessary resources.
  virtual void Shutdown() OVERRIDE;

  // Loads the bookmarks. This is called upon creation of the
  // BookmarkModel. You need not invoke this directly.
  // All load operations will be executed on |task_runner|.
  void Load(const scoped_refptr<base::SequencedTaskRunner>& task_runner);

  // Returns true if the model finished loading.
  bool loaded() const { return loaded_; }

  // Returns the root node. The 'bookmark bar' node and 'other' node are
  // children of the root node.
  const BookmarkNode* root_node() { return &root_; }

  // Returns the 'bookmark bar' node. This is NULL until loaded.
  const BookmarkNode* bookmark_bar_node() { return bookmark_bar_node_; }

  // Returns the 'other' node. This is NULL until loaded.
  const BookmarkNode* other_node() { return other_node_; }

  // Returns the 'mobile' node. This is NULL until loaded.
  const BookmarkNode* mobile_node() { return mobile_node_; }

  bool is_root_node(const BookmarkNode* node) const { return node == &root_; }

  // Returns whether the given |node| is one of the permanent nodes - root node,
  // 'bookmark bar' node, 'other' node or 'mobile' node.
  bool is_permanent_node(const BookmarkNode* node) const {
    return node == &root_ ||
           node == bookmark_bar_node_ ||
           node == other_node_ ||
           node == mobile_node_;
  }

  // Returns the parent the last node was added to. This never returns NULL
  // (as long as the model is loaded).
  const BookmarkNode* GetParentForNewNodes();

  void AddObserver(BookmarkModelObserver* observer);
  void RemoveObserver(BookmarkModelObserver* observer);

  // Notifies the observers that an extensive set of changes is about to happen,
  // such as during import or sync, so they can delay any expensive UI updates
  // until it's finished.
  void BeginExtensiveChanges();
  void EndExtensiveChanges();

  // Returns true if this bookmark model is currently in a mode where extensive
  // changes might happen, such as for import and sync. This is helpful for
  // observers that are created after the mode has started, and want to check
  // state during their own initializer, such as the NTP.
  bool IsDoingExtensiveChanges() const { return extensive_changes_ > 0; }

  // Removes the node at the given |index| from |parent|. Removing a folder node
  // recursively removes all nodes. Observers are notified immediately.
  void Remove(const BookmarkNode* parent, int index);

  // Removes all the non-permanent bookmark nodes. Observers are only notified
  // when all nodes have been removed. There is no notification for individual
  // node removals.
  void RemoveAll();

  // Moves |node| to |new_parent| and inserts it at the given |index|.
  void Move(const BookmarkNode* node,
            const BookmarkNode* new_parent,
            int index);

  // Inserts a copy of |node| into |new_parent| at |index|.
  void Copy(const BookmarkNode* node,
            const BookmarkNode* new_parent,
            int index);

  // Returns the favicon for |node|. If the favicon has not yet been
  // loaded it is loaded and the observer of the model notified when done.
  const gfx::Image& GetFavicon(const BookmarkNode* node);

  // Sets the title of |node|.
  void SetTitle(const BookmarkNode* node, const string16& title);

  // Sets the URL of |node|.
  void SetURL(const BookmarkNode* node, const GURL& url);

  // Sets the date added time of |node|.
  void SetDateAdded(const BookmarkNode* node, base::Time date_added);

  // Returns the set of nodes with the |url|.
  void GetNodesByURL(const GURL& url, std::vector<const BookmarkNode*>* nodes);

  // Returns the most recently added node for the |url|. Returns NULL if |url|
  // is not bookmarked.
  const BookmarkNode* GetMostRecentlyAddedNodeForURL(const GURL& url);

  // Returns true if there are bookmarks, otherwise returns false.
  // This method is thread safe.
  bool HasBookmarks();

  // Returns true if there is a bookmark with the |url|.
  // This method is thread safe.
  // See BookmarkService for more details on this.
  virtual bool IsBookmarked(const GURL& url) OVERRIDE;

  // Returns all the bookmarked urls and their titles.
  // This method is thread safe.
  // See BookmarkService for more details on this.
  virtual void GetBookmarks(
      std::vector<BookmarkService::URLAndTitle>* urls) OVERRIDE;

  // Blocks until loaded; this is NOT invoked on the main thread.
  // See BookmarkService for more details on this.
  virtual void BlockTillLoaded() OVERRIDE;

  // Returns the node with |id|, or NULL if there is no node with |id|.
  const BookmarkNode* GetNodeByID(int64 id) const;

  // Adds a new folder node at the specified position.
  const BookmarkNode* AddFolder(const BookmarkNode* parent,
                                int index,
                                const string16& title);

  // Adds a url at the specified position.
  const BookmarkNode* AddURL(const BookmarkNode* parent,
                             int index,
                             const string16& title,
                             const GURL& url);

  // Adds a url with a specific creation date.
  const BookmarkNode* AddURLWithCreationTime(const BookmarkNode* parent,
                                             int index,
                                             const string16& title,
                                             const GURL& url,
                                             const base::Time& creation_time);

  // Sorts the children of |parent|, notifying observers by way of the
  // BookmarkNodeChildrenReordered method.
  void SortChildren(const BookmarkNode* parent);

  // Order the children of |parent| as specified in |ordered_nodes|.  This
  // function should only be used to reorder the child nodes of |parent| and
  // is not meant to move nodes between different parent. Notifies observers
  // using the BookmarkNodeChildrenReordered method.
  void ReorderChildren(const BookmarkNode* parent,
                       const std::vector<const BookmarkNode*>& ordered_nodes);

  // Sets the date when the folder was modified.
  void SetDateFolderModified(const BookmarkNode* node, const base::Time time);

  // Resets the 'date modified' time of the node to 0. This is used during
  // importing to exclude the newly created folders from showing up in the
  // combobox of most recently modified folders.
  void ResetDateFolderModified(const BookmarkNode* node);

  void GetBookmarksWithTitlesMatching(
      const string16& text,
      size_t max_count,
      std::vector<BookmarkTitleMatch>* matches);

  // Sets the store to NULL, making it so the BookmarkModel does not persist
  // any changes to disk. This is only useful during testing to speed up
  // testing.
  void ClearStore();

  // Returns the next node ID.
  int64 next_node_id() const { return next_node_id_; }

  // Returns the object responsible for tracking the set of expanded nodes in
  // the bookmark editor.
  BookmarkExpandedStateTracker* expanded_state_tracker() {
    return expanded_state_tracker_.get();
  }

  // Sets the visibility of one of the permanent nodes. This is set by sync.
  void SetPermanentNodeVisible(BookmarkNode::Type type, bool value);

  // Sets/deletes meta info of |node|.
  void SetNodeMetaInfo(const BookmarkNode* node,
                       const std::string& key,
                       const std::string& value);
  void DeleteNodeMetaInfo(const BookmarkNode* node,
                          const std::string& key);

 private:
  friend class BookmarkCodecTest;
  friend class BookmarkModelTest;
  friend class BookmarkStorage;

  // Used to order BookmarkNodes by URL.
  class NodeURLComparator {
   public:
    bool operator()(const BookmarkNode* n1, const BookmarkNode* n2) const {
      return n1->url() < n2->url();
    }
  };

  // Implementation of IsBookmarked. Before calling this the caller must obtain
  // a lock on |url_lock_|.
  bool IsBookmarkedNoLock(const GURL& url);

  // Removes the node from internal maps and recurses through all children. If
  // the node is a url, its url is added to removed_urls.
  //
  // This does NOT delete the node.
  void RemoveNode(BookmarkNode* node, std::set<GURL>* removed_urls);

  // Invoked when loading is finished. Sets |loaded_| and notifies observers.
  // BookmarkModel takes ownership of |details|.
  void DoneLoading(BookmarkLoadDetails* details);

  // Populates |nodes_ordered_by_url_set_| from root.
  void PopulateNodesByURL(BookmarkNode* node);

  // Removes the node from its parent, but does not delete it. No notifications
  // are sent. |removed_urls| is populated with the urls which no longer have
  // any bookmarks associated with them.
  // This method should be called after acquiring |url_lock_|.
  void RemoveNodeAndGetRemovedUrls(BookmarkNode* node,
                                   std::set<GURL>* removed_urls);

  // Removes the node from its parent, sends notification, and deletes it.
  // type specifies how the node should be removed.
  void RemoveAndDeleteNode(BookmarkNode* delete_me);

  // Notifies the history backend about urls of removed bookmarks.
  void NotifyHistoryAboutRemovedBookmarks(
      const std::set<GURL>& removed_bookmark_urls) const;

  // Adds the |node| at |parent| in the specified |index| and notifies its
  // observers.
  BookmarkNode* AddNode(BookmarkNode* parent,
                        int index,
                        BookmarkNode* node);

  // Implementation of GetNodeByID.
  const BookmarkNode* GetNodeByID(const BookmarkNode* node, int64 id) const;

  // Returns true if the parent and index are valid.
  bool IsValidIndex(const BookmarkNode* parent, int index, bool allow_end);

  // Creates one of the possible permanent nodes (bookmark bar node, other node
  // and mobile node) from |type|.
  BookmarkPermanentNode* CreatePermanentNode(BookmarkNode::Type type);

  // Notification that a favicon has finished loading. If we can decode the
  // favicon, FaviconLoaded is invoked.
  void OnFaviconDataAvailable(BookmarkNode* node,
                              const chrome::FaviconImageResult& image_result);

  // Invoked from the node to load the favicon. Requests the favicon from the
  // favicon service.
  void LoadFavicon(BookmarkNode* node);

  // Called to notify the observers that the favicon has been loaded.
  void FaviconLoaded(const BookmarkNode* node);

  // If we're waiting on a favicon for node, the load request is canceled.
  void CancelPendingFaviconLoadRequests(BookmarkNode* node);

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Generates and returns the next node ID.
  int64 generate_next_node_id();

  // Sets the maximum node ID to the given value.
  // This is used by BookmarkCodec to report the maximum ID after it's done
  // decoding since during decoding codec assigns node IDs.
  void set_next_node_id(int64 id) { next_node_id_ = id; }

  // Creates and returns a new BookmarkLoadDetails. It's up to the caller to
  // delete the returned object.
  BookmarkLoadDetails* CreateLoadDetails();

  content::NotificationRegistrar registrar_;

  Profile* profile_;

  // Whether the initial set of data has been loaded.
  bool loaded_;

  // The root node. This contains the bookmark bar node and the 'other' node as
  // children.
  BookmarkNode root_;

  BookmarkPermanentNode* bookmark_bar_node_;
  BookmarkPermanentNode* other_node_;
  BookmarkPermanentNode* mobile_node_;

  // The maximum ID assigned to the bookmark nodes in the model.
  int64 next_node_id_;

  // The observers.
  ObserverList<BookmarkModelObserver> observers_;

  // Set of nodes ordered by URL. This is not a map to avoid copying the
  // urls.
  // WARNING: |nodes_ordered_by_url_set_| is accessed on multiple threads. As
  // such, be sure and wrap all usage of it around |url_lock_|.
  typedef std::multiset<BookmarkNode*, NodeURLComparator> NodesOrderedByURLSet;
  NodesOrderedByURLSet nodes_ordered_by_url_set_;
  base::Lock url_lock_;

  // Used for loading favicons.
  CancelableTaskTracker cancelable_task_tracker_;

  // Reads/writes bookmarks to disk.
  scoped_refptr<BookmarkStorage> store_;

  scoped_ptr<BookmarkIndex> index_;

  base::WaitableEvent loaded_signal_;

  // See description of IsDoingExtensiveChanges above.
  int extensive_changes_;

  scoped_ptr<BookmarkExpandedStateTracker> expanded_state_tracker_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkModel);
};

#endif  // CHROME_BROWSER_BOOKMARKS_BOOKMARK_MODEL_H_
