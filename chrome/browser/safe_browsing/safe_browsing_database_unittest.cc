// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for the SafeBrowsing storage system.

#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/time/time.h"
#include "chrome/browser/safe_browsing/safe_browsing_database.h"
#include "chrome/browser/safe_browsing/safe_browsing_store_file.h"
#include "chrome/browser/safe_browsing/safe_browsing_store_unittest_helper.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "crypto/sha2.h"
#include "sql/connection.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"
#include "url/gurl.h"

using base::Time;

namespace {

SBPrefix Sha256Prefix(const std::string& str) {
  SBPrefix prefix;
  crypto::SHA256HashString(str, &prefix, sizeof(prefix));
  return prefix;
}

SBFullHash Sha256Hash(const std::string& str) {
  SBFullHash hash;
  crypto::SHA256HashString(str, &hash, sizeof(hash));
  return hash;
}

// Same as InsertAddChunkHostPrefixUrl, but with pre-computed
// prefix values.
void InsertAddChunkHostPrefixValue(SBChunk* chunk,
                                   int chunk_number,
                                   const SBPrefix& host_prefix,
                                   const SBPrefix& url_prefix) {
  chunk->chunk_number = chunk_number;
  chunk->is_add = true;
  SBChunkHost host;
  host.host = host_prefix;
  host.entry = SBEntry::Create(SBEntry::ADD_PREFIX, 1);
  host.entry->set_chunk_id(chunk->chunk_number);
  host.entry->SetPrefixAt(0, url_prefix);
  chunk->hosts.push_back(host);
}

// A helper function that appends one AddChunkHost to chunk with
// one url for prefix.
void InsertAddChunkHostPrefixUrl(SBChunk* chunk,
                                 int chunk_number,
                                 const std::string& host_name,
                                 const std::string& url) {
  InsertAddChunkHostPrefixValue(chunk, chunk_number,
                                Sha256Prefix(host_name),
                                Sha256Prefix(url));
}

// Same as InsertAddChunkHostPrefixUrl, but with full hashes.
void InsertAddChunkHostFullHashes(SBChunk* chunk,
                                  int chunk_number,
                                  const std::string& host_name,
                                  const std::string& url) {
  chunk->chunk_number = chunk_number;
  chunk->is_add = true;
  SBChunkHost host;
  host.host = Sha256Prefix(host_name);
  host.entry = SBEntry::Create(SBEntry::ADD_FULL_HASH, 1);
  host.entry->set_chunk_id(chunk->chunk_number);
  host.entry->SetFullHashAt(0, Sha256Hash(url));
  chunk->hosts.push_back(host);
}

// Same as InsertAddChunkHostPrefixUrl, but with two urls for prefixes.
void InsertAddChunkHost2PrefixUrls(SBChunk* chunk,
                                   int chunk_number,
                                   const std::string& host_name,
                                   const std::string& url1,
                                   const std::string& url2) {
  chunk->chunk_number = chunk_number;
  chunk->is_add = true;
  SBChunkHost host;
  host.host = Sha256Prefix(host_name);
  host.entry = SBEntry::Create(SBEntry::ADD_PREFIX, 2);
  host.entry->set_chunk_id(chunk->chunk_number);
  host.entry->SetPrefixAt(0, Sha256Prefix(url1));
  host.entry->SetPrefixAt(1, Sha256Prefix(url2));
  chunk->hosts.push_back(host);
}

// Same as InsertAddChunkHost2PrefixUrls, but with full hashes.
void InsertAddChunkHost2FullHashes(SBChunk* chunk,
                                   int chunk_number,
                                   const std::string& host_name,
                                   const std::string& url1,
                                   const std::string& url2) {
  chunk->chunk_number = chunk_number;
  chunk->is_add = true;
  SBChunkHost host;
  host.host = Sha256Prefix(host_name);
  host.entry = SBEntry::Create(SBEntry::ADD_FULL_HASH, 2);
  host.entry->set_chunk_id(chunk->chunk_number);
  host.entry->SetFullHashAt(0, Sha256Hash(url1));
  host.entry->SetFullHashAt(1, Sha256Hash(url2));
  chunk->hosts.push_back(host);
}

// Same as InsertSubChunkHostPrefixUrl, but with pre-computed
// prefix values.
void InsertSubChunkHostPrefixValue(SBChunk* chunk,
                                   int chunk_number,
                                   int chunk_id_to_sub,
                                   const SBPrefix& host_prefix,
                                   const SBPrefix& url_prefix) {
  chunk->chunk_number = chunk_number;
  chunk->is_add = false;
  SBChunkHost host;
  host.host = host_prefix;
  host.entry = SBEntry::Create(SBEntry::SUB_PREFIX, 1);
  host.entry->set_chunk_id(chunk->chunk_number);
  host.entry->SetChunkIdAtPrefix(0, chunk_id_to_sub);
  host.entry->SetPrefixAt(0, url_prefix);
  chunk->hosts.push_back(host);
}

// A helper function that adds one SubChunkHost to chunk with
// one url for prefix.
void InsertSubChunkHostPrefixUrl(SBChunk* chunk,
                                 int chunk_number,
                                 int chunk_id_to_sub,
                                 const std::string& host_name,
                                 const std::string& url) {
  InsertSubChunkHostPrefixValue(chunk, chunk_number,
                                chunk_id_to_sub,
                                Sha256Prefix(host_name),
                                Sha256Prefix(url));
}

// Same as InsertSubChunkHostPrefixUrl, but with two urls for prefixes.
void InsertSubChunkHost2PrefixUrls(SBChunk* chunk,
                                   int chunk_number,
                                   int chunk_id_to_sub,
                                   const std::string& host_name,
                                   const std::string& url1,
                                   const std::string& url2) {
  chunk->chunk_number = chunk_number;
  chunk->is_add = false;
  SBChunkHost host;
  host.host = Sha256Prefix(host_name);
  host.entry = SBEntry::Create(SBEntry::SUB_PREFIX, 2);
  host.entry->set_chunk_id(chunk->chunk_number);
  host.entry->SetPrefixAt(0, Sha256Prefix(url1));
  host.entry->SetChunkIdAtPrefix(0, chunk_id_to_sub);
  host.entry->SetPrefixAt(1, Sha256Prefix(url2));
  host.entry->SetChunkIdAtPrefix(1, chunk_id_to_sub);
  chunk->hosts.push_back(host);
}

// Same as InsertSubChunkHost2PrefixUrls, but with full hashes.
void InsertSubChunkHostFullHash(SBChunk* chunk,
                                int chunk_number,
                                int chunk_id_to_sub,
                                const std::string& host_name,
                                const std::string& url) {
  chunk->chunk_number = chunk_number;
  chunk->is_add = false;
  SBChunkHost host;
  host.host = Sha256Prefix(host_name);
  host.entry = SBEntry::Create(SBEntry::SUB_FULL_HASH, 2);
  host.entry->set_chunk_id(chunk->chunk_number);
  host.entry->SetFullHashAt(0, Sha256Hash(url));
  host.entry->SetChunkIdAtPrefix(0, chunk_id_to_sub);
  chunk->hosts.push_back(host);
}

// Prevent DCHECK from killing tests.
// TODO(shess): Pawel disputes the use of this, so the test which uses
// it is DISABLED.  http://crbug.com/56448
class ScopedLogMessageIgnorer {
 public:
  ScopedLogMessageIgnorer() {
    logging::SetLogMessageHandler(&LogMessageIgnorer);
  }
  ~ScopedLogMessageIgnorer() {
    // TODO(shess): Would be better to verify whether anyone else
    // changed it, and then restore it to the previous value.
    logging::SetLogMessageHandler(NULL);
  }

