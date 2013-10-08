// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <string>
#include <utility>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/path_service.h"
#include "base/stl_util.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/history/archived_database.h"
#include "chrome/browser/history/expire_history_backend.h"
#include "chrome/browser/history/history_database.h"
#include "chrome/browser/history/history_notifications.h"
#include "chrome/browser/history/thumbnail_database.h"
#include "chrome/browser/history/top_sites.h"
#include "chrome/common/thumbnail_score.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/tools/profiles/thumbnail-inl.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/jpeg_codec.h"

using base::Time;
using base::TimeDelta;
using base::TimeTicks;
using content::BrowserThread;

// Filename constants.
static const base::FilePath::CharType kHistoryFile[] =
    FILE_PATH_LITERAL("History");
static const base::FilePath::CharType kArchivedHistoryFile[] =
    FILE_PATH_LITERAL("Archived History");
static const base::FilePath::CharType kThumbnailFile[] =
    FILE_PATH_LITERAL("Thumbnails");

// The test must be in the history namespace for the gtest forward declarations
// to work. It also eliminates a bunch of ugly "history::".
namespace history {

// ExpireHistoryTest -----------------------------------------------------------

class ExpireHistoryTest : public testing::Test,
                          public BroadcastNotificationDelegate {
 public:
  ExpireHistoryTest()
      : bookmark_model_(NULL),
        ui_thread_(BrowserThread::UI, &message_loop_),
        db_thread_(BrowserThread::DB, &message_loop_),
        expirer_(this, &bookmark_model_),
        now_(Time::Now()) {
  }

 protected:
  // Called by individual tests when they want data populated.
  void AddExampleData(URLID url_ids[3], Time visit_times[4]);
  // Add visits with source information.
  void AddExampleSourceData(const GURL& url, URLID* id);

  // Returns true if the given favicon/thumanil has an entry in the DB.
  bool HasFavicon(chrome::FaviconID favicon_id);
  bool HasThumbnail(URLID url_id);

  chrome::FaviconID GetFavicon(const GURL& page_url,
                               chrome::IconType icon_type);

  // EXPECTs that each URL-specific history thing (basically, everything but
  // favicons) is gone.
  void EnsureURLInfoGone(const URLRow& row);

  // Clears the list of notifications received.
  void ClearLastNotifications() {
    STLDeleteValues(&notifications_);
  }

  void StarURL(const GURL& url) {
    bookmark_model_.AddURL(
        bookmark_model_.bookmark_bar_node(), 0, string16(), url);
  }

  static bool IsStringInFile(const base::FilePath& filename, const char* str);

  // Returns the path the db files are created in.
  const base::FilePath& path() const { return tmp_dir_.path(); }

  // This must be destroyed last.
  base::ScopedTempDir tmp_dir_;

  BookmarkModel bookmark_model_;

  base::MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread db_thread_;

  ExpireHistoryBackend expirer_;

  scoped_ptr<HistoryDatabase> main_db_;
  scoped_ptr<ArchivedDatabase> archived_db_;
  scoped_ptr<ThumbnailDatabase> thumb_db_;
  TestingProfile profile_;
  scoped_refptr<TopSites> top_sites_;

  // Time at the beginning of the test, so everybody agrees what "now" is.
  const Time now_;

  // Notifications intended to be broadcast, we can check these values to make
  // sure that the deletor is doing the correct broadcasts. We own the details
  // pointers.
  typedef std::vector< std::pair<int, HistoryDetails*> >
      NotificationList;
  NotificationList notifications_;

 private:
  virtual void SetUp() {
    ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());

    base::FilePath history_name = path().Append(kHistoryFile);
    main_db_.reset(new HistoryDatabase);
    if (main_db_->Init(history_name) != sql::INIT_OK)
      main_db_.reset();

    base::FilePath archived_name = path().Append(kArchivedHistoryFile);
    archived_db_.reset(new ArchivedDatabase);
    if (!archived_db_->Init(archived_name))
      archived_db_.reset();

    base::FilePath thumb_name = path().Append(kThumbnailFile);
    thumb_db_.reset(new ThumbnailDatabase);
    if (thumb_db_->Init(thumb_name, NULL, main_db_.get()) != sql::INIT_OK)
      thumb_db_.reset();

    expirer_.SetDatabases(main_db_.get(), archived_db_.get(), thumb_db_.get());
    profile_.CreateTopSites();
    profile_.BlockUntilTopSitesLoaded();
    top_sites_ = profile_.GetTopSites();
  }

