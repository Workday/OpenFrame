// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "components/mus/common/util.h"
#include "components/mus/public/cpp/tests/window_server_test_base.h"
#include "components/mus/public/cpp/window_observer.h"
#include "components/mus/public/cpp/window_tree_connection.h"
#include "components/mus/public/cpp/window_tree_delegate.h"
#include "mojo/application/public/cpp/application_connection.h"
#include "mojo/application/public/cpp/application_impl.h"
#include "mojo/application/public/cpp/application_test_base.h"
#include "mojo/converters/geometry/geometry_type_converters.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/mojo/geometry/geometry_util.h"

namespace mus {

namespace ws {

namespace {

int ValidIndexOf(const Window::Children& windows, Window* window) {
  Window::Children::const_iterator it =
      std::find(windows.begin(), windows.end(), window);
  return (it != windows.end()) ? (it - windows.begin()) : -1;
}

class BoundsChangeObserver : public WindowObserver {
 public:
  explicit BoundsChangeObserver(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~BoundsChangeObserver() override { window_->RemoveObserver(this); }

 private:
  // Overridden from WindowObserver:
  void OnWindowBoundsChanged(Window* window,
                             const gfx::Rect& old_bounds,
                             const gfx::Rect& new_bounds) override {
    DCHECK_EQ(window, window_);
    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(BoundsChangeObserver);
};

// Wait until the bounds of the supplied window change; returns false on
// timeout.
bool WaitForBoundsToChange(Window* window) {
  BoundsChangeObserver observer(window);
  return WindowServerTestBase::DoRunLoopWithTimeout();
}

class ClientAreaChangeObserver : public WindowObserver {
 public:
  explicit ClientAreaChangeObserver(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~ClientAreaChangeObserver() override { window_->RemoveObserver(this); }

 private:
  // Overridden from WindowObserver:
  void OnWindowClientAreaChanged(Window* window,
                                 const gfx::Insets& old_client_area) override {
    DCHECK_EQ(window, window_);
    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(ClientAreaChangeObserver);
};

// Wait until the bounds of the supplied window change; returns false on
// timeout.
bool WaitForClientAreaToChange(Window* window) {
  ClientAreaChangeObserver observer(window);
  return WindowServerTestBase::DoRunLoopWithTimeout();
}

// Spins a run loop until the tree beginning at |root| has |tree_size| windows
// (including |root|).
class TreeSizeMatchesObserver : public WindowObserver {
 public:
  TreeSizeMatchesObserver(Window* tree, size_t tree_size)
      : tree_(tree), tree_size_(tree_size) {
    tree_->AddObserver(this);
  }
  ~TreeSizeMatchesObserver() override { tree_->RemoveObserver(this); }

  bool IsTreeCorrectSize() { return CountWindows(tree_) == tree_size_; }

 private:
  // Overridden from WindowObserver:
  void OnTreeChanged(const TreeChangeParams& params) override {
    if (IsTreeCorrectSize())
      EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  size_t CountWindows(const Window* window) const {
    size_t count = 1;
    Window::Children::const_iterator it = window->children().begin();
    for (; it != window->children().end(); ++it)
      count += CountWindows(*it);
    return count;
  }

  Window* tree_;
  size_t tree_size_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(TreeSizeMatchesObserver);
};

// Wait until |window| has |tree_size| descendants; returns false on timeout.
// The count includes |window|. For example, if you want to wait for |window| to
// have a single child, use a |tree_size| of 2.
bool WaitForTreeSizeToMatch(Window* window, size_t tree_size) {
  TreeSizeMatchesObserver observer(window, tree_size);
  return observer.IsTreeCorrectSize() ||
         WindowServerTestBase::DoRunLoopWithTimeout();
}

class OrderChangeObserver : public WindowObserver {
 public:
  OrderChangeObserver(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~OrderChangeObserver() override { window_->RemoveObserver(this); }

 private:
  // Overridden from WindowObserver:
  void OnWindowReordered(Window* window,
                         Window* relative_window,
                         mojom::OrderDirection direction) override {
    DCHECK_EQ(window, window_);
    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(OrderChangeObserver);
};

// Wait until |window|'s tree size matches |tree_size|; returns false on
// timeout.
bool WaitForOrderChange(WindowTreeConnection* connection, Window* window) {
  OrderChangeObserver observer(window);
  return WindowServerTestBase::DoRunLoopWithTimeout();
}

// Tracks a window's destruction. Query is_valid() for current state.
class WindowTracker : public WindowObserver {
 public:
  explicit WindowTracker(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~WindowTracker() override {
    if (window_)
      window_->RemoveObserver(this);
  }

  bool is_valid() const { return !!window_; }

 private:
  // Overridden from WindowObserver:
  void OnWindowDestroyed(Window* window) override {
    DCHECK_EQ(window, window_);
    window_ = nullptr;
  }

  Window* window_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(WindowTracker);
};

}  // namespace

// WindowServer
// -----------------------------------------------------------------

struct EmbedResult {
  EmbedResult(WindowTreeConnection* connection, ConnectionSpecificId id)
      : connection(connection), connection_id(id) {}
  EmbedResult() : connection(nullptr), connection_id(0) {}

  WindowTreeConnection* connection;

  // The id supplied to the callback from OnEmbed(). Depending upon the
  // access policy this may or may not match the connection id of
  // |connection|.
  ConnectionSpecificId connection_id;
};

// These tests model synchronization of two peer connections to the window
// manager
// service, that are given access to some root window.

class WindowServerTest : public WindowServerTestBase {
 public:
  WindowServerTest() {}

  Window* NewVisibleWindow(Window* parent, WindowTreeConnection* connection) {
    Window* window = connection->NewWindow();
    window->SetVisible(true);
    parent->AddChild(window);
    return window;
  }

  // Embeds another version of the test app @ window. This runs a run loop until
  // a response is received, or a timeout. On success the new WindowServer is
  // returned.
  EmbedResult Embed(Window* window) {
    return Embed(window, mus::mojom::WindowTree::ACCESS_POLICY_DEFAULT);
  }

  EmbedResult Embed(Window* window, uint32_t access_policy_bitmask) {
    DCHECK(!embed_details_);
    embed_details_.reset(new EmbedDetails);
    window->Embed(ConnectToApplicationAndGetWindowServerClient(),
                  access_policy_bitmask,
                  base::Bind(&WindowServerTest::EmbedCallbackImpl,
                             base::Unretained(this)));
    embed_details_->waiting = true;
    if (!WindowServerTestBase::DoRunLoopWithTimeout())
      return EmbedResult();
    const EmbedResult result(embed_details_->connection,
                             embed_details_->connection_id);
    embed_details_.reset();
    return result;
  }

  // Establishes a connection to this application and asks for a
  // WindowTreeClient.
  mus::mojom::WindowTreeClientPtr
  ConnectToApplicationAndGetWindowServerClient() {
    mus::mojom::WindowTreeClientPtr client;
    application_impl()->ConnectToService(application_impl()->url(), &client);
    return client.Pass();
  }

  // WindowServerTestBase:
  void OnEmbed(Window* root) override {
    if (!embed_details_) {
      WindowServerTestBase::OnEmbed(root);
      return;
    }

    embed_details_->connection = root->connection();
    if (embed_details_->callback_run)
      EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

 private:
  // Used to track the state of a call to window->Embed().
  struct EmbedDetails {
    EmbedDetails()
        : callback_run(false),
          result(false),
          waiting(false),
          connection_id(0),
          connection(nullptr) {}

    // The callback supplied to Embed() was received.
    bool callback_run;

    // The boolean supplied to the Embed() callback.
    bool result;

    // Whether a MessageLoop is running.
    bool waiting;

    // Connection id supplied to the Embed() callback.
    ConnectionSpecificId connection_id;

    // The WindowTreeConnection that resulted from the Embed(). null if |result|
    // is false.
    WindowTreeConnection* connection;
  };

  void EmbedCallbackImpl(bool result, ConnectionSpecificId connection_id) {
    embed_details_->callback_run = true;
    embed_details_->result = result;
    embed_details_->connection_id = connection_id;
    if (embed_details_->waiting && (!result || embed_details_->connection))
      EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  scoped_ptr<EmbedDetails> embed_details_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(WindowServerTest);
};

TEST_F(WindowServerTest, RootWindow) {
  ASSERT_NE(nullptr, window_manager());
  EXPECT_NE(nullptr, window_manager()->GetRoot());
}

TEST_F(WindowServerTest, Embed) {
  Window* window = window_manager()->NewWindow();
  ASSERT_NE(nullptr, window);
  window->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window);
  WindowTreeConnection* embedded = Embed(window).connection;
  ASSERT_NE(nullptr, embedded);

  Window* window_in_embedded = embedded->GetRoot();
  ASSERT_NE(nullptr, window_in_embedded);
  EXPECT_EQ(window->id(), window_in_embedded->id());
  EXPECT_EQ(nullptr, window_in_embedded->parent());
  EXPECT_TRUE(window_in_embedded->children().empty());
}

// Window manager has two windows, N1 and N11. Embeds A at N1. A should not see
// N11.
TEST_F(WindowServerTest, EmbeddedDoesntSeeChild) {
  Window* window = window_manager()->NewWindow();
  ASSERT_NE(nullptr, window);
  window->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window);
  Window* nested = window_manager()->NewWindow();
  ASSERT_NE(nullptr, nested);
  nested->SetVisible(true);
  window->AddChild(nested);

  WindowTreeConnection* embedded = Embed(window).connection;
  ASSERT_NE(nullptr, embedded);
  Window* window_in_embedded = embedded->GetRoot();
  EXPECT_EQ(window->id(), window_in_embedded->id());
  EXPECT_EQ(nullptr, window_in_embedded->parent());
  EXPECT_TRUE(window_in_embedded->children().empty());
}

// TODO(beng): write a replacement test for the one that once existed here:
// This test validates the following scenario:
// -  a window originating from one connection
// -  a window originating from a second connection
// +  the connection originating the window is destroyed
// -> the window should still exist (since the second connection is live) but
//    should be disconnected from any windows.
// http://crbug.com/396300
//
// TODO(beng): The new test should validate the scenario as described above
//             except that the second connection still has a valid tree.

// Verifies that bounds changes applied to a window hierarchy in one connection
// are reflected to another.
TEST_F(WindowServerTest, SetBounds) {
  Window* window = window_manager()->NewWindow();
  window->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window);
  WindowTreeConnection* embedded = Embed(window).connection;
  ASSERT_NE(nullptr, embedded);

  Window* window_in_embedded = embedded->GetWindowById(window->id());
  EXPECT_EQ(window->bounds(), window_in_embedded->bounds());

  window->SetBounds(gfx::Rect(0, 0, 100, 100));
  ASSERT_TRUE(WaitForBoundsToChange(window_in_embedded));
  EXPECT_TRUE(window->bounds() == window_in_embedded->bounds());
}

// Verifies that bounds changes applied to a window owned by a different
// connection are refused.
TEST_F(WindowServerTest, SetBoundsSecurity) {
  Window* window = window_manager()->NewWindow();
  window->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window);
  WindowTreeConnection* embedded = Embed(window).connection;
  ASSERT_NE(nullptr, embedded);

  Window* window_in_embedded = embedded->GetWindowById(window->id());
  window->SetBounds(gfx::Rect(0, 0, 800, 600));
  ASSERT_TRUE(WaitForBoundsToChange(window_in_embedded));

  window_in_embedded->SetBounds(gfx::Rect(0, 0, 1024, 768));
  // Bounds change is initially accepted, but the server declines the request.
  EXPECT_FALSE(window->bounds() == window_in_embedded->bounds());

  // The client is notified when the requested is declined, and updates the
  // local bounds accordingly.
  ASSERT_TRUE(WaitForBoundsToChange(window_in_embedded));
  EXPECT_TRUE(window->bounds() == window_in_embedded->bounds());
}

// Verifies that a root window can always be destroyed.
TEST_F(WindowServerTest, DestroySecurity) {
  Window* window = window_manager()->NewWindow();
  window->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window);

  WindowTreeConnection* embedded = Embed(window).connection;
  ASSERT_NE(nullptr, embedded);

  // The root can be destroyed, even though it was not created by the
  // connection.
  Window* embed_root = embedded->GetWindowById(window->id());
  WindowTracker tracker1(window);
  WindowTracker tracker2(embed_root);
  embed_root->Destroy();
  EXPECT_FALSE(tracker2.is_valid());
  EXPECT_TRUE(tracker1.is_valid());

  window->Destroy();
  EXPECT_FALSE(tracker1.is_valid());
}

TEST_F(WindowServerTest, MultiRoots) {
  Window* window1 = window_manager()->NewWindow();
  window1->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window1);
  Window* window2 = window_manager()->NewWindow();
  window2->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window2);
  WindowTreeConnection* embedded1 = Embed(window1).connection;
  ASSERT_NE(nullptr, embedded1);
  WindowTreeConnection* embedded2 = Embed(window2).connection;
  ASSERT_NE(nullptr, embedded2);
  EXPECT_NE(embedded1, embedded2);
}

TEST_F(WindowServerTest, Reorder) {
  Window* window1 = window_manager()->NewWindow();
  window1->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window1);

  WindowTreeConnection* embedded = Embed(window1).connection;
  ASSERT_NE(nullptr, embedded);

  Window* window11 = embedded->NewWindow();
  window11->SetVisible(true);
  embedded->GetRoot()->AddChild(window11);
  Window* window12 = embedded->NewWindow();
  window12->SetVisible(true);
  embedded->GetRoot()->AddChild(window12);
  ASSERT_TRUE(WaitForTreeSizeToMatch(window1, 3u));

  Window* root_in_embedded = embedded->GetRoot();

  {
    window11->MoveToFront();
    // The |embedded| tree should be updated immediately.
    EXPECT_EQ(root_in_embedded->children().front(),
              embedded->GetWindowById(window12->id()));
    EXPECT_EQ(root_in_embedded->children().back(),
              embedded->GetWindowById(window11->id()));

    // The |window_manager()| tree is still not updated.
    EXPECT_EQ(window1->children().back(),
              window_manager()->GetWindowById(window12->id()));

    // Wait until |window_manager()| tree is updated.
    ASSERT_TRUE(WaitForOrderChange(
        window_manager(), window_manager()->GetWindowById(window11->id())));
    EXPECT_EQ(window1->children().front(),
              window_manager()->GetWindowById(window12->id()));
    EXPECT_EQ(window1->children().back(),
              window_manager()->GetWindowById(window11->id()));
  }

  {
    window11->MoveToBack();
    // |embedded| should be updated immediately.
    EXPECT_EQ(root_in_embedded->children().front(),
              embedded->GetWindowById(window11->id()));
    EXPECT_EQ(root_in_embedded->children().back(),
              embedded->GetWindowById(window12->id()));

    // |window_manager()| is also eventually updated.
    EXPECT_EQ(window1->children().back(),
              window_manager()->GetWindowById(window11->id()));
    ASSERT_TRUE(WaitForOrderChange(
        window_manager(), window_manager()->GetWindowById(window11->id())));
    EXPECT_EQ(window1->children().front(),
              window_manager()->GetWindowById(window11->id()));
    EXPECT_EQ(window1->children().back(),
              window_manager()->GetWindowById(window12->id()));
  }
}

namespace {

class VisibilityChangeObserver : public WindowObserver {
 public:
  explicit VisibilityChangeObserver(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~VisibilityChangeObserver() override { window_->RemoveObserver(this); }

 private:
  // Overridden from WindowObserver:
  void OnWindowVisibilityChanged(Window* window) override {
    EXPECT_EQ(window, window_);
    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(VisibilityChangeObserver);
};

}  // namespace

TEST_F(WindowServerTest, Visible) {
  Window* window1 = window_manager()->NewWindow();
  window1->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window1);