 private:
  static bool LogMessageIgnorer(int severity, const char* file, int line,
      size_t message_start, const std::string& str) {
    // Intercept FATAL, strip the stack backtrace, and log it without
    // the crash part.
    if (severity == logging::LOG_FATAL) {
      size_t newline = str.find('\n');
      if (newline != std::string::npos) {
        const std::string msg = str.substr(0, newline + 1);
        fprintf(stderr, "%s", msg.c_str());
        fflush(stderr);
      }
      return true;
    }

    return false;
  }
};

}  // namespace

class SafeBrowsingDatabaseTest : public PlatformTest {
 public:
  virtual void SetUp() {
    PlatformTest::SetUp();

    // Setup a database in a temporary directory.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    database_.reset(new SafeBrowsingDatabaseNew);
    database_filename_ =
        temp_dir_.path().AppendASCII("SafeBrowsingTestDatabase");
    database_->Init(database_filename_);
  }

  virtual void TearDown() {
    database_.reset();

    PlatformTest::TearDown();
  }

  void GetListsInfo(std::vector<SBListChunkRanges>* lists) {
    lists->clear();
    EXPECT_TRUE(database_->UpdateStarted(lists));
    database_->UpdateFinished(true);
  }

  // Helper function to do an AddDel or SubDel command.
  void DelChunk(const std::string& list,
                int chunk_id,
                bool is_sub_del) {
    std::vector<SBChunkDelete> deletes;
    SBChunkDelete chunk_delete;
    chunk_delete.list_name = list;
    chunk_delete.is_sub_del = is_sub_del;
    chunk_delete.chunk_del.push_back(ChunkRange(chunk_id));
    deletes.push_back(chunk_delete);
    database_->DeleteChunks(deletes);
  }

  void AddDelChunk(const std::string& list, int chunk_id) {
    DelChunk(list, chunk_id, false);
  }

  void SubDelChunk(const std::string& list, int chunk_id) {
    DelChunk(list, chunk_id, true);
  }

  // Utility function for setting up the database for the caching test.
  void PopulateDatabaseForCacheTest();

  scoped_ptr<SafeBrowsingDatabaseNew> database_;
  base::FilePath database_filename_;
  base::ScopedTempDir temp_dir_;
};

// Tests retrieving list name information.
TEST_F(SafeBrowsingDatabaseTest, ListNameForBrowse) {
  SBChunkList chunks;
  SBChunk chunk;

  InsertAddChunkHostPrefixUrl(&chunk, 1, "www.evil.com/",
                              "www.evil.com/malware.html");
  chunks.clear();
  chunks.push_back(chunk);
  std::vector<SBListChunkRanges> lists;
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);

  chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&chunk, 2, "www.foo.com/",
                              "www.foo.com/malware.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);

  chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&chunk, 3, "www.whatever.com/",
                              "www.whatever.com/malware.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  EXPECT_TRUE(lists[0].name == safe_browsing_util::kMalwareList);
  EXPECT_EQ(lists[0].adds, "1-3");
  EXPECT_TRUE(lists[0].subs.empty());

  // Insert a malware sub chunk.
  chunk.hosts.clear();
  InsertSubChunkHostPrefixUrl(&chunk, 7, 19, "www.subbed.com/",
                              "www.subbed.com/noteveil1.html");
  chunks.clear();
  chunks.push_back(chunk);

  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  EXPECT_TRUE(lists[0].name == safe_browsing_util::kMalwareList);
  EXPECT_EQ(lists[0].adds, "1-3");
  EXPECT_EQ(lists[0].subs, "7");
  if (lists.size() == 2) {
    // Old style database won't have the second entry since it creates the lists
    // when it receives an update containing that list. The filter-based
    // database has these values hard coded.
    EXPECT_TRUE(lists[1].name == safe_browsing_util::kPhishingList);
    EXPECT_TRUE(lists[1].adds.empty());
    EXPECT_TRUE(lists[1].subs.empty());
  }

  // Add a phishing add chunk.
  chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&chunk, 47, "www.evil.com/",
                              "www.evil.com/phishing.html");
  chunks.clear();
  chunks.push_back(chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kPhishingList, chunks);

  // Insert some phishing sub chunks.
  chunk.hosts.clear();
  InsertSubChunkHostPrefixUrl(&chunk, 200, 1999, "www.phishy.com/",
                              "www.phishy.com/notevil1.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kPhishingList, chunks);

  chunk.hosts.clear();
  InsertSubChunkHostPrefixUrl(&chunk, 201, 1999, "www.phishy2.com/",
                              "www.phishy2.com/notevil1.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kPhishingList, chunks);
  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  EXPECT_TRUE(lists[0].name == safe_browsing_util::kMalwareList);
  EXPECT_EQ(lists[0].adds, "1-3");
  EXPECT_EQ(lists[0].subs, "7");
  EXPECT_TRUE(lists[1].name == safe_browsing_util::kPhishingList);
  EXPECT_EQ(lists[1].adds, "47");
  EXPECT_EQ(lists[1].subs, "200-201");
}

TEST_F(SafeBrowsingDatabaseTest, ListNameForBrowseAndDownload) {
  database_.reset();
  base::MessageLoop loop(base::MessageLoop::TYPE_DEFAULT);
  SafeBrowsingStoreFile* browse_store = new SafeBrowsingStoreFile();
  SafeBrowsingStoreFile* download_store = new SafeBrowsingStoreFile();
  SafeBrowsingStoreFile* csd_whitelist_store = new SafeBrowsingStoreFile();
  SafeBrowsingStoreFile* download_whitelist_store = new SafeBrowsingStoreFile();
  SafeBrowsingStoreFile* extension_blacklist_store =
      new SafeBrowsingStoreFile();
  database_.reset(new SafeBrowsingDatabaseNew(browse_store,
                                              download_store,
                                              csd_whitelist_store,
                                              download_whitelist_store,
                                              extension_blacklist_store,
                                              NULL));
  database_->Init(database_filename_);

  SBChunkList chunks;
  SBChunk chunk;

  // Insert malware, phish, binurl and bindownload add chunks.
  InsertAddChunkHostPrefixUrl(&chunk, 1, "www.evil.com/",
                              "www.evil.com/malware.html");
  chunks.push_back(chunk);
  std::vector<SBListChunkRanges> lists;
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);

  chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&chunk, 2, "www.foo.com/",
                              "www.foo.com/malware.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kPhishingList, chunks);

  chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&chunk, 3, "www.whatever.com/",
                              "www.whatever.com/download.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kBinUrlList, chunks);

  chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&chunk, 4, "www.forhash.com/",
                              "www.forhash.com/download.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kBinHashList, chunks);

  chunk.hosts.clear();
  InsertAddChunkHostFullHashes(&chunk, 5, "www.forwhitelist.com/",
                               "www.forwhitelist.com/a.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kCsdWhiteList, chunks);

  chunk.hosts.clear();
  InsertAddChunkHostFullHashes(&chunk, 6, "www.download.com/",
                               "www.download.com/");

  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kDownloadWhiteList, chunks);


  chunk.hosts.clear();
  InsertAddChunkHostFullHashes(&chunk, 8,
                               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kExtensionBlacklist, chunks);

  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  ASSERT_EQ(6U, lists.size());
  EXPECT_TRUE(lists[0].name == safe_browsing_util::kMalwareList);
  EXPECT_EQ(lists[0].adds, "1");
  EXPECT_TRUE(lists[0].subs.empty());
  EXPECT_TRUE(lists[1].name == safe_browsing_util::kPhishingList);
  EXPECT_EQ(lists[1].adds, "2");
  EXPECT_TRUE(lists[1].subs.empty());
  EXPECT_TRUE(lists[2].name == safe_browsing_util::kBinUrlList);
  EXPECT_EQ(lists[2].adds, "3");
  EXPECT_TRUE(lists[2].subs.empty());
  // kBinHashList is ignored.  (http://crbug.com/108130)
  EXPECT_TRUE(lists[3].name == safe_browsing_util::kCsdWhiteList);
  EXPECT_EQ(lists[3].adds, "5");
  EXPECT_TRUE(lists[3].subs.empty());
  EXPECT_TRUE(lists[4].name == safe_browsing_util::kDownloadWhiteList);
  EXPECT_EQ(lists[4].adds, "6");
  EXPECT_TRUE(lists[4].subs.empty());
  EXPECT_TRUE(lists[5].name == safe_browsing_util::kExtensionBlacklist);
  EXPECT_EQ(lists[5].adds, "8");
  EXPECT_TRUE(lists[5].subs.empty());
  database_.reset();
}