  virtual void TearDown() {
    top_sites_ = NULL;

    ClearLastNotifications();

    expirer_.SetDatabases(NULL, NULL, NULL);

    main_db_.reset();
    archived_db_.reset();
    thumb_db_.reset();
  }

  // BroadcastNotificationDelegate implementation.
  virtual void BroadcastNotifications(
      int type,
      HistoryDetails* details_deleted) OVERRIDE {
    // This gets called when there are notifications to broadcast. Instead, we
    // store them so we can tell that the correct notifications were sent.
    notifications_.push_back(std::make_pair(type, details_deleted));
  }
  virtual void NotifySyncURLsDeleted(
      bool all_history,
      bool archived,
      URLRows* rows) OVERRIDE {}
};

// The example data consists of 4 visits. The middle two visits are to the
// same URL, while the first and last are for unique ones. This allows a test
// for the oldest or newest to include both a URL that should get totally
// deleted (the one on the end) with one that should only get a visit deleted
// (with the one in the middle) when it picks the proper threshold time.
//
// Each visit has indexed data, each URL has thumbnail. The first two URLs will
// share the same avicon, while the last one will have a unique favicon. The
// second visit for the middle URL is typed.
//
// The IDs of the added URLs, and the times of the four added visits will be
// added to the given arrays.
void ExpireHistoryTest::AddExampleData(URLID url_ids[3], Time visit_times[4]) {
  if (!main_db_.get())
    return;

  // Four times for each visit.
  visit_times[3] = Time::Now();
  visit_times[2] = visit_times[3] - TimeDelta::FromDays(1);
  visit_times[1] = visit_times[3] - TimeDelta::FromDays(2);
  visit_times[0] = visit_times[3] - TimeDelta::FromDays(3);

  // Two favicons. The first two URLs will share the same one, while the last
  // one will have a unique favicon.
  chrome::FaviconID favicon1 = thumb_db_->AddFavicon(
      GURL("http://favicon/url1"), chrome::FAVICON);
  chrome::FaviconID favicon2 = thumb_db_->AddFavicon(
      GURL("http://favicon/url2"), chrome::FAVICON);

  // Three URLs.
  URLRow url_row1(GURL("http://www.google.com/1"));
  url_row1.set_last_visit(visit_times[0]);
  url_row1.set_visit_count(1);
  url_ids[0] = main_db_->AddURL(url_row1);
  thumb_db_->AddIconMapping(url_row1.url(), favicon1);

  URLRow url_row2(GURL("http://www.google.com/2"));
  url_row2.set_last_visit(visit_times[2]);
  url_row2.set_visit_count(2);
  url_row2.set_typed_count(1);
  url_ids[1] = main_db_->AddURL(url_row2);
  thumb_db_->AddIconMapping(url_row2.url(), favicon1);

  URLRow url_row3(GURL("http://www.google.com/3"));
  url_row3.set_last_visit(visit_times[3]);
  url_row3.set_visit_count(1);
  url_ids[2] = main_db_->AddURL(url_row3);
  thumb_db_->AddIconMapping(url_row3.url(), favicon2);

  // Thumbnails for each URL. |thumbnail| takes ownership of decoded SkBitmap.
  scoped_ptr<SkBitmap> thumbnail_bitmap(
      gfx::JPEGCodec::Decode(kGoogleThumbnail, sizeof(kGoogleThumbnail)));
  gfx::Image thumbnail = gfx::Image::CreateFrom1xBitmap(*thumbnail_bitmap);
  ThumbnailScore score(0.25, true, true, Time::Now());

  Time time;
  GURL gurl;
  top_sites_->SetPageThumbnail(url_row1.url(), thumbnail, score);
  top_sites_->SetPageThumbnail(url_row2.url(), thumbnail, score);
  top_sites_->SetPageThumbnail(url_row3.url(), thumbnail, score);

  // Four visits.
  VisitRow visit_row1;
  visit_row1.url_id = url_ids[0];
  visit_row1.visit_time = visit_times[0];
  main_db_->AddVisit(&visit_row1, SOURCE_BROWSED);

  VisitRow visit_row2;
  visit_row2.url_id = url_ids[1];
  visit_row2.visit_time = visit_times[1];
  main_db_->AddVisit(&visit_row2, SOURCE_BROWSED);

  VisitRow visit_row3;
  visit_row3.url_id = url_ids[1];
  visit_row3.visit_time = visit_times[2];
  visit_row3.transition = content::PAGE_TRANSITION_TYPED;
  main_db_->AddVisit(&visit_row3, SOURCE_BROWSED);

  VisitRow visit_row4;
  visit_row4.url_id = url_ids[2];
  visit_row4.visit_time = visit_times[3];
  main_db_->AddVisit(&visit_row4, SOURCE_BROWSED);
}