  // Embed another app and verify initial state.
  WindowTreeConnection* embedded = Embed(window1).connection;
  ASSERT_NE(nullptr, embedded);
  ASSERT_NE(nullptr, embedded->GetRoot());
  Window* embedded_root = embedded->GetRoot();
  EXPECT_TRUE(embedded_root->visible());
  EXPECT_TRUE(embedded_root->IsDrawn());

  // Change the visible state from the first connection and verify its mirrored
  // correctly to the embedded app.
  {
    VisibilityChangeObserver observer(embedded_root);
    window1->SetVisible(false);
    ASSERT_TRUE(WindowServerTestBase::DoRunLoopWithTimeout());
  }

  EXPECT_FALSE(window1->visible());
  EXPECT_FALSE(window1->IsDrawn());

  EXPECT_FALSE(embedded_root->visible());
  EXPECT_FALSE(embedded_root->IsDrawn());

  // Make the node visible again.
  {
    VisibilityChangeObserver observer(embedded_root);
    window1->SetVisible(true);
    ASSERT_TRUE(WindowServerTestBase::DoRunLoopWithTimeout());
  }

  EXPECT_TRUE(window1->visible());
  EXPECT_TRUE(window1->IsDrawn());

  EXPECT_TRUE(embedded_root->visible());
  EXPECT_TRUE(embedded_root->IsDrawn());
}

namespace {

class DrawnChangeObserver : public WindowObserver {
 public:
  explicit DrawnChangeObserver(Window* window) : window_(window) {
    window_->AddObserver(this);
  }
  ~DrawnChangeObserver() override { window_->RemoveObserver(this); }