// Checks database reading and writing for browse.
TEST_F(SafeBrowsingDatabaseTest, BrowseDatabase) {
  SBChunkList chunks;
  SBChunk chunk;

  // Add a simple chunk with one hostkey.
  InsertAddChunkHost2PrefixUrls(&chunk, 1, "www.evil.com/",
                                "www.evil.com/phishing.html",
                                "www.evil.com/malware.html");
  chunks.push_back(chunk);
  std::vector<SBListChunkRanges> lists;
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);

  chunk.hosts.clear();
  InsertAddChunkHost2PrefixUrls(&chunk, 2, "www.evil.com/",
                                "www.evil.com/notevil1.html",
                                "www.evil.com/notevil2.html");
  InsertAddChunkHost2PrefixUrls(&chunk, 2, "www.good.com/",
                                "www.good.com/good1.html",
                                "www.good.com/good2.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);

  // and a chunk with an IP-based host
  chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&chunk, 3, "192.168.0.1/",
                              "192.168.0.1/malware.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  // Make sure they were added correctly.
  GetListsInfo(&lists);
  EXPECT_TRUE(lists[0].name == safe_browsing_util::kMalwareList);
  EXPECT_EQ(lists[0].adds, "1-3");
  EXPECT_TRUE(lists[0].subs.empty());

  const Time now = Time::Now();
  std::vector<SBFullHashResult> full_hashes;
  std::vector<SBPrefix> prefix_hits;
  std::string matching_list;
  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/phishing.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));
  EXPECT_EQ(prefix_hits[0], Sha256Prefix("www.evil.com/phishing.html"));
  EXPECT_EQ(prefix_hits.size(), 1U);

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/notevil1.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/notevil2.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.good.com/good1.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.good.com/good2.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://192.168.0.1/malware.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/"),
      &matching_list, &prefix_hits,
      &full_hashes, now));
  EXPECT_TRUE(prefix_hits.empty());

  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/robots.txt"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  // Attempt to re-add the first chunk (should be a no-op).
  // see bug: http://code.google.com/p/chromium/issues/detail?id=4522
  chunk.hosts.clear();
  InsertAddChunkHost2PrefixUrls(&chunk, 1, "www.evil.com/",
                                "www.evil.com/phishing.html",
                                "www.evil.com/malware.html");
  chunks.clear();
  chunks.push_back(chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  EXPECT_TRUE(lists[0].name == safe_browsing_util::kMalwareList);
  EXPECT_EQ(lists[0].adds, "1-3");
  EXPECT_TRUE(lists[0].subs.empty());

  // Test removing a single prefix from the add chunk.
  chunk.hosts.clear();
  InsertSubChunkHostPrefixUrl(&chunk, 4, 2, "www.evil.com/",
                              "www.evil.com/notevil1.html");
  chunks.clear();
  chunks.push_back(chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);

  database_->UpdateFinished(true);

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/phishing.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));
  EXPECT_EQ(prefix_hits[0], Sha256Prefix("www.evil.com/phishing.html"));
  EXPECT_EQ(prefix_hits.size(), 1U);

  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/notevil1.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));
  EXPECT_TRUE(prefix_hits.empty());

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/notevil2.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.good.com/good1.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.good.com/good2.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  GetListsInfo(&lists);
  EXPECT_TRUE(lists[0].name == safe_browsing_util::kMalwareList);
  EXPECT_EQ(lists[0].subs, "4");

  // Test the same sub chunk again.  This should be a no-op.
  // see bug: http://code.google.com/p/chromium/issues/detail?id=4522
  chunk.hosts.clear();
  InsertSubChunkHostPrefixUrl(&chunk, 4, 2, "www.evil.com/",
                              "www.evil.com/notevil1.html");
  chunks.clear();
  chunks.push_back(chunk);

  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  EXPECT_TRUE(lists[0].name == safe_browsing_util::kMalwareList);
  EXPECT_EQ(lists[0].subs, "4");

  // Test removing all the prefixes from an add chunk.
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  AddDelChunk(safe_browsing_util::kMalwareList, 2);
  database_->UpdateFinished(true);

  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/notevil2.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.good.com/good1.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.good.com/good2.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  GetListsInfo(&lists);
  EXPECT_TRUE(lists[0].name == safe_browsing_util::kMalwareList);
  EXPECT_EQ(lists[0].adds, "1,3");
  EXPECT_EQ(lists[0].subs, "4");

  // The adddel command exposed a bug in the transaction code where any
  // transaction after it would fail.  Add a dummy entry and remove it to
  // make sure the transcation works fine.
  chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&chunk, 44, "www.redherring.com/",
                              "www.redherring.com/index.html");
  chunks.clear();
  chunks.push_back(chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);

  // Now remove the dummy entry.  If there are any problems with the
  // transactions, asserts will fire.
  AddDelChunk(safe_browsing_util::kMalwareList, 44);

  // Test the subdel command.
  SubDelChunk(safe_browsing_util::kMalwareList, 4);
  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  EXPECT_TRUE(lists[0].name == safe_browsing_util::kMalwareList);
  EXPECT_EQ(lists[0].adds, "1,3");
  EXPECT_EQ(lists[0].subs, "");

  // Test a sub command coming in before the add.
  chunk.hosts.clear();
  InsertSubChunkHost2PrefixUrls(&chunk, 5, 10,
                                "www.notevilanymore.com/",
                                "www.notevilanymore.com/index.html",
                                "www.notevilanymore.com/good.html");
  chunks.clear();
  chunks.push_back(chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.notevilanymore.com/index.html"),
      &matching_list, &prefix_hits, &full_hashes, now));

  // Now insert the tardy add chunk and we don't expect them to appear
  // in database because of the previous sub chunk.
  chunk.hosts.clear();
  InsertAddChunkHost2PrefixUrls(&chunk, 10, "www.notevilanymore.com/",
                                "www.notevilanymore.com/index.html",
                                "www.notevilanymore.com/good.html");
  chunks.clear();
  chunks.push_back(chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.notevilanymore.com/index.html"),
      &matching_list, &prefix_hits, &full_hashes, now));

  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.notevilanymore.com/good.html"),
      &matching_list, &prefix_hits, &full_hashes, now));
}


