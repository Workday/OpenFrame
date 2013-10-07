// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Chromium settings and storage represent user-selected preferences and
// information and MUST not be extracted, overwritten or modified except
// through Chromium defined APIs.

#ifndef CHROME_BROWSER_WEBDATA_WEB_DATA_SERVICE_H__
#define CHROME_BROWSER_WEBDATA_WEB_DATA_SERVICE_H__

#include <map>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner_helpers.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_id.h"
#include "chrome/browser/webdata/keyword_table.h"
#include "components/webdata/common/web_data_results.h"
#include "components/webdata/common/web_data_service_base.h"
#include "components/webdata/common/web_data_service_consumer.h"
#include "components/webdata/common/web_database.h"

struct DefaultWebIntentService;
class GURL;
#if defined(OS_WIN)
struct IE7PasswordInfo;
#endif
class Profile;
class SkBitmap;
class WebDatabaseService;

namespace base {
class Thread;
}

namespace content {
class BrowserContext;
}

namespace webkit_glue {
struct WebIntentServiceData;
}

////////////////////////////////////////////////////////////////////////////////
//
// WebDataService is a generic data repository for meta data associated with
// web pages. All data is retrieved and archived in an asynchronous way.
//
// All requests return a handle. The handle can be used to cancel the request.
//
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
//
// WebDataService results
//
////////////////////////////////////////////////////////////////////////////////

typedef base::Callback<scoped_ptr<WDTypedResult>(void)> ResultTask;

// Result from GetWebAppImages.
struct WDAppImagesResult {
  WDAppImagesResult();
  ~WDAppImagesResult();

  // True if SetWebAppHasAllImages(true) was invoked.
  bool has_all_images;

  // The images, may be empty.
  std::vector<SkBitmap> images;
};

struct WDKeywordsResult {
  WDKeywordsResult();
  ~WDKeywordsResult();

  KeywordTable::Keywords keywords;
  // Identifies the ID of the TemplateURL that is the default search. A value of
  // 0 indicates there is no default search provider.
  int64 default_search_provider_id;
  // Version of the built-in keywords. A value of 0 indicates a first run.
  int builtin_keyword_version;
};

class WebDataServiceConsumer;

class WebDataService : public WebDataServiceBase {
 public:
  // Retrieve a WebDataService for the given context.
  static scoped_refptr<WebDataService> FromBrowserContext(
      content::BrowserContext* context);

  WebDataService(scoped_refptr<WebDatabaseService> wdbs,
                 const ProfileErrorCallback& callback);

  //////////////////////////////////////////////////////////////////////////////
  //
  // Keywords
  //
  //////////////////////////////////////////////////////////////////////////////

  // As the database processes requests at a later date, all deletion is
  // done on the background thread.
  //
  // Many of the keyword related methods do not return a handle. This is because
  // the caller (TemplateURLService) does not need to know when the request is
  // done.

  void AddKeyword(const TemplateURLData& data);
  void RemoveKeyword(TemplateURLID id);
  void UpdateKeyword(const TemplateURLData& data);

  // Fetches the keywords.
  // On success, consumer is notified with WDResult<KeywordTable::Keywords>.
  Handle GetKeywords(WebDataServiceConsumer* consumer);

  // Sets the keywords used for the default search provider.
  void SetDefaultSearchProvider(const TemplateURL* url);

  // Sets the version of the builtin keywords.
  void SetBuiltinKeywordVersion(int version);

  //////////////////////////////////////////////////////////////////////////////
  //
  // Web Apps
  //
  //////////////////////////////////////////////////////////////////////////////

  // Sets the image for the specified web app. A web app can have any number of
  // images, but only one at a particular size. If there was an image for the
  // web app at the size of the given image it is replaced.
  void SetWebAppImage(const GURL& app_url, const SkBitmap& image);

  // Sets whether all the images have been downloaded for the specified web app.
  void SetWebAppHasAllImages(const GURL& app_url, bool has_all_images);

  // Removes all images for the specified web app.
  void RemoveWebApp(const GURL& app_url);

  // Fetches the images and whether all images have been downloaded for the
  // specified web app.
  Handle GetWebAppImages(const GURL& app_url, WebDataServiceConsumer* consumer);

#if defined(ENABLE_WEB_INTENTS)
  //////////////////////////////////////////////////////////////////////////////
  //
  // Web Intents
  //
  //////////////////////////////////////////////////////////////////////////////

  // Adds a web intent service registration.
  void AddWebIntentService(const webkit_glue::WebIntentServiceData& service);

  // Removes a web intent service registration.
  void RemoveWebIntentService(const webkit_glue::WebIntentServiceData& service);

  // Get all web intent services registered for the specified |action|.
  // |consumer| must not be NULL.
  Handle GetWebIntentServicesForAction(const string16& action,
                                       WebDataServiceConsumer* consumer);

  // Get all web intent services registered using the specified |service_url|.
  // |consumer| must not be NULL.
  Handle GetWebIntentServicesForURL(const string16& service_url,
                                    WebDataServiceConsumer* consumer);

  // Get all web intent services registered. |consumer| must not be NULL.
  Handle GetAllWebIntentServices(WebDataServiceConsumer* consumer);

  // Adds a default web intent service entry.
  void AddDefaultWebIntentService(const DefaultWebIntentService& service);

