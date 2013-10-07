// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/webdata/keyword_table.h"
#include "components/webdata/common/web_database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Time;
using base::TimeDelta;

class KeywordTableTest : public testing::Test {
 public:
  KeywordTableTest() {}
  virtual ~KeywordTableTest() {}

 protected:
  virtual void SetUp() {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    file_ = temp_dir_.path().AppendASCII("TestWebDatabase");

    table_.reset(new KeywordTable);
    db_.reset(new WebDatabase);
    db_->AddTable(table_.get());
    ASSERT_EQ(sql::INIT_OK, db_->Init(file_));
  }

  base::FilePath file_;
  base::ScopedTempDir temp_dir_;
  scoped_ptr<KeywordTable> table_;
  scoped_ptr<WebDatabase> db_;

 private:
  DISALLOW_COPY_AND_ASSIGN(KeywordTableTest);
};


TEST_F(KeywordTableTest, Keywords) {
  TemplateURLData keyword;
  keyword.short_name = ASCIIToUTF16("short_name");
  keyword.SetKeyword(ASCIIToUTF16("keyword"));
  keyword.SetURL("http://url/");
  keyword.instant_url = "http://instant/";
  keyword.favicon_url = GURL("http://favicon.url/");
  keyword.originating_url = GURL("http://google.com/");
  keyword.show_in_default_list = true;
  keyword.safe_for_autoreplace = true;
  keyword.input_encodings.push_back("UTF-8");
  keyword.input_encodings.push_back("UTF-16");
  keyword.id = 1;
  keyword.date_created = Time::Now();
  keyword.last_modified = keyword.date_created + TimeDelta::FromSeconds(10);
  keyword.created_by_policy = true;
  keyword.usage_count = 32;
  keyword.prepopulate_id = 10;
  EXPECT_TRUE(table_->AddKeyword(keyword));

  KeywordTable::Keywords keywords;
  EXPECT_TRUE(table_->GetKeywords(&keywords));
  EXPECT_EQ(1U, keywords.size());
  const TemplateURLData& restored_keyword = keywords.front();

  EXPECT_EQ(keyword.short_name, restored_keyword.short_name);
  EXPECT_EQ(keyword.keyword(), restored_keyword.keyword());
  EXPECT_EQ(keyword.url(), restored_keyword.url());
  EXPECT_EQ(keyword.suggestions_url, restored_keyword.suggestions_url);
  EXPECT_EQ(keyword.instant_url, restored_keyword.instant_url);
  EXPECT_EQ(keyword.favicon_url, restored_keyword.favicon_url);
  EXPECT_EQ(keyword.originating_url, restored_keyword.originating_url);
  EXPECT_EQ(keyword.show_in_default_list,
            restored_keyword.show_in_default_list);
  EXPECT_EQ(keyword.safe_for_autoreplace,
            restored_keyword.safe_for_autoreplace);
  EXPECT_EQ(keyword.input_encodings, restored_keyword.input_encodings);
  EXPECT_EQ(keyword.id, restored_keyword.id);
  // The database stores time only at the resolution of a second.
  EXPECT_EQ(keyword.date_created.ToTimeT(),
            restored_keyword.date_created.ToTimeT());
  EXPECT_EQ(keyword.last_modified.ToTimeT(),
            restored_keyword.last_modified.ToTimeT());
  EXPECT_EQ(keyword.created_by_policy, restored_keyword.created_by_policy);
  EXPECT_EQ(keyword.usage_count, restored_keyword.usage_count);
  EXPECT_EQ(keyword.prepopulate_id, restored_keyword.prepopulate_id);

  EXPECT_TRUE(table_->RemoveKeyword(restored_keyword.id));

  KeywordTable::Keywords empty_keywords;
  EXPECT_TRUE(table_->GetKeywords(&empty_keywords));
  EXPECT_EQ(0U, empty_keywords.size());
}

TEST_F(KeywordTableTest, KeywordMisc) {
  EXPECT_EQ(kInvalidTemplateURLID, table_->GetDefaultSearchProviderID());
  EXPECT_EQ(0, table_->GetBuiltinKeywordVersion());

  TemplateURLData keyword;
  keyword.short_name = ASCIIToUTF16("short_name");
  keyword.SetKeyword(ASCIIToUTF16("keyword"));
  keyword.SetURL("http://url/");
  keyword.instant_url = "http://instant/";
  keyword.favicon_url = GURL("http://favicon.url/");
  keyword.originating_url = GURL("http://google.com/");
  keyword.show_in_default_list = true;
  keyword.safe_for_autoreplace = true;
  keyword.input_encodings.push_back("UTF-8");
  keyword.input_encodings.push_back("UTF-16");
  keyword.id = 10;
  keyword.date_created = Time::Now();
  keyword.last_modified = keyword.date_created + TimeDelta::FromSeconds(10);
  keyword.created_by_policy = true;
  keyword.usage_count = 32;
  keyword.prepopulate_id = 10;
  EXPECT_TRUE(table_->AddKeyword(keyword));

  EXPECT_TRUE(table_->SetDefaultSearchProviderID(10));
  EXPECT_TRUE(table_->SetBuiltinKeywordVersion(11));

  EXPECT_EQ(10, table_->GetDefaultSearchProviderID());
  EXPECT_EQ(11, table_->GetBuiltinKeywordVersion());
}