// Test adding zero length chunks to the database.
TEST_F(SafeBrowsingDatabaseTest, ZeroSizeChunk) {
  SBChunkList chunks;
  SBChunk chunk;

  // Populate with a couple of normal chunks.
  InsertAddChunkHost2PrefixUrls(&chunk, 1, "www.test.com/",
                                "www.test.com/test1.html",
                                "www.test.com/test2.html");
  chunks.clear();
  chunks.push_back(chunk);

  chunk.hosts.clear();
  InsertAddChunkHost2PrefixUrls(&chunk, 10, "www.random.com/",
                                "www.random.com/random1.html",
                                "www.random.com/random2.html");
  chunks.push_back(chunk);

  std::vector<SBListChunkRanges> lists;
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  // Add an empty ADD and SUB chunk.
  GetListsInfo(&lists);
  EXPECT_EQ(lists[0].adds, "1,10");

  SBChunk empty_chunk;
  empty_chunk.chunk_number = 19;
  empty_chunk.is_add = true;
  chunks.clear();
  chunks.push_back(empty_chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  chunks.clear();
  empty_chunk.chunk_number = 7;
  empty_chunk.is_add = false;
  chunks.push_back(empty_chunk);
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  EXPECT_EQ(lists[0].adds, "1,10,19");
  EXPECT_EQ(lists[0].subs, "7");

  // Add an empty chunk along with a couple that contain data. This should
  // result in the chunk range being reduced in size.
  empty_chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&empty_chunk, 20, "www.notempy.com/",
                              "www.notempty.com/full1.html");
  chunks.clear();
  chunks.push_back(empty_chunk);

  empty_chunk.chunk_number = 21;
  empty_chunk.is_add = true;
  empty_chunk.hosts.clear();
  chunks.push_back(empty_chunk);

  empty_chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&empty_chunk, 22, "www.notempy.com/",
                              "www.notempty.com/full2.html");
  chunks.push_back(empty_chunk);

  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  const Time now = Time::Now();
  std::vector<SBFullHashResult> full_hashes;
  std::vector<SBPrefix> prefix_hits;
  std::string matching_list;
  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.notempty.com/full1.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));
  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.notempty.com/full2.html"),
      &matching_list, &prefix_hits,
      &full_hashes, now));

  GetListsInfo(&lists);
  EXPECT_EQ(lists[0].adds, "1,10,19-22");
  EXPECT_EQ(lists[0].subs, "7");

  // Handle AddDel and SubDel commands for empty chunks.
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  AddDelChunk(safe_browsing_util::kMalwareList, 21);
  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  EXPECT_EQ(lists[0].adds, "1,10,19-20,22");
  EXPECT_EQ(lists[0].subs, "7");

  EXPECT_TRUE(database_->UpdateStarted(&lists));
  SubDelChunk(safe_browsing_util::kMalwareList, 7);
  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  EXPECT_EQ(lists[0].adds, "1,10,19-20,22");
  EXPECT_EQ(lists[0].subs, "");
}

// Utility function for setting up the database for the caching test.
void SafeBrowsingDatabaseTest::PopulateDatabaseForCacheTest() {
  SBChunkList chunks;
  SBChunk chunk;
  // Add a simple chunk with one hostkey and cache it.
  InsertAddChunkHost2PrefixUrls(&chunk, 1, "www.evil.com/",
                                "www.evil.com/phishing.html",
                                "www.evil.com/malware.html");
  chunks.push_back(chunk);

  std::vector<SBListChunkRanges> lists;
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  // Add the GetHash results to the cache.
  SBFullHashResult full_hash;
  full_hash.hash = Sha256Hash("www.evil.com/phishing.html");
  full_hash.list_name = safe_browsing_util::kMalwareList;
  full_hash.add_chunk_id = 1;

  std::vector<SBFullHashResult> results;
  results.push_back(full_hash);

  full_hash.hash = Sha256Hash("www.evil.com/malware.html");
  results.push_back(full_hash);

  std::vector<SBPrefix> prefixes;
  database_->CacheHashResults(prefixes, results);
}