 private:
  // Overridden from WindowObserver:
  void OnWindowDrawnChanged(Window* window) override {
    EXPECT_EQ(window, window_);
    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(DrawnChangeObserver);
};

}  // namespace

TEST_F(WindowServerTest, Drawn) {
  Window* window1 = window_manager()->NewWindow();
  window1->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window1);

  // Embed another app and verify initial state.
  WindowTreeConnection* embedded = Embed(window1).connection;
  ASSERT_NE(nullptr, embedded);
  ASSERT_NE(nullptr, embedded->GetRoot());
  Window* embedded_root = embedded->GetRoot();
  EXPECT_TRUE(embedded_root->visible());
  EXPECT_TRUE(embedded_root->IsDrawn());

  // Change the visibility of the root, this should propagate a drawn state
  // change to |embedded|.
  {
    DrawnChangeObserver observer(embedded_root);
    window_manager()->GetRoot()->SetVisible(false);
    ASSERT_TRUE(DoRunLoopWithTimeout());
  }

  EXPECT_TRUE(window1->visible());
  EXPECT_FALSE(window1->IsDrawn());

  EXPECT_TRUE(embedded_root->visible());
  EXPECT_FALSE(embedded_root->IsDrawn());
}

// TODO(beng): tests for window event dispatcher.
// - verify that we see events for all windows.

namespace {

class FocusChangeObserver : public WindowObserver {
 public:
  explicit FocusChangeObserver(Window* window)
      : window_(window),
        last_gained_focus_(nullptr),
        last_lost_focus_(nullptr) {
    window_->AddObserver(this);
  }
  ~FocusChangeObserver() override { window_->RemoveObserver(this); }