void ExpireHistoryTest::AddExampleSourceData(const GURL& url, URLID* id) {
  if (!main_db_)
    return;

  Time last_visit_time = Time::Now();
  // Add one URL.
  URLRow url_row1(url);
  url_row1.set_last_visit(last_visit_time);
  url_row1.set_visit_count(4);
  URLID url_id = main_db_->AddURL(url_row1);
  *id = url_id;

  // Four times for each visit.
  VisitRow visit_row1(url_id, last_visit_time - TimeDelta::FromDays(4), 0,
                      content::PAGE_TRANSITION_TYPED, 0);
  main_db_->AddVisit(&visit_row1, SOURCE_SYNCED);

  VisitRow visit_row2(url_id, last_visit_time - TimeDelta::FromDays(3), 0,
                      content::PAGE_TRANSITION_TYPED, 0);
  main_db_->AddVisit(&visit_row2, SOURCE_BROWSED);

  VisitRow visit_row3(url_id, last_visit_time - TimeDelta::FromDays(2), 0,
                      content::PAGE_TRANSITION_TYPED, 0);
  main_db_->AddVisit(&visit_row3, SOURCE_EXTENSION);

  VisitRow visit_row4(
      url_id, last_visit_time, 0, content::PAGE_TRANSITION_TYPED, 0);
  main_db_->AddVisit(&visit_row4, SOURCE_FIREFOX_IMPORTED);
}

bool ExpireHistoryTest::HasFavicon(chrome::FaviconID favicon_id) {
  if (!thumb_db_.get() || favicon_id == 0)
    return false;
  return thumb_db_->GetFaviconHeader(favicon_id, NULL, NULL);
}

chrome::FaviconID ExpireHistoryTest::GetFavicon(const GURL& page_url,
                                                chrome::IconType icon_type) {
  std::vector<IconMapping> icon_mappings;
  if (thumb_db_->GetIconMappingsForPageURL(page_url, icon_type,
                                           &icon_mappings)) {
    return icon_mappings[0].icon_id;
  }
  return 0;
}

bool ExpireHistoryTest::HasThumbnail(URLID url_id) {
  // TODO(sky): fix this. This test isn't really valid for TopSites. For
  // TopSites we should be checking URL always, not the id.
  URLRow info;
  if (!main_db_->GetURLRow(url_id, &info))
    return false;
  GURL url = info.url();
  scoped_refptr<base::RefCountedMemory> data;
  return top_sites_->GetPageThumbnail(url, &data);
}

void ExpireHistoryTest::EnsureURLInfoGone(const URLRow& row) {
  // Verify the URL no longer exists.
  URLRow temp_row;
  EXPECT_FALSE(main_db_->GetURLRow(row.id(), &temp_row));

  // There should be no visits.
  VisitVector visits;
  main_db_->GetVisitsForURL(row.id(), &visits);
  EXPECT_EQ(0U, visits.size());

  // Thumbnail should be gone.
  // TODO(sky): fix this, see comment in HasThumbnail.
  // EXPECT_FALSE(HasThumbnail(row.id()));

  bool found_delete_notification = false;
  for (size_t i = 0; i < notifications_.size(); i++) {
    if (notifications_[i].first == chrome::NOTIFICATION_HISTORY_URLS_DELETED) {
      URLsDeletedDetails* details = reinterpret_cast<URLsDeletedDetails*>(
          notifications_[i].second);
      EXPECT_FALSE(details->archived);
      const history::URLRows& rows(details->rows);
      if (std::find_if(rows.begin(), rows.end(),
          history::URLRow::URLRowHasURL(row.url())) != rows.end()) {
        found_delete_notification = true;
      }
    } else {
      EXPECT_NE(notifications_[i].first,
                chrome::NOTIFICATION_HISTORY_URL_VISITED);
      EXPECT_NE(notifications_[i].first,
                chrome::NOTIFICATION_HISTORY_URLS_MODIFIED);
    }
  }
  EXPECT_TRUE(found_delete_notification);
}