TEST_F(SafeBrowsingDatabaseTest, HashCaching) {
  PopulateDatabaseForCacheTest();

  // We should have both full hashes in the cache.
  EXPECT_EQ(database_->pending_browse_hashes_.size(), 2U);

  // Test the cache lookup for the first prefix.
  std::string listname;
  std::vector<SBPrefix> prefixes;
  std::vector<SBFullHashResult> full_hashes;
  database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/phishing.html"),
      &listname, &prefixes, &full_hashes, Time::Now());
  EXPECT_EQ(full_hashes.size(), 1U);
  EXPECT_TRUE(SBFullHashEq(full_hashes[0].hash,
                           Sha256Hash("www.evil.com/phishing.html")));

  prefixes.clear();
  full_hashes.clear();

  // Test the cache lookup for the second prefix.
  database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware.html"),
      &listname, &prefixes, &full_hashes, Time::Now());
  EXPECT_EQ(full_hashes.size(), 1U);
  EXPECT_TRUE(SBFullHashEq(full_hashes[0].hash,
                           Sha256Hash("www.evil.com/malware.html")));

  prefixes.clear();
  full_hashes.clear();

  // Test removing a prefix via a sub chunk.
  SBChunk chunk;
  SBChunkList chunks;
  InsertSubChunkHostPrefixUrl(&chunk, 2, 1, "www.evil.com/",
                              "www.evil.com/phishing.html");
  chunks.push_back(chunk);

  std::vector<SBListChunkRanges> lists;
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  // This prefix should still be there.
  database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware.html"),
      &listname, &prefixes, &full_hashes, Time::Now());
  EXPECT_EQ(full_hashes.size(), 1U);
  EXPECT_TRUE(SBFullHashEq(full_hashes[0].hash,
                           Sha256Hash("www.evil.com/malware.html")));
  prefixes.clear();
  full_hashes.clear();

  // This prefix should be gone.
  database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/phishing.html"),
      &listname, &prefixes, &full_hashes, Time::Now());
  EXPECT_TRUE(full_hashes.empty());

  prefixes.clear();
  full_hashes.clear();

  // Test that an AddDel for the original chunk removes the last cached entry.
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  AddDelChunk(safe_browsing_util::kMalwareList, 1);
  database_->UpdateFinished(true);
  database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware.html"),
      &listname, &prefixes, &full_hashes, Time::Now());
  EXPECT_TRUE(full_hashes.empty());
  EXPECT_TRUE(database_->full_browse_hashes_.empty());
  EXPECT_TRUE(database_->pending_browse_hashes_.empty());

  prefixes.clear();
  full_hashes.clear();

  // Test that the cache won't return expired values. First we have to adjust
  // the cached entries' received time to make them older, since the database
  // cache insert uses Time::Now(). First, store some entries.
  PopulateDatabaseForCacheTest();

  std::vector<SBAddFullHash>* hash_cache = &database_->pending_browse_hashes_;
  EXPECT_EQ(hash_cache->size(), 2U);

  // Now adjust one of the entries times to be in the past.
  base::Time expired = base::Time::Now() - base::TimeDelta::FromMinutes(60);
  const SBPrefix key = Sha256Prefix("www.evil.com/malware.html");
  std::vector<SBAddFullHash>::iterator iter;
  for (iter = hash_cache->begin(); iter != hash_cache->end(); ++iter) {
    if (iter->full_hash.prefix == key) {
      iter->received = static_cast<int32>(expired.ToTimeT());
      break;
    }
  }
  EXPECT_TRUE(iter != hash_cache->end());

  database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware.html"),
      &listname, &prefixes, &full_hashes, expired);
  EXPECT_TRUE(full_hashes.empty());

  // This entry should still exist.
  database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/phishing.html"),
      &listname, &prefixes, &full_hashes, expired);
  EXPECT_EQ(full_hashes.size(), 1U);


  // Testing prefix miss caching. First, we clear out the existing database,
  // Since PopulateDatabaseForCacheTest() doesn't handle adding duplicate
  // chunks.
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  AddDelChunk(safe_browsing_util::kMalwareList, 1);
  database_->UpdateFinished(true);

  std::vector<SBPrefix> prefix_misses;
  std::vector<SBFullHashResult> empty_full_hash;
  prefix_misses.push_back(Sha256Prefix("http://www.bad.com/malware.html"));
  prefix_misses.push_back(Sha256Prefix("http://www.bad.com/phishing.html"));
  database_->CacheHashResults(prefix_misses, empty_full_hash);

  // Prefixes with no full results are misses.
  EXPECT_EQ(database_->prefix_miss_cache_.size(), 2U);

  // Update the database.
  PopulateDatabaseForCacheTest();

  // Prefix miss cache should be cleared.
  EXPECT_TRUE(database_->prefix_miss_cache_.empty());

  // Cache a GetHash miss for a particular prefix, and even though the prefix is
  // in the database, it is flagged as a miss so looking up the associated URL
  // will not succeed.
  prefixes.clear();
  full_hashes.clear();
  prefix_misses.clear();
  empty_full_hash.clear();
  prefix_misses.push_back(Sha256Prefix("www.evil.com/phishing.html"));
  database_->CacheHashResults(prefix_misses, empty_full_hash);
  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/phishing.html"),
      &listname, &prefixes,
      &full_hashes, Time::Now()));

  prefixes.clear();
  full_hashes.clear();

  // Test receiving a full add chunk.
  chunk.hosts.clear();
  InsertAddChunkHost2FullHashes(&chunk, 20, "www.fullevil.com/",
                                "www.fullevil.com/bad1.html",
                                "www.fullevil.com/bad2.html");
  chunks.clear();
  chunks.push_back(chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.fullevil.com/bad1.html"),
      &listname, &prefixes, &full_hashes,
      Time::Now()));
  EXPECT_EQ(full_hashes.size(), 1U);
  EXPECT_TRUE(SBFullHashEq(full_hashes[0].hash,
                           Sha256Hash("www.fullevil.com/bad1.html")));
  prefixes.clear();
  full_hashes.clear();

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.fullevil.com/bad2.html"),
      &listname, &prefixes, &full_hashes,
      Time::Now()));
  EXPECT_EQ(full_hashes.size(), 1U);
  EXPECT_TRUE(SBFullHashEq(full_hashes[0].hash,
                           Sha256Hash("www.fullevil.com/bad2.html")));
  prefixes.clear();
  full_hashes.clear();

  // Test receiving a full sub chunk, which will remove one of the full adds.
  chunk.hosts.clear();
  InsertSubChunkHostFullHash(&chunk, 200, 20,
                             "www.fullevil.com/",
                             "www.fullevil.com/bad1.html");
  chunks.clear();
  chunks.push_back(chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.fullevil.com/bad1.html"),
      &listname, &prefixes, &full_hashes,
      Time::Now()));
  EXPECT_TRUE(full_hashes.empty());

  // There should be one remaining full add.
  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.fullevil.com/bad2.html"),
      &listname, &prefixes, &full_hashes,
      Time::Now()));
  EXPECT_EQ(full_hashes.size(), 1U);
  EXPECT_TRUE(SBFullHashEq(full_hashes[0].hash,
                           Sha256Hash("www.fullevil.com/bad2.html")));
  prefixes.clear();
  full_hashes.clear();

  // Now test an AddDel for the remaining full add.
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  AddDelChunk(safe_browsing_util::kMalwareList, 20);
  database_->UpdateFinished(true);

  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.fullevil.com/bad1.html"),
      &listname, &prefixes, &full_hashes,
      Time::Now()));
  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.fullevil.com/bad2.html"),
      &listname, &prefixes, &full_hashes,
      Time::Now()));
}

// Test that corrupt databases are appropriately handled, even if the
// corruption is detected in the midst of the update.
// TODO(shess): Disabled until ScopedLogMessageIgnorer resolved.
// http://crbug.com/56448
TEST_F(SafeBrowsingDatabaseTest, DISABLED_FileCorruptionHandling) {
  // Re-create the database in a captive message loop so that we can
  // influence task-posting.  Database specifically needs to the
  // file-backed.
  database_.reset();
  base::MessageLoop loop(base::MessageLoop::TYPE_DEFAULT);
  SafeBrowsingStoreFile* store = new SafeBrowsingStoreFile();
  database_.reset(new SafeBrowsingDatabaseNew(store, NULL, NULL, NULL, NULL,
                                              NULL));
  database_->Init(database_filename_);

  // This will cause an empty database to be created.
  std::vector<SBListChunkRanges> lists;
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->UpdateFinished(true);

  // Create a sub chunk to insert.
  SBChunkList chunks;
  SBChunk chunk;
  SBChunkHost host;
  host.host = Sha256Prefix("www.subbed.com/");
  host.entry = SBEntry::Create(SBEntry::SUB_PREFIX, 1);
  host.entry->set_chunk_id(7);
  host.entry->SetChunkIdAtPrefix(0, 19);
  host.entry->SetPrefixAt(0, Sha256Prefix("www.subbed.com/notevil1.html"));
  chunk.chunk_number = 7;
  chunk.is_add = false;
  chunk.hosts.clear();
  chunk.hosts.push_back(host);
  chunks.clear();
  chunks.push_back(chunk);

  // Corrupt the file by corrupting the checksum, which is not checked
  // until the entire table is read in |UpdateFinished()|.
  FILE* fp = file_util::OpenFile(database_filename_, "r+");
  ASSERT_TRUE(fp);
  ASSERT_NE(-1, fseek(fp, -8, SEEK_END));
  for (size_t i = 0; i < 8; ++i) {
    fputc('!', fp);
  }
  fclose(fp);

  {
    // The following code will cause DCHECKs, so suppress the crashes.
    ScopedLogMessageIgnorer ignorer;

    // Start an update.  The insert will fail due to corruption.
    EXPECT_TRUE(database_->UpdateStarted(&lists));
    database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
    database_->UpdateFinished(true);

    // Database file still exists until the corruption handler has run.
    EXPECT_TRUE(base::PathExists(database_filename_));

    // Flush through the corruption-handler task.
    VLOG(1) << "Expect failed check on: SafeBrowsing database reset";
    base::MessageLoop::current()->RunUntilIdle();
  }

  // Database file should not exist.
  EXPECT_FALSE(base::PathExists(database_filename_));

  // Run the update again successfully.
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);
  EXPECT_TRUE(base::PathExists(database_filename_));

  database_.reset();
}