  // Removes a default web intent service entry. Removes entries matching
  // the |action|, |type|, and |url_pattern| of |service|.
  void RemoveDefaultWebIntentService(const DefaultWebIntentService& service);

  // Removes all default web intent service entries associated with
  // |service_url|
  void RemoveWebIntentServiceDefaults(const GURL& service_url);

    // Get a list of all web intent service defaults for the given |action|.
  // |consumer| must not be null.
  Handle GetDefaultWebIntentServicesForAction(const string16& action,
                                              WebDataServiceConsumer* consumer);

  // Get a list of all registered web intent service defaults.
  // |consumer| must not be null.
  Handle GetAllDefaultWebIntentServices(WebDataServiceConsumer* consumer);
#endif

#if defined(OS_WIN)
  //////////////////////////////////////////////////////////////////////////////
  //
  // IE7/8 Password Access (used by PasswordStoreWin - do not use elsewhere)
  //
  //////////////////////////////////////////////////////////////////////////////

  // Adds |info| to the list of imported passwords from ie7/ie8.
  void AddIE7Login(const IE7PasswordInfo& info);

  // Removes |info| from the list of imported passwords from ie7/ie8.
  void RemoveIE7Login(const IE7PasswordInfo& info);

  // Get the login matching the information in |info|. |consumer| will be
  // notified when the request is done. The result is of type
  // WDResult<IE7PasswordInfo>.
  // If there is no match, the fields of the IE7PasswordInfo will be empty.
  Handle GetIE7Login(const IE7PasswordInfo& info,
                     WebDataServiceConsumer* consumer);
#endif  // defined(OS_WIN)

 protected:
  // For unit tests, passes a null callback.
  WebDataService();

  virtual ~WebDataService();

 private:
  //////////////////////////////////////////////////////////////////////////////
  //
  // The following methods are only invoked on the DB thread.
  //
  //////////////////////////////////////////////////////////////////////////////

  //////////////////////////////////////////////////////////////////////////////
  //
  // Keywords.
  //
  //////////////////////////////////////////////////////////////////////////////
  WebDatabase::State AddKeywordImpl(
      const TemplateURLData& data, WebDatabase* db);
  WebDatabase::State RemoveKeywordImpl(
      TemplateURLID id, WebDatabase* db);
  WebDatabase::State UpdateKeywordImpl(
      const TemplateURLData& data, WebDatabase* db);
  scoped_ptr<WDTypedResult> GetKeywordsImpl(WebDatabase* db);
  WebDatabase::State SetDefaultSearchProviderImpl(
      TemplateURLID r, WebDatabase* db);
  WebDatabase::State SetBuiltinKeywordVersionImpl(int version, WebDatabase* db);

  //////////////////////////////////////////////////////////////////////////////
  //
  // Web Apps.
  //
  //////////////////////////////////////////////////////////////////////////////

  WebDatabase::State SetWebAppImageImpl(const GURL& app_url,
      const SkBitmap& image, WebDatabase* db);
  WebDatabase::State SetWebAppHasAllImagesImpl(const GURL& app_url,
      bool has_all_images, WebDatabase* db);
  WebDatabase::State RemoveWebAppImpl(const GURL& app_url, WebDatabase* db);
  scoped_ptr<WDTypedResult> GetWebAppImagesImpl(
      const GURL& app_url, WebDatabase* db);

#if defined(ENABLE_WEB_INTENTS)
  //////////////////////////////////////////////////////////////////////////////
  //
  // Web Intents.
  //
  //////////////////////////////////////////////////////////////////////////////
  WebDatabase::State AddWebIntentServiceImpl(
      const webkit_glue::WebIntentServiceData& service);
  WebDatabase::State RemoveWebIntentServiceImpl(
      const webkit_glue::WebIntentServiceData& service);
  scoped_ptr<WDTypedResult> GetWebIntentServicesImpl(const string16& action);
  scoped_ptr<WDTypedResult> GetWebIntentServicesForURLImpl(
      const string16& service_url);
  scoped_ptr<WDTypedResult> GetAllWebIntentServicesImpl();
  WebDatabase::State AddDefaultWebIntentServiceImpl(
      const DefaultWebIntentService& service);
  WebDatabase::State RemoveDefaultWebIntentServiceImpl(
      const DefaultWebIntentService& service);
  WebDatabase::State RemoveWebIntentServiceDefaultsImpl(
      const GURL& service_url);
  scoped_ptr<WDTypedResult> GetDefaultWebIntentServicesForActionImpl(
      const string16& action);
  scoped_ptr<WDTypedResult> GetAllDefaultWebIntentServicesImpl();
#endif

#if defined(OS_WIN)
  //////////////////////////////////////////////////////////////////////////////
  //
  // Password manager.
  //
  //////////////////////////////////////////////////////////////////////////////
  WebDatabase::State AddIE7LoginImpl(
      const IE7PasswordInfo& info, WebDatabase* db);
  WebDatabase::State RemoveIE7LoginImpl(
      const IE7PasswordInfo& info, WebDatabase* db);
  scoped_ptr<WDTypedResult> GetIE7LoginImpl(
      const IE7PasswordInfo& info, WebDatabase* db);
#endif  // defined(OS_WIN)

  DISALLOW_COPY_AND_ASSIGN(WebDataService);
};

#endif  // CHROME_BROWSER_WEBDATA_WEB_DATA_SERVICE_H__