TEST_F(ExpireHistoryTest, DeleteFaviconsIfPossible) {
  // Add a favicon record.
  const GURL favicon_url("http://www.google.com/favicon.ico");
  chrome::FaviconID icon_id = thumb_db_->AddFavicon(
      favicon_url, chrome::FAVICON);
  EXPECT_TRUE(icon_id);
  EXPECT_TRUE(HasFavicon(icon_id));

  // The favicon should be deletable with no users.
  std::set<chrome::FaviconID> favicon_set;
  std::set<GURL> expired_favicons;
  favicon_set.insert(icon_id);
  expirer_.DeleteFaviconsIfPossible(favicon_set, &expired_favicons);
  EXPECT_FALSE(HasFavicon(icon_id));
  EXPECT_EQ(1U, expired_favicons.size());
  EXPECT_EQ(1U, expired_favicons.count(favicon_url));

  // Add back the favicon.
  icon_id = thumb_db_->AddFavicon(
      favicon_url, chrome::TOUCH_ICON);
  EXPECT_TRUE(icon_id);
  EXPECT_TRUE(HasFavicon(icon_id));

  // Add a page that references the favicon.
  URLRow row(GURL("http://www.google.com/2"));
  row.set_visit_count(1);
  EXPECT_TRUE(main_db_->AddURL(row));
  thumb_db_->AddIconMapping(row.url(), icon_id);

  // Favicon should not be deletable.
  favicon_set.clear();
  favicon_set.insert(icon_id);
  expired_favicons.clear();
  expirer_.DeleteFaviconsIfPossible(favicon_set, &expired_favicons);
  EXPECT_TRUE(HasFavicon(icon_id));
  EXPECT_TRUE(expired_favicons.empty());
}

// static
bool ExpireHistoryTest::IsStringInFile(const base::FilePath& filename,
                                       const char* str) {
  std::string contents;
  EXPECT_TRUE(file_util::ReadFileToString(filename, &contents));
  return contents.find(str) != std::string::npos;
}

// Deletes a URL with a favicon that it is the last referencer of, so that it
// should also get deleted.
// Fails near end of month. http://crbug.com/43586
TEST_F(ExpireHistoryTest, DISABLED_DeleteURLAndFavicon) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);

  // Verify things are the way we expect with a URL row, favicon, thumbnail.
  URLRow last_row;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[2], &last_row));
  chrome::FaviconID favicon_id = GetFavicon(last_row.url(), chrome::FAVICON);
  EXPECT_TRUE(HasFavicon(favicon_id));
  // TODO(sky): fix this, see comment in HasThumbnail.
  // EXPECT_TRUE(HasThumbnail(url_ids[2]));

  VisitVector visits;
  main_db_->GetVisitsForURL(url_ids[2], &visits);
  ASSERT_EQ(1U, visits.size());

  // Delete the URL and its dependencies.
  expirer_.DeleteURL(last_row.url());

  // All the normal data + the favicon should be gone.
  EnsureURLInfoGone(last_row);
  EXPECT_FALSE(GetFavicon(last_row.url(), chrome::FAVICON));
  EXPECT_FALSE(HasFavicon(favicon_id));
}

// Deletes a URL with a favicon that other URLs reference, so that the favicon
// should not get deleted. This also tests deleting more than one visit.
TEST_F(ExpireHistoryTest, DeleteURLWithoutFavicon) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);

  // Verify things are the way we expect with a URL row, favicon, thumbnail.
  URLRow last_row;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &last_row));
  chrome::FaviconID favicon_id = GetFavicon(last_row.url(), chrome::FAVICON);
  EXPECT_TRUE(HasFavicon(favicon_id));
  // TODO(sky): fix this, see comment in HasThumbnail.
  // EXPECT_TRUE(HasThumbnail(url_ids[1]));

  VisitVector visits;
  main_db_->GetVisitsForURL(url_ids[1], &visits);
  EXPECT_EQ(2U, visits.size());

  // Delete the URL and its dependencies.
  expirer_.DeleteURL(last_row.url());

  // All the normal data except the favicon should be gone.
  EnsureURLInfoGone(last_row);
  EXPECT_TRUE(HasFavicon(favicon_id));
}

// DeleteURL should not delete starred urls.
TEST_F(ExpireHistoryTest, DontDeleteStarredURL) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);

  URLRow url_row;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[2], &url_row));

  // Star the last URL.
  StarURL(url_row.url());

  // Attempt to delete the url.
  expirer_.DeleteURL(url_row.url());

  // Because the url is starred, it shouldn't be deleted.
  GURL url = url_row.url();
  ASSERT_TRUE(main_db_->GetRowForURL(url, &url_row));

  // And the favicon should exist.
  chrome::FaviconID favicon_id = GetFavicon(url_row.url(), chrome::FAVICON);
  EXPECT_TRUE(HasFavicon(favicon_id));

  // And no visits.
  VisitVector visits;
  main_db_->GetVisitsForURL(url_row.id(), &visits);
  ASSERT_EQ(0U, visits.size());

  // Should still have the thumbnail.
  // TODO(sky): fix this, see comment in HasThumbnail.
  // ASSERT_TRUE(HasThumbnail(url_row.id()));

  // Unstar the URL and delete again.
  bookmark_utils::RemoveAllBookmarks(&bookmark_model_, url);
  expirer_.DeleteURL(url);

  // Now it should be completely deleted.
  EnsureURLInfoGone(url_row);
}