  Window* last_gained_focus() { return last_gained_focus_; }

  Window* last_lost_focus() { return last_lost_focus_; }

 private:
  // Overridden from WindowObserver.
  void OnWindowFocusChanged(Window* gained_focus, Window* lost_focus) override {
    EXPECT_TRUE(!gained_focus || gained_focus->HasFocus());
    EXPECT_FALSE(lost_focus && lost_focus->HasFocus());
    last_gained_focus_ = gained_focus;
    last_lost_focus_ = lost_focus;
    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  Window* window_;
  Window* last_gained_focus_;
  Window* last_lost_focus_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(FocusChangeObserver);
};

bool WaitForWindowToHaveFocus(Window* window) {
  if (window->HasFocus())
    return true;
  FocusChangeObserver observer(window);
  return WindowServerTestBase::DoRunLoopWithTimeout();
}

}  // namespace

TEST_F(WindowServerTest, Focus) {
  Window* window1 = window_manager()->NewWindow();
  window1->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window1);

  WindowTreeConnection* embedded = Embed(window1).connection;
  ASSERT_NE(nullptr, embedded);
  Window* window11 = embedded->NewWindow();
  window11->SetVisible(true);
  embedded->GetRoot()->AddChild(window11);

  // TODO(alhaad): Figure out why switching focus between windows from different
  // connections is causing the tests to crash and add tests for that.
  {
    Window* embedded_root = embedded->GetRoot();
    FocusChangeObserver observer(embedded_root);
    embedded_root->SetFocus();
    ASSERT_TRUE(DoRunLoopWithTimeout());
    ASSERT_NE(nullptr, observer.last_gained_focus());
    EXPECT_EQ(embedded_root->id(), observer.last_gained_focus()->id());
  }
  {
    FocusChangeObserver observer(window11);
    window11->SetFocus();
    ASSERT_TRUE(DoRunLoopWithTimeout());
    ASSERT_NE(nullptr, observer.last_gained_focus());
    ASSERT_NE(nullptr, observer.last_lost_focus());
    EXPECT_EQ(window11->id(), observer.last_gained_focus()->id());
    EXPECT_EQ(embedded->GetRoot()->id(), observer.last_lost_focus()->id());
  }
  {
    // Add an observer on the Window that loses focus, and make sure the
    // observer sees the right values.
    FocusChangeObserver observer(window11);
    embedded->GetRoot()->SetFocus();
    ASSERT_TRUE(DoRunLoopWithTimeout());
    ASSERT_NE(nullptr, observer.last_gained_focus());
    ASSERT_NE(nullptr, observer.last_lost_focus());
    EXPECT_EQ(window11->id(), observer.last_lost_focus()->id());
    EXPECT_EQ(embedded->GetRoot()->id(), observer.last_gained_focus()->id());
  }
}

TEST_F(WindowServerTest, Activation) {
  Window* parent =
      NewVisibleWindow(window_manager()->GetRoot(), window_manager());
  Window* child1 = NewVisibleWindow(parent, window_manager());
  Window* child2 = NewVisibleWindow(parent, window_manager());
  Window* child3 = NewVisibleWindow(parent, window_manager());

  child1->AddTransientWindow(child3);

  WindowTreeConnection* embedded1 = Embed(child1).connection;
  ASSERT_NE(nullptr, embedded1);
  WindowTreeConnection* embedded2 = Embed(child2).connection;
  ASSERT_NE(nullptr, embedded2);

  Window* child11 = NewVisibleWindow(embedded1->GetRoot(), embedded1);
  Window* child21 = NewVisibleWindow(embedded2->GetRoot(), embedded2);

  WaitForTreeSizeToMatch(parent, 6);

  // Allow the child windows to be activated.
  host()->AddActivationParent(parent->id());

  // |child2| and |child3| are stacked about |child1|.
  EXPECT_GT(ValidIndexOf(parent->children(), child2),
            ValidIndexOf(parent->children(), child1));
  EXPECT_GT(ValidIndexOf(parent->children(), child3),
            ValidIndexOf(parent->children(), child1));

  // Set focus on |child11|. This should activate |child1|, and raise it over
  // |child2|. But |child3| should still be above |child1| because of
  // transiency.
  child11->SetFocus();
  ASSERT_TRUE(WaitForWindowToHaveFocus(child11));
  ASSERT_TRUE(
      WaitForWindowToHaveFocus(window_manager()->GetWindowById(child11->id())));
  EXPECT_EQ(child11->id(), window_manager()->GetFocusedWindow()->id());
  EXPECT_EQ(child11->id(), embedded1->GetFocusedWindow()->id());
  EXPECT_EQ(nullptr, embedded2->GetFocusedWindow());
  EXPECT_GT(ValidIndexOf(parent->children(), child1),
            ValidIndexOf(parent->children(), child2));
  EXPECT_GT(ValidIndexOf(parent->children(), child3),
            ValidIndexOf(parent->children(), child1));

  // Set focus on |child21|. This should activate |child2|, and raise it over
  // |child1|.
  child21->SetFocus();
  ASSERT_TRUE(WaitForWindowToHaveFocus(child21));
  ASSERT_TRUE(
      WaitForWindowToHaveFocus(window_manager()->GetWindowById(child21->id())));
  EXPECT_EQ(child21->id(), window_manager()->GetFocusedWindow()->id());
  EXPECT_EQ(child21->id(), embedded2->GetFocusedWindow()->id());
  EXPECT_EQ(nullptr, embedded1->GetFocusedWindow());
  EXPECT_GT(ValidIndexOf(parent->children(), child2),
            ValidIndexOf(parent->children(), child1));
  EXPECT_GT(ValidIndexOf(parent->children(), child3),
            ValidIndexOf(parent->children(), child1));
}

TEST_F(WindowServerTest, ActivationNext) {
  Window* parent = window_manager()->GetRoot();
  Window* child1 = NewVisibleWindow(parent, window_manager());
  Window* child2 = NewVisibleWindow(parent, window_manager());
  Window* child3 = NewVisibleWindow(parent, window_manager());

  WindowTreeConnection* embedded1 = Embed(child1).connection;
  ASSERT_NE(nullptr, embedded1);

  NewVisibleWindow(embedded1->GetRoot(), embedded1);
  WaitForTreeSizeToMatch(parent, 5);

  Window* expecteds[] = { child1, child2, child3, child1, nullptr };
  for (size_t index = 0; expecteds[index]; ++index) {
    host()->ActivateNextWindow();
    ASSERT_TRUE(WaitForOrderChange(window_manager(), expecteds[index]))
        << " Failure at " << index;
  }
}

namespace {

class DestroyedChangedObserver : public WindowObserver {
 public:
  DestroyedChangedObserver(WindowServerTestBase* test,
                           Window* window,
                           bool* got_destroy)
      : test_(test), window_(window), got_destroy_(got_destroy) {
    window_->AddObserver(this);
  }
  ~DestroyedChangedObserver() override {
    if (window_)
      window_->RemoveObserver(this);
  }