TEST_F(KeywordTableTest, GetTableContents) {
  TemplateURLData keyword;
  keyword.short_name = ASCIIToUTF16("short_name");
  keyword.SetKeyword(ASCIIToUTF16("keyword"));
  keyword.SetURL("http://url/");
  keyword.suggestions_url = "url2";
  keyword.image_url = "http://image-search-url/";
  keyword.favicon_url = GURL("http://favicon.url/");
  keyword.show_in_default_list = true;
  keyword.safe_for_autoreplace = true;
  keyword.id = 1;
  keyword.date_created = base::Time::UnixEpoch();
  keyword.last_modified = base::Time::UnixEpoch();
  keyword.sync_guid = "1234-5678-90AB-CDEF";
  keyword.alternate_urls.push_back("a_url1");
  keyword.alternate_urls.push_back("a_url2");
  keyword.search_terms_replacement_key = "espv";
  EXPECT_TRUE(table_->AddKeyword(keyword));

  keyword.SetKeyword(ASCIIToUTF16("url"));
  keyword.instant_url = "http://instant2/";
  keyword.image_url.clear();
  keyword.originating_url = GURL("http://originating.url/");
  keyword.input_encodings.push_back("Shift_JIS");
  keyword.id = 2;
  keyword.prepopulate_id = 5;
  keyword.sync_guid = "FEDC-BA09-8765-4321";
  keyword.alternate_urls.clear();
  keyword.search_terms_replacement_key.clear();
  EXPECT_TRUE(table_->AddKeyword(keyword));

  const char kTestContents[] = "1short_namekeywordhttp://favicon.url/"
      "http://url/1001url20001234-5678-90AB-CDEF[\"a_url1\",\"a_url2\"]espv"
      "http://image-search-url/2short_nameurlhttp://favicon.url/http://url/1"
      "http://originating.url/00Shift_JIS1url250http://instant2/0"
      "FEDC-BA09-8765-4321[]";

  std::string contents;
  EXPECT_TRUE(table_->GetTableContents("keywords",
      WebDatabase::kCurrentVersionNumber, &contents));
  EXPECT_EQ(kTestContents, contents);
}

TEST_F(KeywordTableTest, GetTableContentsOrdering) {
  TemplateURLData keyword;
  keyword.short_name = ASCIIToUTF16("short_name");
  keyword.SetKeyword(ASCIIToUTF16("keyword"));
  keyword.SetURL("http://url/");
  keyword.suggestions_url = "url2";
  keyword.favicon_url = GURL("http://favicon.url/");
  keyword.show_in_default_list = true;
  keyword.safe_for_autoreplace = true;
  keyword.id = 2;
  keyword.date_created = base::Time::UnixEpoch();
  keyword.last_modified = base::Time::UnixEpoch();
  keyword.sync_guid = "1234-5678-90AB-CDEF";
  keyword.alternate_urls.push_back("a_url1");
  keyword.alternate_urls.push_back("a_url2");
  keyword.search_terms_replacement_key = "espv";
  keyword.image_url = "http://image-search-url/";
  keyword.search_url_post_params = "ie=utf-8,oe=utf-8";
  keyword.image_url_post_params = "name=1,value=2";
  EXPECT_TRUE(table_->AddKeyword(keyword));

  keyword.SetKeyword(ASCIIToUTF16("url"));
  keyword.instant_url = "http://instant2/";
  keyword.originating_url = GURL("http://originating.url/");
  keyword.input_encodings.push_back("Shift_JIS");
  keyword.id = 1;
  keyword.prepopulate_id = 5;
  keyword.sync_guid = "FEDC-BA09-8765-4321";
  keyword.alternate_urls.clear();
  keyword.search_terms_replacement_key.clear();
  keyword.image_url.clear();
  keyword.search_url_post_params.clear();
  keyword.image_url_post_params.clear();
  EXPECT_TRUE(table_->AddKeyword(keyword));

  const char kTestContents[] = "1short_nameurlhttp://favicon.url/http://url/1"
      "http://originating.url/00Shift_JIS1url250http://instant2/0"
      "FEDC-BA09-8765-4321[]2short_namekeywordhttp://favicon.url/"
      "http://url/1001url20001234-5678-90AB-CDEF[\"a_url1\",\"a_url2\"]espv"
      "http://image-search-url/ie=utf-8,oe=utf-8name=1,value=2";

  std::string contents;
  EXPECT_TRUE(table_->GetTableContents("keywords",
      WebDatabase::kCurrentVersionNumber, &contents));
  EXPECT_EQ(kTestContents, contents);
}