// Deletes multiple URLs at once.  The favicon for the third one but
// not the first two should be deleted.
TEST_F(ExpireHistoryTest, DeleteURLs) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);

  // Verify things are the way we expect with URL rows, favicons,
  // thumbnails.
  URLRow rows[3];
  chrome::FaviconID favicon_ids[3];
  std::vector<GURL> urls;
  // Push back a bogus URL (which shouldn't change anything).
  urls.push_back(GURL());
  for (size_t i = 0; i < arraysize(rows); ++i) {
    ASSERT_TRUE(main_db_->GetURLRow(url_ids[i], &rows[i]));
    favicon_ids[i] = GetFavicon(rows[i].url(), chrome::FAVICON);
    EXPECT_TRUE(HasFavicon(favicon_ids[i]));
    // TODO(sky): fix this, see comment in HasThumbnail.
    // EXPECT_TRUE(HasThumbnail(url_ids[i]));
    urls.push_back(rows[i].url());
  }

  StarURL(rows[0].url());

  // Delete the URLs and their dependencies.
  expirer_.DeleteURLs(urls);

  // First one should still be around (since it was starred).
  ASSERT_TRUE(main_db_->GetRowForURL(rows[0].url(), &rows[0]));
  EnsureURLInfoGone(rows[1]);
  EnsureURLInfoGone(rows[2]);
  EXPECT_TRUE(HasFavicon(favicon_ids[0]));
  EXPECT_TRUE(HasFavicon(favicon_ids[1]));
  EXPECT_FALSE(HasFavicon(favicon_ids[2]));
}

// Expires all URLs more recent than a given time, with no starred items.
// Our time threshold is such that one URL should be updated (we delete one of
// the two visits) and one is deleted.
TEST_F(ExpireHistoryTest, FlushRecentURLsUnstarred) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);

  URLRow url_row1, url_row2;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &url_row1));
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[2], &url_row2));

  VisitVector visits;
  main_db_->GetVisitsForURL(url_ids[2], &visits);
  ASSERT_EQ(1U, visits.size());

  // This should delete the last two visits.
  std::set<GURL> restrict_urls;
  expirer_.ExpireHistoryBetween(restrict_urls, visit_times[2], Time());

  // Verify that the middle URL had its last visit deleted only.
  visits.clear();
  main_db_->GetVisitsForURL(url_ids[1], &visits);
  EXPECT_EQ(1U, visits.size());

  // Verify that the middle URL visit time and visit counts were updated.
  URLRow temp_row;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &temp_row));
  EXPECT_TRUE(visit_times[2] == url_row1.last_visit());  // Previous value.
  EXPECT_TRUE(visit_times[1] == temp_row.last_visit());  // New value.
  EXPECT_EQ(2, url_row1.visit_count());
  EXPECT_EQ(1, temp_row.visit_count());
  EXPECT_EQ(1, url_row1.typed_count());
  EXPECT_EQ(0, temp_row.typed_count());

  // Verify that the middle URL's favicon and thumbnail is still there.
  chrome::FaviconID favicon_id = GetFavicon(url_row1.url(), chrome::FAVICON);
  EXPECT_TRUE(HasFavicon(favicon_id));
  // TODO(sky): fix this, see comment in HasThumbnail.
  // EXPECT_TRUE(HasThumbnail(url_row1.id()));

  // Verify that the last URL was deleted.
  chrome::FaviconID favicon_id2 = GetFavicon(url_row2.url(), chrome::FAVICON);
  EnsureURLInfoGone(url_row2);
  EXPECT_FALSE(HasFavicon(favicon_id2));
}