 private:
  // Overridden from WindowObserver:
  void OnWindowDestroyed(Window* window) override {
    EXPECT_EQ(window, window_);
    window_->RemoveObserver(this);
    *got_destroy_ = true;
    window_ = nullptr;

    // We should always get OnWindowDestroyed() before OnConnectionLost().
    EXPECT_FALSE(test_->window_tree_connection_destroyed());
  }

  WindowServerTestBase* test_;
  Window* window_;
  bool* got_destroy_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(DestroyedChangedObserver);
};

}  // namespace

// Verifies deleting a WindowServer sends the right notifications.
TEST_F(WindowServerTest, DeleteWindowServer) {
  Window* window = window_manager()->NewWindow();
  ASSERT_NE(nullptr, window);
  window->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window);
  WindowTreeConnection* connection = Embed(window).connection;
  ASSERT_TRUE(connection);
  bool got_destroy = false;
  DestroyedChangedObserver observer(this, connection->GetRoot(), &got_destroy);
  delete connection;
  EXPECT_TRUE(window_tree_connection_destroyed());
  EXPECT_TRUE(got_destroy);
}

// Verifies two Embed()s in the same window trigger deletion of the first
// WindowServer.
TEST_F(WindowServerTest, DisconnectTriggersDelete) {
  Window* window = window_manager()->NewWindow();
  ASSERT_NE(nullptr, window);
  window->SetVisible(true);
  window_manager()->GetRoot()->AddChild(window);
  WindowTreeConnection* connection = Embed(window).connection;
  EXPECT_NE(connection, window_manager());
  Window* embedded_window = connection->NewWindow();
  // Embed again, this should trigger disconnect and deletion of connection.
  bool got_destroy;
  DestroyedChangedObserver observer(this, embedded_window, &got_destroy);
  EXPECT_FALSE(window_tree_connection_destroyed());
  Embed(window);
  EXPECT_TRUE(window_tree_connection_destroyed());
}

class WindowRemovedFromParentObserver : public WindowObserver {
 public:
  explicit WindowRemovedFromParentObserver(Window* window)
      : window_(window), was_removed_(false) {
    window_->AddObserver(this);
  }
  ~WindowRemovedFromParentObserver() override { window_->RemoveObserver(this); }