// Checks database reading and writing.
TEST_F(SafeBrowsingDatabaseTest, ContainsDownloadUrl) {
  database_.reset();
  base::MessageLoop loop(base::MessageLoop::TYPE_DEFAULT);
  SafeBrowsingStoreFile* browse_store = new SafeBrowsingStoreFile();
  SafeBrowsingStoreFile* download_store = new SafeBrowsingStoreFile();
  SafeBrowsingStoreFile* csd_whitelist_store = new SafeBrowsingStoreFile();
  database_.reset(new SafeBrowsingDatabaseNew(browse_store,
                                              download_store,
                                              csd_whitelist_store,
                                              NULL,
                                              NULL,
                                              NULL));
  database_->Init(database_filename_);

  const char kEvil1Host[] = "www.evil1.com/";
  const char kEvil1Url1[] = "www.evil1.com/download1/";
  const char kEvil1Url2[] = "www.evil1.com/download2.html";

  SBChunkList chunks;
  SBChunk chunk;
  // Add a simple chunk with one hostkey for download url list.
  InsertAddChunkHost2PrefixUrls(&chunk, 1, kEvil1Host,
                                kEvil1Url1, kEvil1Url2);
  chunks.push_back(chunk);
  std::vector<SBListChunkRanges> lists;
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kBinUrlList, chunks);
  database_->UpdateFinished(true);

  std::vector<SBPrefix> prefix_hits;
  std::vector<GURL> urls(1);

  urls[0] = GURL(std::string("http://") + kEvil1Url1);
  EXPECT_TRUE(database_->ContainsDownloadUrl(urls, &prefix_hits));
  ASSERT_EQ(prefix_hits.size(), 1U);
  EXPECT_EQ(prefix_hits[0], Sha256Prefix(kEvil1Url1));

  urls[0] = GURL(std::string("http://") + kEvil1Url2);
  EXPECT_TRUE(database_->ContainsDownloadUrl(urls, &prefix_hits));
  ASSERT_EQ(prefix_hits.size(), 1U);
  EXPECT_EQ(prefix_hits[0], Sha256Prefix(kEvil1Url2));

  urls[0] = GURL(std::string("https://") + kEvil1Url2);
  EXPECT_TRUE(database_->ContainsDownloadUrl(urls, &prefix_hits));
  ASSERT_EQ(prefix_hits.size(), 1U);
  EXPECT_EQ(prefix_hits[0], Sha256Prefix(kEvil1Url2));

  urls[0] = GURL(std::string("ftp://") + kEvil1Url2);
  EXPECT_TRUE(database_->ContainsDownloadUrl(urls, &prefix_hits));
  ASSERT_EQ(prefix_hits.size(), 1U);
  EXPECT_EQ(prefix_hits[0], Sha256Prefix(kEvil1Url2));

  urls[0] = GURL("http://www.randomevil.com");
  EXPECT_FALSE(database_->ContainsDownloadUrl(urls, &prefix_hits));

  // Should match with query args stripped.
  urls[0] = GURL(std::string("http://") + kEvil1Url2 + "?blah");
  EXPECT_TRUE(database_->ContainsDownloadUrl(urls, &prefix_hits));
  ASSERT_EQ(prefix_hits.size(), 1U);
  EXPECT_EQ(prefix_hits[0], Sha256Prefix(kEvil1Url2));

  // Should match with extra path stuff and query args stripped.
  urls[0] = GURL(std::string("http://") + kEvil1Url1 + "foo/bar?blah");
  EXPECT_TRUE(database_->ContainsDownloadUrl(urls, &prefix_hits));
  ASSERT_EQ(prefix_hits.size(), 1U);
  EXPECT_EQ(prefix_hits[0], Sha256Prefix(kEvil1Url1));

  // First hit in redirect chain is malware.
  urls.clear();
  urls.push_back(GURL(std::string("http://") + kEvil1Url1));
  urls.push_back(GURL("http://www.randomevil.com"));
  EXPECT_TRUE(database_->ContainsDownloadUrl(urls, &prefix_hits));
  ASSERT_EQ(prefix_hits.size(), 1U);
  EXPECT_EQ(prefix_hits[0], Sha256Prefix(kEvil1Url1));

  // Middle hit in redirect chain is malware.
  urls.clear();
  urls.push_back(GURL("http://www.randomevil.com"));
  urls.push_back(GURL(std::string("http://") + kEvil1Url1));
  urls.push_back(GURL("http://www.randomevil2.com"));
  EXPECT_TRUE(database_->ContainsDownloadUrl(urls, &prefix_hits));
  ASSERT_EQ(prefix_hits.size(), 1U);
  EXPECT_EQ(prefix_hits[0], Sha256Prefix(kEvil1Url1));

  // Final hit in redirect chain is malware.
  urls.clear();
  urls.push_back(GURL("http://www.randomevil.com"));
  urls.push_back(GURL(std::string("http://") + kEvil1Url1));
  EXPECT_TRUE(database_->ContainsDownloadUrl(urls, &prefix_hits));
  ASSERT_EQ(prefix_hits.size(), 1U);
  EXPECT_EQ(prefix_hits[0], Sha256Prefix(kEvil1Url1));

  // Multiple hits in redirect chain are in malware list.
  urls.clear();
  urls.push_back(GURL(std::string("http://") + kEvil1Url1));
  urls.push_back(GURL(std::string("https://") + kEvil1Url2));
  EXPECT_TRUE(database_->ContainsDownloadUrl(urls, &prefix_hits));
  ASSERT_EQ(prefix_hits.size(), 2U);
  EXPECT_EQ(prefix_hits[0], Sha256Prefix(kEvil1Url1));
  EXPECT_EQ(prefix_hits[1], Sha256Prefix(kEvil1Url2));
  database_.reset();
}

