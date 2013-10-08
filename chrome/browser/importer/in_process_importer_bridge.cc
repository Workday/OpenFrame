// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/importer/in_process_importer_bridge.h"

#include "base/bind.h"
#include "base/file_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_parser.h"
#include "chrome/browser/search_engines/template_url_prepopulate_data.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/common/importer/imported_bookmark_entry.h"
#include "chrome/common/importer/imported_favicon_usage.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/password_form.h"
#include "ui/base/l10n/l10n_util.h"

#if defined(OS_WIN)
#include "components/webdata/encryptor/ie7_password.h"
#endif

#include <iterator>

namespace {

history::URLRows ConvertImporterURLRowsToHistoryURLRows(
    const std::vector<ImporterURLRow>& rows) {
  history::URLRows converted;
  converted.reserve(rows.size());
  for (std::vector<ImporterURLRow>::const_iterator it = rows.begin();
       it != rows.end(); ++it) {
    history::URLRow row(it->url);
    row.set_title(it->title);
    row.set_visit_count(it->visit_count);
    row.set_typed_count(it->typed_count);
    row.set_last_visit(it->last_visit);
    row.set_hidden(it->hidden);
    converted.push_back(row);
  }
  return converted;
}

history::VisitSource ConvertImporterVisitSourceToHistoryVisitSource(
    importer::VisitSource visit_source) {
  switch (visit_source) {
    case importer::VISIT_SOURCE_BROWSED:
      return history::SOURCE_BROWSED;
    case importer::VISIT_SOURCE_FIREFOX_IMPORTED:
      return history::SOURCE_FIREFOX_IMPORTED;
    case importer::VISIT_SOURCE_IE_IMPORTED:
      return history::SOURCE_IE_IMPORTED;
    case importer::VISIT_SOURCE_SAFARI_IMPORTED:
      return history::SOURCE_SAFARI_IMPORTED;
  }
  NOTREACHED();
  return history::SOURCE_SYNCED;
}

}  // namespace

using content::BrowserThread;

namespace {

// FirefoxURLParameterFilter is used to remove parameter mentioning Firefox from
// the search URL when importing search engines.
class FirefoxURLParameterFilter : public TemplateURLParser::ParameterFilter {
 public:
  FirefoxURLParameterFilter() {}
  virtual ~FirefoxURLParameterFilter() {}

  // TemplateURLParser::ParameterFilter method.
  virtual bool KeepParameter(const std::string& key,
                             const std::string& value) OVERRIDE {
    std::string low_value = StringToLowerASCII(value);
    if (low_value.find("mozilla") != std::string::npos ||
        low_value.find("firefox") != std::string::npos ||
        low_value.find("moz:") != std::string::npos) {
      return false;
    }
    return true;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FirefoxURLParameterFilter);
};

// Creates a TemplateURL with the |keyword| and |url|. |title| may be empty.
// This function transfers ownership of the created TemplateURL to the caller.
TemplateURL* CreateTemplateURL(const string16& title,
                               const string16& keyword,
                               const GURL& url) {
  // Skip if the url is invalid.
  if (!url.is_valid())
    return NULL;

  TemplateURLData data;
  if (keyword.empty())
    data.SetKeyword(TemplateURLService::GenerateKeyword(url));
  else
    data.SetKeyword(keyword);
  // We set short name by using the title if it exists.
  // Otherwise, we use the shortcut.
  data.short_name = title.empty() ? keyword : title;
  data.SetURL(TemplateURLRef::DisplayURLToURLRef(UTF8ToUTF16(url.spec())));
  return new TemplateURL(NULL, data);
}

// Parses the OpenSearch XML files in |xml_files| and populates |search_engines|
// with the resulting TemplateURLs.
void ParseSearchEnginesFromFirefoxXMLData(
    const std::vector<std::string>& xml_data,
    std::vector<TemplateURL*>* search_engines) {
  DCHECK(search_engines);

  typedef std::map<std::string, TemplateURL*> SearchEnginesMap;
  SearchEnginesMap search_engine_for_url;
  FirefoxURLParameterFilter param_filter;
  // The first XML file represents the default search engine in Firefox 3, so we
  // need to keep it on top of the list.
  SearchEnginesMap::const_iterator default_turl = search_engine_for_url.end();
  for (std::vector<std::string>::const_iterator xml_iter =
           xml_data.begin(); xml_iter != xml_data.end(); ++xml_iter) {
    TemplateURL* template_url = TemplateURLParser::Parse(NULL, true,
        xml_iter->data(), xml_iter->length(), &param_filter);
    if (template_url) {
      SearchEnginesMap::iterator iter =
          search_engine_for_url.find(template_url->url());
      if (iter == search_engine_for_url.end()) {
        iter = search_engine_for_url.insert(
            std::make_pair(template_url->url(), template_url)).first;
      } else {
        // We have already found a search engine with the same URL.  We give
        // priority to the latest one found, as GetSearchEnginesXMLFiles()
        // returns a vector with first Firefox default search engines and then
        // the user's ones.  We want to give priority to the user ones.
        delete iter->second;
        iter->second = template_url;
      }
      if (default_turl == search_engine_for_url.end())
        default_turl = iter;
    }
  }

  // Put the results in the |search_engines| vector.
  for (SearchEnginesMap::iterator t_iter = search_engine_for_url.begin();
       t_iter != search_engine_for_url.end(); ++t_iter) {
    if (t_iter == default_turl)
      search_engines->insert(search_engines->begin(), default_turl->second);
    else
      search_engines->push_back(t_iter->second);
  }
}

}  // namespace