  bool was_removed() const { return was_removed_; }

 private:
  // Overridden from WindowObserver:
  void OnTreeChanged(const TreeChangeParams& params) override {
    if (params.target == window_ && !params.new_parent)
      was_removed_ = true;
  }

  Window* window_;
  bool was_removed_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(WindowRemovedFromParentObserver);
};

TEST_F(WindowServerTest, EmbedRemovesChildren) {
  Window* window1 = window_manager()->NewWindow();
  Window* window2 = window_manager()->NewWindow();
  window_manager()->GetRoot()->AddChild(window1);
  window1->AddChild(window2);

  WindowRemovedFromParentObserver observer(window2);
  window1->Embed(ConnectToApplicationAndGetWindowServerClient());
  EXPECT_TRUE(observer.was_removed());
  EXPECT_EQ(nullptr, window2->parent());
  EXPECT_TRUE(window1->children().empty());

  // Run the message loop so the Embed() call above completes. Without this
  // we may end up reconnecting to the test and rerunning the test, which is
  // problematic since the other services don't shut down.
  ASSERT_TRUE(DoRunLoopWithTimeout());
}

namespace {

class DestroyObserver : public WindowObserver {
 public:
  DestroyObserver(WindowServerTestBase* test,
                  WindowTreeConnection* connection,
                  bool* got_destroy)
      : test_(test), got_destroy_(got_destroy) {
    connection->GetRoot()->AddObserver(this);
  }
  ~DestroyObserver() override {}