// Checks that the whitelists are handled properly.
TEST_F(SafeBrowsingDatabaseTest, Whitelists) {
  database_.reset();
  // We expect all calls to ContainsCsdWhitelistedUrl in particular to be made
  // from the IO thread.  In general the whitelist lookups are thread-safe.
  content::TestBrowserThreadBundle thread_bundle_;

  // If the whitelist is disabled everything should match the whitelist.
  database_.reset(new SafeBrowsingDatabaseNew(new SafeBrowsingStoreFile(),
                                              NULL, NULL, NULL, NULL, NULL));
  database_->Init(database_filename_);
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://www.phishing.com/"))));
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://www.phishing.com/"))));
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedString("asdf"));

  SafeBrowsingStoreFile* browse_store = new SafeBrowsingStoreFile();
  SafeBrowsingStoreFile* csd_whitelist_store = new SafeBrowsingStoreFile();
  SafeBrowsingStoreFile* download_whitelist_store = new SafeBrowsingStoreFile();
  SafeBrowsingStoreFile* extension_blacklist_store =
      new SafeBrowsingStoreFile();
  database_.reset(new SafeBrowsingDatabaseNew(browse_store, NULL,
                                              csd_whitelist_store,
                                              download_whitelist_store,
                                              extension_blacklist_store,
                                              NULL));
  database_->Init(database_filename_);

  const char kGood1Host[] = "www.good1.com/";
  const char kGood1Url1[] = "www.good1.com/a/b.html";
  const char kGood1Url2[] = "www.good1.com/b/";

  const char kGood2Host[] = "www.good2.com/";
  const char kGood2Url1[] = "www.good2.com/c";  // Should match '/c/bla'.

  // good3.com/a/b/c/d/e/f/g/ should match because it's a whitelist.
  const char kGood3Host[] = "good3.com/";
  const char kGood3Url1[] = "good3.com/";

  const char kGoodString[] = "good_string";

  SBChunkList download_chunks, csd_chunks;
  SBChunk chunk;
  // Add two simple chunks to the csd whitelist.
  InsertAddChunkHost2FullHashes(&chunk, 1, kGood1Host,
                                kGood1Url1, kGood1Url2);
  csd_chunks.push_back(chunk);

  chunk.hosts.clear();
  InsertAddChunkHostFullHashes(&chunk, 2, kGood2Host, kGood2Url1);
  csd_chunks.push_back(chunk);

  chunk.hosts.clear();
  InsertAddChunkHostFullHashes(&chunk, 2, kGood2Host, kGood2Url1);
  download_chunks.push_back(chunk);

  chunk.hosts.clear();
  InsertAddChunkHostFullHashes(&chunk, 3, kGoodString, kGoodString);
  download_chunks.push_back(chunk);

  chunk.hosts.clear();
  InsertAddChunkHostFullHashes(&chunk, 4, kGood3Host, kGood3Url1);
  download_chunks.push_back(chunk);

  std::vector<SBListChunkRanges> lists;
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kCsdWhiteList,
                          csd_chunks);
  database_->InsertChunks(safe_browsing_util::kDownloadWhiteList,
                          download_chunks);
  database_->UpdateFinished(true);

  EXPECT_FALSE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://") + kGood1Host)));

  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://") + kGood1Url1)));
  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://") + kGood1Url1 + "?a=b")));

  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://") + kGood1Url2)));
  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://") + kGood1Url2 + "/c.html")));

  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("https://") + kGood1Url2 + "/c.html")));

  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://") + kGood2Url1 + "/c")));
  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://") + kGood2Url1 + "/c?bla")));
  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://") + kGood2Url1 + "/c/bla")));

  EXPECT_FALSE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://www.google.com/"))));

  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://") + kGood2Url1 + "/c")));
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://") + kGood2Url1 + "/c?bla")));
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://") + kGood2Url1 + "/c/bla")));

  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://good3.com/a/b/c/d/e/f/g/"))));
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://a.b.good3.com/"))));

  EXPECT_FALSE(database_->ContainsDownloadWhitelistedString("asdf"));
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedString(kGoodString));

  EXPECT_FALSE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://www.google.com/"))));

  // Test only add the malware IP killswitch
  csd_chunks.clear();
  chunk.hosts.clear();
  InsertAddChunkHostFullHashes(
      &chunk, 15, "sb-ssl.google.com/",
      "sb-ssl.google.com/safebrowsing/csd/killswitch_malware");
  csd_chunks.push_back(chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kCsdWhiteList, csd_chunks);
  database_->UpdateFinished(true);

  EXPECT_TRUE(database_->IsMalwareIPMatchKillSwitchOn());

  // Test that the kill-switch works as intended.
  csd_chunks.clear();
  download_chunks.clear();
  lists.clear();
  chunk.hosts.clear();
  InsertAddChunkHostFullHashes(&chunk, 5, "sb-ssl.google.com/",
                               "sb-ssl.google.com/safebrowsing/csd/killswitch");
  csd_chunks.push_back(chunk);
  chunk.hosts.clear();
  InsertAddChunkHostFullHashes(&chunk, 5, "sb-ssl.google.com/",
                               "sb-ssl.google.com/safebrowsing/csd/killswitch");
  download_chunks.push_back(chunk);

  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kCsdWhiteList, csd_chunks);
  database_->InsertChunks(safe_browsing_util::kDownloadWhiteList,
                          download_chunks);
  database_->UpdateFinished(true);

  EXPECT_TRUE(database_->IsMalwareIPMatchKillSwitchOn());
  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("https://") + kGood1Url2 + "/c.html")));
  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://www.google.com/"))));
  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://www.phishing_url.com/"))));

  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("https://") + kGood1Url2 + "/c.html")));
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://www.google.com/"))));
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://www.phishing_url.com/"))));

  EXPECT_TRUE(database_->ContainsDownloadWhitelistedString("asdf"));
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedString(kGoodString));

  // Remove the kill-switch and verify that we can recover.
  csd_chunks.clear();
  download_chunks.clear();
  lists.clear();
  SBChunk sub_chunk;
  InsertSubChunkHostFullHash(&sub_chunk, 1, 5,
                             "sb-ssl.google.com/",
                             "sb-ssl.google.com/safebrowsing/csd/killswitch");
  csd_chunks.push_back(sub_chunk);

  sub_chunk.hosts.clear();
  InsertSubChunkHostFullHash(
      &sub_chunk, 10, 15, "sb-ssl.google.com/",
      "sb-ssl.google.com/safebrowsing/csd/killswitch_malware");
  csd_chunks.push_back(sub_chunk);

  sub_chunk.hosts.clear();
  InsertSubChunkHostFullHash(&sub_chunk, 1, 5,
                             "sb-ssl.google.com/",
                             "sb-ssl.google.com/safebrowsing/csd/killswitch");
  download_chunks.push_back(sub_chunk);

  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kCsdWhiteList, csd_chunks);
  database_->InsertChunks(safe_browsing_util::kDownloadWhiteList,
                          download_chunks);
  database_->UpdateFinished(true);

  EXPECT_FALSE(database_->IsMalwareIPMatchKillSwitchOn());
  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("https://") + kGood1Url2 + "/c.html")));
  EXPECT_TRUE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("https://") + kGood2Url1 + "/c/bla")));
  EXPECT_FALSE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://www.google.com/"))));
  EXPECT_FALSE(database_->ContainsCsdWhitelistedUrl(
      GURL(std::string("http://www.phishing_url.com/"))));

  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("https://") + kGood2Url1 + "/c/bla")));
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("https://good3.com/"))));
  EXPECT_TRUE(database_->ContainsDownloadWhitelistedString(kGoodString));
  EXPECT_FALSE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://www.google.com/"))));
  EXPECT_FALSE(database_->ContainsDownloadWhitelistedUrl(
      GURL(std::string("http://www.phishing_url.com/"))));
  EXPECT_FALSE(database_->ContainsDownloadWhitelistedString("asdf"));

  database_.reset();
}

