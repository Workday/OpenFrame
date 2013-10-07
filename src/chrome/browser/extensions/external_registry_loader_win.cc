// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/external_registry_loader_win.h"

#include "base/bind.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_handle.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "base/version.h"
#include "base/win/registry.h"
#include "chrome/browser/extensions/external_provider_impl.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace {

// The Registry subkey that contains information about external extensions.
const char kRegistryExtensions[] = "Software\\Google\\Chrome\\Extensions";

// Registry value of of that key that defines the path to the .crx file.
const wchar_t kRegistryExtensionPath[] = L"path";

// Registry value of that key that defines the current version of the .crx file.
const wchar_t kRegistryExtensionVersion[] = L"version";

bool CanOpenFileForReading(const base::FilePath& path) {
  ScopedStdioHandle file_handle(file_util::OpenFile(path, "rb"));
  return file_handle.get() != NULL;
}

}  // namespace

namespace extensions {

void ExternalRegistryLoader::StartLoading() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&ExternalRegistryLoader::LoadOnFileThread, this));
}

void ExternalRegistryLoader::LoadOnFileThread() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  base::TimeTicks start_time = base::TimeTicks::Now();
  scoped_ptr<DictionaryValue> prefs(new DictionaryValue);

  // A map of IDs, to weed out duplicates between HKCU and HKLM.
  std::set<string16> keys;
  base::win::RegistryKeyIterator iterator_machine_key(
      HKEY_LOCAL_MACHINE, ASCIIToWide(kRegistryExtensions).c_str());
  for (; iterator_machine_key.Valid(); ++iterator_machine_key)
    keys.insert(iterator_machine_key.Name());
  base::win::RegistryKeyIterator iterator_user_key(
      HKEY_CURRENT_USER, ASCIIToWide(kRegistryExtensions).c_str());
  for (; iterator_user_key.Valid(); ++iterator_user_key)
    keys.insert(iterator_user_key.Name());

  // Iterate over the keys found, first trying HKLM, then HKCU, as per Windows
  // policy conventions. We only fall back to HKCU if the HKLM key cannot be
  // opened, not if the data within the key is invalid, for example.
  for (std::set<string16>::const_iterator it = keys.begin();
       it != keys.end(); ++it) {
    base::win::RegKey key;
    string16 key_path = ASCIIToWide(kRegistryExtensions);
    key_path.append(L"\\");
    key_path.append(*it);
    if (key.Open(HKEY_LOCAL_MACHINE,
                 key_path.c_str(), KEY_READ) != ERROR_SUCCESS) {
      if (key.Open(HKEY_CURRENT_USER,
                   key_path.c_str(), KEY_READ) != ERROR_SUCCESS) {
        LOG(ERROR) << "Unable to read registry key at path (HKLM & HKCU): "
                   << key_path << ".";
        continue;
      }
    }

    string16 extension_path_str;
    if (key.ReadValue(kRegistryExtensionPath, &extension_path_str)
        != ERROR_SUCCESS) {
      // TODO(erikkay): find a way to get this into about:extensions
      LOG(ERROR) << "Missing value " << kRegistryExtensionPath
                 << " for key " << key_path << ".";
      continue;
    }

    base::FilePath extension_path(extension_path_str);
    if (!extension_path.IsAbsolute()) {
      LOG(ERROR) << "File path " << extension_path_str
                 << " needs to be absolute in key "
                 << key_path;
      continue;
    }

    if (!base::PathExists(extension_path)) {
      LOG(ERROR) << "File " << extension_path_str
                 << " for key " << key_path
                 << " does not exist or is not readable.";
      continue;
    }

    if (!CanOpenFileForReading(extension_path)) {
      LOG(ERROR) << "File " << extension_path_str
                 << " for key " << key_path << " can not be read. "
                 << "Check that users who should have the extension "
                 << "installed have permission to read it.";
      continue;
    }

    string16 extension_version;
    if (key.ReadValue(kRegistryExtensionVersion, &extension_version)
        != ERROR_SUCCESS) {
      // TODO(erikkay): find a way to get this into about:extensions
      LOG(ERROR) << "Missing value " << kRegistryExtensionVersion
                 << " for key " << key_path << ".";
      continue;
    }

    std::string id = WideToASCII(*it);
    StringToLowerASCII(&id);
    if (!Extension::IdIsValid(id)) {
      LOG(ERROR) << "Invalid id value " << id
                 << " for key " << key_path << ".";
      continue;
    }

    Version version(WideToASCII(extension_version));
    if (!version.IsValid()) {
      LOG(ERROR) << "Invalid version value " << extension_version
                 << " for key " << key_path << ".";
      continue;
    }

    prefs->SetString(
        id + "." + ExternalProviderImpl::kExternalVersion,
        WideToASCII(extension_version));
    prefs->SetString(
        id + "." + ExternalProviderImpl::kExternalCrx,
        extension_path_str);
  }

  prefs_.reset(prefs.release());
  HISTOGRAM_TIMES("Extensions.ExternalRegistryLoaderWin",
                  base::TimeTicks::Now() - start_time);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&ExternalRegistryLoader::LoadFinished, this));
}

}  // namespace extensions