// Expires all URLs with times in a given set.
TEST_F(ExpireHistoryTest, FlushURLsForTimes) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);

  URLRow url_row1, url_row2;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &url_row1));
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[2], &url_row2));

  VisitVector visits;
  main_db_->GetVisitsForURL(url_ids[2], &visits);
  ASSERT_EQ(1U, visits.size());

  // This should delete the last two visits.
  std::vector<base::Time> times;
  times.push_back(visit_times[3]);
  times.push_back(visit_times[2]);
  expirer_.ExpireHistoryForTimes(times);

  // Verify that the middle URL had its last visit deleted only.
  visits.clear();
  main_db_->GetVisitsForURL(url_ids[1], &visits);
  EXPECT_EQ(1U, visits.size());

  // Verify that the middle URL visit time and visit counts were updated.
  URLRow temp_row;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &temp_row));
  EXPECT_TRUE(visit_times[2] == url_row1.last_visit());  // Previous value.
  EXPECT_TRUE(visit_times[1] == temp_row.last_visit());  // New value.
  EXPECT_EQ(2, url_row1.visit_count());
  EXPECT_EQ(1, temp_row.visit_count());
  EXPECT_EQ(1, url_row1.typed_count());
  EXPECT_EQ(0, temp_row.typed_count());

  // Verify that the middle URL's favicon and thumbnail is still there.
  chrome::FaviconID favicon_id = GetFavicon(url_row1.url(), chrome::FAVICON);
  EXPECT_TRUE(HasFavicon(favicon_id));
  // TODO(sky): fix this, see comment in HasThumbnail.
  // EXPECT_TRUE(HasThumbnail(url_row1.id()));

  // Verify that the last URL was deleted.
  chrome::FaviconID favicon_id2 = GetFavicon(url_row2.url(), chrome::FAVICON);
  EnsureURLInfoGone(url_row2);
  EXPECT_FALSE(HasFavicon(favicon_id2));
}

// Expires only a specific URLs more recent than a given time, with no starred
// items.  Our time threshold is such that the URL should be updated (we delete
// one of the two visits).
TEST_F(ExpireHistoryTest, FlushRecentURLsUnstarredRestricted) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);

  URLRow url_row1, url_row2;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &url_row1));
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[2], &url_row2));

  VisitVector visits;
  main_db_->GetVisitsForURL(url_ids[2], &visits);
  ASSERT_EQ(1U, visits.size());

  // This should delete the last two visits.
  std::set<GURL> restrict_urls;
  restrict_urls.insert(url_row1.url());
  expirer_.ExpireHistoryBetween(restrict_urls, visit_times[2], Time());

  // Verify that the middle URL had its last visit deleted only.
  visits.clear();
  main_db_->GetVisitsForURL(url_ids[1], &visits);
  EXPECT_EQ(1U, visits.size());

  // Verify that the middle URL visit time and visit counts were updated.
  URLRow temp_row;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &temp_row));
  EXPECT_TRUE(visit_times[2] == url_row1.last_visit());  // Previous value.
  EXPECT_TRUE(visit_times[1] == temp_row.last_visit());  // New value.
  EXPECT_EQ(2, url_row1.visit_count());
  EXPECT_EQ(1, temp_row.visit_count());
  EXPECT_EQ(1, url_row1.typed_count());
  EXPECT_EQ(0, temp_row.typed_count());

  // Verify that the middle URL's favicon and thumbnail is still there.
  chrome::FaviconID favicon_id = GetFavicon(url_row1.url(), chrome::FAVICON);
  EXPECT_TRUE(HasFavicon(favicon_id));
  // TODO(sky): fix this, see comment in HasThumbnail.
  // EXPECT_TRUE(HasThumbnail(url_row1.id()));

  // Verify that the last URL was not touched.
  EXPECT_TRUE(main_db_->GetURLRow(url_ids[2], &temp_row));
  EXPECT_TRUE(HasFavicon(favicon_id));
  // TODO(sky): fix this, see comment in HasThumbnail.
  // EXPECT_TRUE(HasThumbnail(url_row2.id()));
}