TEST_F(KeywordTableTest, UpdateKeyword) {
  TemplateURLData keyword;
  keyword.short_name = ASCIIToUTF16("short_name");
  keyword.SetKeyword(ASCIIToUTF16("keyword"));
  keyword.SetURL("http://url/");
  keyword.suggestions_url = "url2";
  keyword.favicon_url = GURL("http://favicon.url/");
  keyword.show_in_default_list = true;
  keyword.safe_for_autoreplace = true;
  keyword.id = 1;
  EXPECT_TRUE(table_->AddKeyword(keyword));

  keyword.SetKeyword(ASCIIToUTF16("url"));
  keyword.instant_url = "http://instant2/";
  keyword.originating_url = GURL("http://originating.url/");
  keyword.input_encodings.push_back("Shift_JIS");
  keyword.prepopulate_id = 5;
  EXPECT_TRUE(table_->UpdateKeyword(keyword));

  KeywordTable::Keywords keywords;
  EXPECT_TRUE(table_->GetKeywords(&keywords));
  EXPECT_EQ(1U, keywords.size());
  const TemplateURLData& restored_keyword = keywords.front();

  EXPECT_EQ(keyword.short_name, restored_keyword.short_name);
  EXPECT_EQ(keyword.keyword(), restored_keyword.keyword());
  EXPECT_EQ(keyword.suggestions_url, restored_keyword.suggestions_url);
  EXPECT_EQ(keyword.instant_url, restored_keyword.instant_url);
  EXPECT_EQ(keyword.favicon_url, restored_keyword.favicon_url);
  EXPECT_EQ(keyword.originating_url, restored_keyword.originating_url);
  EXPECT_EQ(keyword.show_in_default_list,
            restored_keyword.show_in_default_list);
  EXPECT_EQ(keyword.safe_for_autoreplace,
            restored_keyword.safe_for_autoreplace);
  EXPECT_EQ(keyword.input_encodings, restored_keyword.input_encodings);
  EXPECT_EQ(keyword.id, restored_keyword.id);
  EXPECT_EQ(keyword.prepopulate_id, restored_keyword.prepopulate_id);
}

TEST_F(KeywordTableTest, KeywordWithNoFavicon) {
  TemplateURLData keyword;
  keyword.short_name = ASCIIToUTF16("short_name");
  keyword.SetKeyword(ASCIIToUTF16("keyword"));
  keyword.SetURL("http://url/");
  keyword.safe_for_autoreplace = true;
  keyword.id = -100;
  EXPECT_TRUE(table_->AddKeyword(keyword));

  KeywordTable::Keywords keywords;
  EXPECT_TRUE(table_->GetKeywords(&keywords));
  EXPECT_EQ(1U, keywords.size());
  const TemplateURLData& restored_keyword = keywords.front();

  EXPECT_EQ(keyword.short_name, restored_keyword.short_name);
  EXPECT_EQ(keyword.keyword(), restored_keyword.keyword());
  EXPECT_EQ(keyword.favicon_url, restored_keyword.favicon_url);
  EXPECT_EQ(keyword.safe_for_autoreplace,
            restored_keyword.safe_for_autoreplace);
  EXPECT_EQ(keyword.id, restored_keyword.id);
}

TEST_F(KeywordTableTest, SanitizeURLs) {
  TemplateURLData keyword;
  keyword.short_name = ASCIIToUTF16("legit");
  keyword.SetKeyword(ASCIIToUTF16("legit"));
  keyword.SetURL("http://url/");
  keyword.id = 1000;
  EXPECT_TRUE(table_->AddKeyword(keyword));

  keyword.short_name = ASCIIToUTF16("bogus");
  keyword.SetKeyword(ASCIIToUTF16("bogus"));
  keyword.id = 2000;
  EXPECT_TRUE(table_->AddKeyword(keyword));

  KeywordTable::Keywords keywords;
  EXPECT_TRUE(table_->GetKeywords(&keywords));
  EXPECT_EQ(2U, keywords.size());
  keywords.clear();

  // Erase the URL field for the second keyword to simulate having bogus data
  // previously saved into the database.
  sql::Statement s(table_->db_->GetUniqueStatement(
      "UPDATE keywords SET url=? WHERE id=?"));
  s.BindString16(0, string16());
  s.BindInt64(1, 2000);
  EXPECT_TRUE(s.Run());

  // GetKeywords() should erase the entry with the empty URL field.
  EXPECT_TRUE(table_->GetKeywords(&keywords));
  EXPECT_EQ(1U, keywords.size());
}