 private:
  // Overridden from WindowObserver:
  void OnWindowDestroyed(Window* window) override {
    *got_destroy_ = true;
    window->RemoveObserver(this);

    // We should always get OnWindowDestroyed() before
    // OnWindowManagerDestroyed().
    EXPECT_FALSE(test_->window_tree_connection_destroyed());

    EXPECT_TRUE(WindowServerTestBase::QuitRunLoop());
  }

  WindowServerTestBase* test_;
  bool* got_destroy_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(DestroyObserver);
};

}  // namespace

// Verifies deleting a Window that is the root of another connection notifies
// observers in the right order (OnWindowDestroyed() before
// OnWindowManagerDestroyed()).
TEST_F(WindowServerTest, WindowServerDestroyedAfterRootObserver) {
  Window* embed_window = window_manager()->NewWindow();
  window_manager()->GetRoot()->AddChild(embed_window);

  WindowTreeConnection* embedded_connection = Embed(embed_window).connection;

  bool got_destroy = false;
  DestroyObserver observer(this, embedded_connection, &got_destroy);
  // Delete the window |embedded_connection| is embedded in. This is async,
  // but will eventually trigger deleting |embedded_connection|.
  embed_window->Destroy();
  EXPECT_TRUE(DoRunLoopWithTimeout());
  EXPECT_TRUE(got_destroy);
}

// Verifies an embed root sees windows created beneath it from another
// connection.
TEST_F(WindowServerTest, EmbedRootSeesHierarchyChanged) {
  Window* embed_window = window_manager()->NewWindow();
  window_manager()->GetRoot()->AddChild(embed_window);

  WindowTreeConnection* vm2 =
      Embed(embed_window, mus::mojom::WindowTree::ACCESS_POLICY_EMBED_ROOT)
          .connection;
  Window* vm2_v1 = vm2->NewWindow();
  vm2->GetRoot()->AddChild(vm2_v1);

  WindowTreeConnection* vm3 = Embed(vm2_v1).connection;
  Window* vm3_v1 = vm3->NewWindow();
  vm3->GetRoot()->AddChild(vm3_v1);

  // As |vm2| is an embed root it should get notified about |vm3_v1|.
  ASSERT_TRUE(WaitForTreeSizeToMatch(vm2_v1, 2));
}

TEST_F(WindowServerTest, EmbedFromEmbedRoot) {
  Window* embed_window = window_manager()->NewWindow();
  window_manager()->GetRoot()->AddChild(embed_window);

  // Give the connection embedded at |embed_window| embed root powers.
  const EmbedResult result1 =
      Embed(embed_window, mus::mojom::WindowTree::ACCESS_POLICY_EMBED_ROOT);
  WindowTreeConnection* vm2 = result1.connection;
  EXPECT_EQ(result1.connection_id, vm2->GetConnectionId());
  Window* vm2_v1 = vm2->NewWindow();
  vm2->GetRoot()->AddChild(vm2_v1);

  const EmbedResult result2 = Embed(vm2_v1);
  WindowTreeConnection* vm3 = result2.connection;
  EXPECT_EQ(result2.connection_id, vm3->GetConnectionId());
  Window* vm3_v1 = vm3->NewWindow();
  vm3->GetRoot()->AddChild(vm3_v1);

  // Embed from v3, the callback should not get the connection id as vm3 is not
  // an embed root.
  const EmbedResult result3 = Embed(vm3_v1);
  ASSERT_TRUE(result3.connection);
  EXPECT_EQ(0, result3.connection_id);

  // As |vm2| is an embed root it should get notified about |vm3_v1|.
  ASSERT_TRUE(WaitForTreeSizeToMatch(vm2_v1, 2));

  // Embed() from vm2 in vm3_v1. This is allowed as vm2 is an embed root, and
  // further the callback should see the connection id.
  ASSERT_EQ(1u, vm2_v1->children().size());
  Window* vm3_v1_in_vm2 = vm2_v1->children()[0];
  const EmbedResult result4 = Embed(vm3_v1_in_vm2);
  ASSERT_TRUE(result4.connection);
  EXPECT_EQ(result4.connection_id, result4.connection->GetConnectionId());
}

TEST_F(WindowServerTest, ClientAreaChanged) {
  Window* embed_window = window_manager()->NewWindow();
  window_manager()->GetRoot()->AddChild(embed_window);

  WindowTreeConnection* embedded_connection = Embed(embed_window).connection;

  // Verify change from embedded makes it to parent.
  embedded_connection->GetRoot()->SetClientArea(gfx::Insets(1, 2, 3, 4));
  ASSERT_TRUE(WaitForClientAreaToChange(embed_window));
  EXPECT_TRUE(gfx::Insets(1, 2, 3, 4) == embed_window->client_area());

  // Changing bounds shouldn't effect client area.
  embed_window->SetBounds(gfx::Rect(21, 22, 23, 24));
  WaitForBoundsToChange(embedded_connection->GetRoot());
  EXPECT_TRUE(gfx::Rect(21, 22, 23, 24) ==
              embedded_connection->GetRoot()->bounds());
  EXPECT_TRUE(gfx::Insets(1, 2, 3, 4) ==
              embedded_connection->GetRoot()->client_area());
}

}  // namespace ws

}  // namespace mus