// Expire a starred URL, it shouldn't get deleted
TEST_F(ExpireHistoryTest, FlushRecentURLsStarred) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);

  URLRow url_row1, url_row2;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &url_row1));
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[2], &url_row2));

  // Star the last two URLs.
  StarURL(url_row1.url());
  StarURL(url_row2.url());

  // This should delete the last two visits.
  std::set<GURL> restrict_urls;
  expirer_.ExpireHistoryBetween(restrict_urls, visit_times[2], Time());

  // The URL rows should still exist.
  URLRow new_url_row1, new_url_row2;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &new_url_row1));
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[2], &new_url_row2));

  // The visit times should be updated.
  EXPECT_TRUE(new_url_row1.last_visit() == visit_times[1]);
  EXPECT_TRUE(new_url_row2.last_visit().is_null());  // No last visit time.

  // Visit/typed count should not be updated for bookmarks.
  EXPECT_EQ(0, new_url_row1.typed_count());
  EXPECT_EQ(1, new_url_row1.visit_count());
  EXPECT_EQ(0, new_url_row2.typed_count());
  EXPECT_EQ(0, new_url_row2.visit_count());

  // Thumbnails and favicons should still exist. Note that we keep thumbnails
  // that may have been updated since the time threshold. Since the URL still
  // exists in history, this should not be a privacy problem, we only update
  // the visit counts in this case for consistency anyway.
  chrome::FaviconID favicon_id = GetFavicon(url_row1.url(), chrome::FAVICON);
  EXPECT_TRUE(HasFavicon(favicon_id));
  // TODO(sky): fix this, see comment in HasThumbnail.
  // EXPECT_TRUE(HasThumbnail(new_url_row1.id()));
  favicon_id = GetFavicon(url_row1.url(), chrome::FAVICON);
  EXPECT_TRUE(HasFavicon(favicon_id));
  // TODO(sky): fix this, see comment in HasThumbnail.
  // EXPECT_TRUE(HasThumbnail(new_url_row2.id()));
}

TEST_F(ExpireHistoryTest, ArchiveHistoryBeforeUnstarred) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);

  URLRow url_row1, url_row2;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &url_row1));
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[2], &url_row2));

  // Archive the oldest two visits. This will actually result in deleting them
  // since their transition types are empty (not important).
  expirer_.ArchiveHistoryBefore(visit_times[1]);

  // The first URL should be deleted, the second should not be affected.
  URLRow temp_row;
  EXPECT_FALSE(main_db_->GetURLRow(url_ids[0], &temp_row));
  EXPECT_TRUE(main_db_->GetURLRow(url_ids[1], &temp_row));
  EXPECT_TRUE(main_db_->GetURLRow(url_ids[2], &temp_row));

  // Make sure the archived database has nothing in it.
  EXPECT_FALSE(archived_db_->GetRowForURL(url_row1.url(), NULL));
  EXPECT_FALSE(archived_db_->GetRowForURL(url_row2.url(), NULL));

  // Now archive one more visit so that the middle URL should be removed. This
  // one will actually be archived instead of deleted.
  expirer_.ArchiveHistoryBefore(visit_times[2]);
  EXPECT_FALSE(main_db_->GetURLRow(url_ids[1], &temp_row));
  EXPECT_TRUE(main_db_->GetURLRow(url_ids[2], &temp_row));

  // Make sure the archived database has an entry for the second URL.
  URLRow archived_row;
  // Note that the ID is different in the archived DB, so look up by URL.
  EXPECT_TRUE(archived_db_->GetRowForURL(url_row1.url(), &archived_row));
  VisitVector archived_visits;
  archived_db_->GetVisitsForURL(archived_row.id(), &archived_visits);
  EXPECT_EQ(1U, archived_visits.size());
}

TEST_F(ExpireHistoryTest, ArchiveHistoryBeforeStarred) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);

  URLRow url_row0, url_row1;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[0], &url_row0));
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &url_row1));

  // Star the URLs.
  StarURL(url_row0.url());
  StarURL(url_row1.url());

  // Now archive the first three visits (first two URLs). The first two visits
  // should be, the third deleted, but the URL records should not.
  expirer_.ArchiveHistoryBefore(visit_times[2]);

  // The first URL should have its visit deleted, but it should still be present
  // in the main DB and not in the archived one since it is starred.
  URLRow temp_row;
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[0], &temp_row));
  // Note that the ID is different in the archived DB, so look up by URL.
  EXPECT_FALSE(archived_db_->GetRowForURL(temp_row.url(), NULL));
  VisitVector visits;
  main_db_->GetVisitsForURL(temp_row.id(), &visits);
  EXPECT_EQ(0U, visits.size());

  // The second URL should have its first visit deleted and its second visit
  // archived. It should be present in both the main DB (because it's starred)
  // and the archived DB (for the archived visit).
  ASSERT_TRUE(main_db_->GetURLRow(url_ids[1], &temp_row));
  main_db_->GetVisitsForURL(temp_row.id(), &visits);
  EXPECT_EQ(0U, visits.size());

  // Note that the ID is different in the archived DB, so look up by URL.
  ASSERT_TRUE(archived_db_->GetRowForURL(temp_row.url(), &temp_row));
  archived_db_->GetVisitsForURL(temp_row.id(), &visits);
  ASSERT_EQ(1U, visits.size());
  EXPECT_TRUE(visit_times[2] == visits[0].visit_time);

  // The third URL should be unchanged.
  EXPECT_TRUE(main_db_->GetURLRow(url_ids[2], &temp_row));
  EXPECT_FALSE(archived_db_->GetRowForURL(temp_row.url(), NULL));
}

