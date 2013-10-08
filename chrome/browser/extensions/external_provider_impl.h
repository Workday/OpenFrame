// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTERNAL_PROVIDER_IMPL_H_
#define CHROME_BROWSER_EXTENSIONS_EXTERNAL_PROVIDER_IMPL_H_

#include <string>

#include "chrome/browser/extensions/external_provider_interface.h"

#include "base/memory/ref_counted.h"
#include "chrome/browser/extensions/external_loader.h"
#include "chrome/common/extensions/manifest.h"

class Profile;

namespace base {
class DictionaryValue;
class Version;
}

namespace extensions {
class ExternalLoader;

// A specialization of the ExternalProvider that uses an instance of
// ExternalLoader to provide external extensions. This class can be seen as a
// bridge between the extension system and an ExternalLoader. Instances live
// their entire life on the UI thread.
class ExternalProviderImpl : public ExternalProviderInterface {
 public:
  // The constructed provider will provide the extensions loaded from |loader|
  // to |service|, that will deal with the installation. The location
  // attributes of the provided extensions are also specified here:
  // |crx_location|: extensions originating from crx files
  // |download_location|: extensions originating from update URLs
  // If either of the origins is not supported by this provider, then it should
  // be initialized as Manifest::INVALID_LOCATION.
  ExternalProviderImpl(VisitorInterface* service,
                       ExternalLoader* loader,
                       Profile* profile,
                       Manifest::Location crx_location,
                       Manifest::Location download_location,
                       int creation_flags);

  virtual ~ExternalProviderImpl();

  // Populates a list with providers for all known sources.
  static void CreateExternalProviders(
      VisitorInterface* service,
      Profile* profile,
      ProviderCollection* provider_list);

  // Sets underlying prefs and notifies provider. Only to be called by the
  // owned ExternalLoader instance.
  virtual void SetPrefs(base::DictionaryValue* prefs);

  // ExternalProvider implementation:
  virtual void ServiceShutdown() OVERRIDE;
  virtual void VisitRegisteredExtension() OVERRIDE;
  virtual bool HasExtension(const std::string& id) const OVERRIDE;
  virtual bool GetExtensionDetails(
      const std::string& id,
      Manifest::Location* location,
      scoped_ptr<base::Version>* version) const OVERRIDE;

  virtual bool IsReady() const OVERRIDE;

  static const char kExternalCrx[];
  static const char kExternalVersion[];
  static const char kExternalUpdateUrl[];
  static const char kSupportedLocales[];
  static const char kIsBookmarkApp[];
  static const char kIsFromWebstore[];
  static const char kKeepIfPresent[];

  void set_auto_acknowledge(bool auto_acknowledge) {
    auto_acknowledge_ = auto_acknowledge;
  }

 private:
  // Location for external extensions that are provided by this provider from
  // local crx files.
  const Manifest::Location crx_location_;

  // Location for external extensions that are provided by this provider from
  // update URLs.
  const Manifest::Location download_location_;

  // Weak pointer to the object that consumes the external extensions.
  // This is zeroed out by: ServiceShutdown()
  VisitorInterface* service_;  // weak

  // Dictionary of the external extensions that are provided by this provider.
  scoped_ptr<base::DictionaryValue> prefs_;

  // Indicates that the extensions provided by this provider are loaded
  // entirely.
  bool ready_;

  // The loader that loads the list of external extensions and reports them
  // via |SetPrefs|.
  scoped_refptr<ExternalLoader> loader_;

  // The profile that will be used to install external extensions.
  Profile* profile_;

  // Creation flags to use for the extension.  These flags will be used
  // when calling Extension::Create() by the crx installer.
  int creation_flags_;

  // Whether loaded extensions should be automatically acknowledged, so that
  // the user doesn't see an alert about them.
  bool auto_acknowledge_;

  DISALLOW_COPY_AND_ASSIGN(ExternalProviderImpl);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTERNAL_PROVIDER_IMPL_H_