// Test to make sure we could insert chunk list that
// contains entries for the same host.
TEST_F(SafeBrowsingDatabaseTest, SameHostEntriesOkay) {
  SBChunk chunk;

  // Add a malware add chunk with two entries of the same host.
  InsertAddChunkHostPrefixUrl(&chunk, 1, "www.evil.com/",
                              "www.evil.com/malware1.html");
  InsertAddChunkHostPrefixUrl(&chunk, 1, "www.evil.com/",
                              "www.evil.com/malware2.html");
  SBChunkList chunks;
  chunks.push_back(chunk);

  // Insert the testing chunks into database.
  std::vector<SBListChunkRanges> lists;
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  EXPECT_EQ(std::string(safe_browsing_util::kMalwareList), lists[0].name);
  EXPECT_EQ("1", lists[0].adds);
  EXPECT_TRUE(lists[0].subs.empty());

  // Add a phishing add chunk with two entries of the same host.
  chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&chunk, 47, "www.evil.com/",
                              "www.evil.com/phishing1.html");
  InsertAddChunkHostPrefixUrl(&chunk, 47, "www.evil.com/",
                              "www.evil.com/phishing2.html");
  chunks.clear();
  chunks.push_back(chunk);

  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kPhishingList, chunks);
  database_->UpdateFinished(true);

  GetListsInfo(&lists);
  EXPECT_EQ(std::string(safe_browsing_util::kMalwareList), lists[0].name);
  EXPECT_EQ("1", lists[0].adds);
  EXPECT_EQ(std::string(safe_browsing_util::kPhishingList), lists[1].name);
  EXPECT_EQ("47", lists[1].adds);

  const Time now = Time::Now();
  std::vector<SBPrefix> prefixes;
  std::vector<SBFullHashResult> full_hashes;
  std::vector<SBPrefix> prefix_hits;
  std::string matching_list;
  std::string listname;

  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware1.html"),
      &listname, &prefixes, &full_hashes, now));
  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware2.html"),
      &listname, &prefixes, &full_hashes, now));
  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/phishing1.html"),
      &listname, &prefixes, &full_hashes, now));
  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/phishing2.html"),
      &listname, &prefixes, &full_hashes, now));

  // Test removing a single prefix from the add chunk.
  // Remove the prefix that added first.
  chunk.hosts.clear();
  InsertSubChunkHostPrefixUrl(&chunk, 4, 1, "www.evil.com/",
                              "www.evil.com/malware1.html");
  chunks.clear();
  chunks.push_back(chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  // Remove the prefix that added last.
  chunk.hosts.clear();
  InsertSubChunkHostPrefixUrl(&chunk, 5, 47, "www.evil.com/",
                              "www.evil.com/phishing2.html");
  chunks.clear();
  chunks.push_back(chunk);
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->InsertChunks(safe_browsing_util::kPhishingList, chunks);
  database_->UpdateFinished(true);

  // Verify that the database contains urls expected.
  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware1.html"),
      &listname, &prefixes, &full_hashes, now));
  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware2.html"),
      &listname, &prefixes, &full_hashes, now));
  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/phishing1.html"),
      &listname, &prefixes, &full_hashes, now));
  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/phishing2.html"),
      &listname, &prefixes, &full_hashes, now));
}

// Test that an empty update doesn't actually update the database.
// This isn't a functionality requirement, but it is a useful
// optimization.
TEST_F(SafeBrowsingDatabaseTest, EmptyUpdate) {
  SBChunkList chunks;
  SBChunk chunk;

  base::FilePath filename = database_->BrowseDBFilename(database_filename_);

  // Prime the database.
  std::vector<SBListChunkRanges> lists;
  EXPECT_TRUE(database_->UpdateStarted(&lists));

  InsertAddChunkHostPrefixUrl(&chunk, 1, "www.evil.com/",
                              "www.evil.com/malware.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);

  // Get an older time to reset the lastmod time for detecting whether
  // the file has been updated.
  base::PlatformFileInfo before_info, after_info;
  ASSERT_TRUE(file_util::GetFileInfo(filename, &before_info));
  const base::Time old_last_modified =
      before_info.last_modified - base::TimeDelta::FromSeconds(10);

  // Inserting another chunk updates the database file.  The sleep is
  // needed because otherwise the entire test can finish w/in the
  // resolution of the lastmod time.
  ASSERT_TRUE(file_util::SetLastModifiedTime(filename, old_last_modified));
  ASSERT_TRUE(file_util::GetFileInfo(filename, &before_info));
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  chunk.hosts.clear();
  InsertAddChunkHostPrefixUrl(&chunk, 2, "www.foo.com/",
                              "www.foo.com/malware.html");
  chunks.clear();
  chunks.push_back(chunk);
  database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
  database_->UpdateFinished(true);
  ASSERT_TRUE(file_util::GetFileInfo(filename, &after_info));
  EXPECT_LT(before_info.last_modified, after_info.last_modified);

  // Deleting a chunk updates the database file.
  ASSERT_TRUE(file_util::SetLastModifiedTime(filename, old_last_modified));
  ASSERT_TRUE(file_util::GetFileInfo(filename, &before_info));
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  AddDelChunk(safe_browsing_util::kMalwareList, chunk.chunk_number);
  database_->UpdateFinished(true);
  ASSERT_TRUE(file_util::GetFileInfo(filename, &after_info));
  EXPECT_LT(before_info.last_modified, after_info.last_modified);

  // Simply calling |UpdateStarted()| then |UpdateFinished()| does not
  // update the database file.
  ASSERT_TRUE(file_util::SetLastModifiedTime(filename, old_last_modified));
  ASSERT_TRUE(file_util::GetFileInfo(filename, &before_info));
  EXPECT_TRUE(database_->UpdateStarted(&lists));
  database_->UpdateFinished(true);
  ASSERT_TRUE(file_util::GetFileInfo(filename, &after_info));
  EXPECT_EQ(before_info.last_modified, after_info.last_modified);
}

// Test that a filter file is written out during update and read back
// in during setup.
TEST_F(SafeBrowsingDatabaseTest, FilterFile) {
  // Create a database with trivial example data and write it out.
  {
    SBChunkList chunks;
    SBChunk chunk;

    // Prime the database.
    std::vector<SBListChunkRanges> lists;
    EXPECT_TRUE(database_->UpdateStarted(&lists));

    InsertAddChunkHostPrefixUrl(&chunk, 1, "www.evil.com/",
                                "www.evil.com/malware.html");
    chunks.clear();
    chunks.push_back(chunk);
    database_->InsertChunks(safe_browsing_util::kMalwareList, chunks);
    database_->UpdateFinished(true);
  }

  // Find the malware url in the database, don't find a good url.
  const Time now = Time::Now();
  std::vector<SBFullHashResult> full_hashes;
  std::vector<SBPrefix> prefix_hits;
  std::string matching_list;
  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware.html"),
      &matching_list, &prefix_hits, &full_hashes, now));
  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.good.com/goodware.html"),
      &matching_list, &prefix_hits, &full_hashes, now));

  base::FilePath filter_file = database_->PrefixSetForFilename(
      database_->BrowseDBFilename(database_filename_));

  // After re-creating the database, it should have a filter read from
  // a file, so it should find the same results.
  ASSERT_TRUE(base::PathExists(filter_file));
  database_.reset(new SafeBrowsingDatabaseNew);
  database_->Init(database_filename_);
  EXPECT_TRUE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware.html"),
      &matching_list, &prefix_hits, &full_hashes, now));
  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.good.com/goodware.html"),
      &matching_list, &prefix_hits, &full_hashes, now));

  // If there is no filter file, the database cannot find malware urls.
  base::DeleteFile(filter_file, false);
  ASSERT_FALSE(base::PathExists(filter_file));
  database_.reset(new SafeBrowsingDatabaseNew);
  database_->Init(database_filename_);
  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.evil.com/malware.html"),
      &matching_list, &prefix_hits, &full_hashes, now));
  EXPECT_FALSE(database_->ContainsBrowseUrl(
      GURL("http://www.good.com/goodware.html"),
      &matching_list, &prefix_hits, &full_hashes, now));
}