InProcessImporterBridge::InProcessImporterBridge(
    ProfileWriter* writer,
    base::WeakPtr<ExternalProcessImporterHost> host) : writer_(writer),
                                                       host_(host) {
}

void InProcessImporterBridge::AddBookmarks(
    const std::vector<ImportedBookmarkEntry>& bookmarks,
    const string16& first_folder_name) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ProfileWriter::AddBookmarks, writer_, bookmarks,
                 first_folder_name));
}

void InProcessImporterBridge::AddHomePage(const GURL& home_page) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ProfileWriter::AddHomepage, writer_, home_page));
}

#if defined(OS_WIN)
void InProcessImporterBridge::AddIE7PasswordInfo(
    const importer::ImporterIE7PasswordInfo& password_info) {
  IE7PasswordInfo ie7_password_info;
  ie7_password_info.url_hash = password_info.url_hash;
  ie7_password_info.encrypted_data = password_info.encrypted_data;
  ie7_password_info.date_created = password_info.date_created;

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ProfileWriter::AddIE7PasswordInfo, writer_,
                 ie7_password_info));
}
#endif  // OS_WIN

void InProcessImporterBridge::SetFavicons(
    const std::vector<ImportedFaviconUsage>& favicons) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ProfileWriter::AddFavicons, writer_, favicons));
}

void InProcessImporterBridge::SetHistoryItems(
    const std::vector<ImporterURLRow>& rows,
    importer::VisitSource visit_source) {
  history::URLRows converted_rows =
      ConvertImporterURLRowsToHistoryURLRows(rows);
  history::VisitSource converted_visit_source =
      ConvertImporterVisitSourceToHistoryVisitSource(visit_source);
  BrowserThread::PostTask(BrowserThread::UI,
                          FROM_HERE,
                          base::Bind(&ProfileWriter::AddHistoryPage,
                                     writer_,
                                     converted_rows,
                                     converted_visit_source));
}

void InProcessImporterBridge::SetKeywords(
    const std::vector<importer::URLKeywordInfo>& url_keywords,
    bool unique_on_host_and_path) {
  ScopedVector<TemplateURL> owned_template_urls;
  for (size_t i = 0; i < url_keywords.size(); ++i) {
    owned_template_urls.push_back(
        CreateTemplateURL(url_keywords[i].display_name,
                          url_keywords[i].keyword,
                          url_keywords[i].url));
  }
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      base::Bind(&ProfileWriter::AddKeywords, writer_,
                 base::Passed(&owned_template_urls), unique_on_host_and_path));
}

void InProcessImporterBridge::SetFirefoxSearchEnginesXMLData(
    const std::vector<std::string>& search_engine_data) {
  std::vector<TemplateURL*> search_engines;
  ParseSearchEnginesFromFirefoxXMLData(search_engine_data, &search_engines);

  ScopedVector<TemplateURL> owned_template_urls;
  std::copy(search_engines.begin(), search_engines.end(),
            std::back_inserter(owned_template_urls));

  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      base::Bind(&ProfileWriter::AddKeywords, writer_,
                 base::Passed(&owned_template_urls), true));
}

void InProcessImporterBridge::SetPasswordForm(
    const content::PasswordForm& form) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ProfileWriter::AddPasswordForm, writer_, form));
}

void InProcessImporterBridge::NotifyStarted() {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ExternalProcessImporterHost::NotifyImportStarted, host_));
}

void InProcessImporterBridge::NotifyItemStarted(importer::ImportItem item) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ExternalProcessImporterHost::NotifyImportItemStarted,
                 host_, item));
}

void InProcessImporterBridge::NotifyItemEnded(importer::ImportItem item) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ExternalProcessImporterHost::NotifyImportItemEnded,
                 host_, item));
}

void InProcessImporterBridge::NotifyEnded() {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ExternalProcessImporterHost::NotifyImportEnded, host_));
}

string16 InProcessImporterBridge::GetLocalizedString(int message_id) {
  return l10n_util::GetStringUTF16(message_id);
}

InProcessImporterBridge::~InProcessImporterBridge() {}