// Tests the return values from ArchiveSomeOldHistory. The rest of the
// functionality of this function is tested by the ArchiveHistoryBefore*
// tests which use this function internally.
TEST_F(ExpireHistoryTest, ArchiveSomeOldHistory) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);
  const ExpiringVisitsReader* reader = expirer_.GetAllVisitsReader();

  // Deleting a time range with no URLs should return false (nothing found).
  EXPECT_FALSE(expirer_.ArchiveSomeOldHistory(
      visit_times[0] - TimeDelta::FromDays(100), reader, 1));

  // Deleting a time range with not up the the max results should also return
  // false (there will only be one visit deleted in this range).
  EXPECT_FALSE(expirer_.ArchiveSomeOldHistory(visit_times[0], reader, 2));

  // Deleting a time range with the max number of results should return true
  // (max deleted).
  EXPECT_TRUE(expirer_.ArchiveSomeOldHistory(visit_times[2], reader, 1));
}

TEST_F(ExpireHistoryTest, ExpiringVisitsReader) {
  URLID url_ids[3];
  Time visit_times[4];
  AddExampleData(url_ids, visit_times);

  const ExpiringVisitsReader* all = expirer_.GetAllVisitsReader();
  const ExpiringVisitsReader* auto_subframes =
      expirer_.GetAutoSubframeVisitsReader();

  VisitVector visits;
  Time now = Time::Now();

  // Verify that the early expiration threshold, stored in the meta table is
  // initialized.
  EXPECT_TRUE(main_db_->GetEarlyExpirationThreshold() ==
      Time::FromInternalValue(1L));

  // First, attempt reading AUTO_SUBFRAME visits. We should get none.
  EXPECT_FALSE(auto_subframes->Read(now, main_db_.get(), &visits, 1));
  EXPECT_EQ(0U, visits.size());

  // Verify that the early expiration threshold was updated, since there are no
  // AUTO_SUBFRAME visits in the given time range.
  EXPECT_TRUE(now <= main_db_->GetEarlyExpirationThreshold());

  // Now, read all visits and verify that there's at least one.
  EXPECT_TRUE(all->Read(now, main_db_.get(), &visits, 1));
  EXPECT_EQ(1U, visits.size());
}

// Tests how ArchiveSomeOldHistory treats source information.
TEST_F(ExpireHistoryTest, ArchiveSomeOldHistoryWithSource) {
  const GURL url("www.testsource.com");
  URLID url_id;
  AddExampleSourceData(url, &url_id);
  const ExpiringVisitsReader* reader = expirer_.GetAllVisitsReader();

  // Archiving all the visits we added.
  ASSERT_FALSE(expirer_.ArchiveSomeOldHistory(Time::Now(), reader, 10));

  URLRow archived_row;
  ASSERT_TRUE(archived_db_->GetRowForURL(url, &archived_row));
  VisitVector archived_visits;
  archived_db_->GetVisitsForURL(archived_row.id(), &archived_visits);
  ASSERT_EQ(4U, archived_visits.size());
  VisitSourceMap sources;
  archived_db_->GetVisitsSource(archived_visits, &sources);
  ASSERT_EQ(3U, sources.size());
  int result = 0;
  VisitSourceMap::iterator iter;
  for (int i = 0; i < 4; i++) {
    iter = sources.find(archived_visits[i].visit_id);
    if (iter == sources.end())
      continue;
    switch (iter->second) {
      case history::SOURCE_EXTENSION:
        result |= 0x1;
        break;
      case history::SOURCE_FIREFOX_IMPORTED:
        result |= 0x2;
        break;
      case history::SOURCE_SYNCED:
        result |= 0x4;
      default:
        break;
    }
  }
  EXPECT_EQ(0x7, result);
  main_db_->GetVisitsSource(archived_visits, &sources);
  EXPECT_EQ(0U, sources.size());
  main_db_->GetVisitsForURL(url_id, &archived_visits);
  EXPECT_EQ(0U, archived_visits.size());
}

// TODO(brettw) add some visits with no URL to make sure everything is updated
// properly. Have the visits also refer to nonexistent FTS rows.
//
// Maybe also refer to invalid favicons.

}  // namespace history
