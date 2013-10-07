// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/manifest_handlers/shared_module_info.h"

#include "base/lazy_instance.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/version.h"
#include "chrome/common/extensions/extension_manifest_constants.h"
#include "chrome/common/extensions/permissions/permission_set.h"
#include "extensions/common/constants.h"
#include "extensions/common/error_utils.h"

namespace keys = extension_manifest_keys;
namespace values = extension_manifest_values;
namespace errors = extension_manifest_errors;

namespace extensions {

namespace {

const char kSharedModule[] = "shared_module";

static base::LazyInstance<SharedModuleInfo> g_empty_shared_module_info =
    LAZY_INSTANCE_INITIALIZER;

const SharedModuleInfo& GetSharedModuleInfo(const Extension* extension) {
  SharedModuleInfo* info = static_cast<SharedModuleInfo*>(
      extension->GetManifestData(kSharedModule));
  if (!info)
    return g_empty_shared_module_info.Get();
  return *info;
}

}  // namespace

SharedModuleInfo::SharedModuleInfo() {
}

SharedModuleInfo::~SharedModuleInfo() {
}

// static
void SharedModuleInfo::ParseImportedPath(const std::string& path,
                                         std::string* import_id,
                                         std::string* import_relative_path) {
  std::vector<std::string> tokens;
  Tokenize(path, std::string("/"), &tokens);
  if (tokens.size() > 2 && tokens[0] == kModulesDir &&
      Extension::IdIsValid(tokens[1])) {
    *import_id = tokens[1];
    *import_relative_path = tokens[2];
    for (size_t i = 3; i < tokens.size(); ++i)
      *import_relative_path += "/" + tokens[i];
  }
}

// static
bool SharedModuleInfo::IsImportedPath(const std::string& path) {
  std::vector<std::string> tokens;
  Tokenize(path, std::string("/"), &tokens);
  if (tokens.size() > 2 && tokens[0] == kModulesDir &&
      Extension::IdIsValid(tokens[1])) {
    return true;
  }
  return false;
}

// static
bool SharedModuleInfo::IsSharedModule(const Extension* extension) {
  CHECK(extension);
  return extension->manifest()->is_shared_module();
}

// static
bool SharedModuleInfo::IsExportAllowed(const Extension* extension,
                                       const std::string& relative_path) {
  return GetSharedModuleInfo(extension).
      exported_set_.MatchesURL(extension->url().Resolve(relative_path));
}

// static
bool SharedModuleInfo::ImportsExtensionById(const Extension* extension,
                                            const std::string& other_id) {
  const SharedModuleInfo& info = GetSharedModuleInfo(extension);
  for (size_t i = 0; i < info.imports_.size(); i++) {
    if (info.imports_[i].extension_id == other_id)
      return true;
  }
  return false;
}

// static
bool SharedModuleInfo::ImportsModules(const Extension* extension) {
  return GetSharedModuleInfo(extension).imports_.size() > 0;
}

// static
const std::vector<SharedModuleInfo::ImportInfo>& SharedModuleInfo::GetImports(
    const Extension* extension) {
  return GetSharedModuleInfo(extension).imports_;
}

bool SharedModuleInfo::Parse(const Extension* extension, string16* error) {
  bool has_import = extension->manifest()->HasKey(keys::kImport);
  bool has_export = extension->manifest()->HasKey(keys::kExport);
  if (!has_import && !has_export)
    return true;

  if (has_import && has_export) {
    *error = ASCIIToUTF16(errors::kInvalidImportAndExport);
    return false;
  }

  if (has_export) {
    const base::DictionaryValue* export_value = NULL;
    if (!extension->manifest()->GetDictionary(keys::kExport, &export_value)) {
      *error = ASCIIToUTF16(errors::kInvalidExport);
      return false;
    }
    const base::ListValue* resources_list = NULL;
    if (!export_value->GetList(keys::kResources, &resources_list)) {
      *error = ASCIIToUTF16(errors::kInvalidExportResources);
      return false;
    }
    for (size_t i = 0; i < resources_list->GetSize(); ++i) {
      std::string resource_path;
      if (!resources_list->GetString(i, &resource_path)) {
        *error = ErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidExportResourcesString, base::IntToString(i));
        return false;
      }
      const GURL& resolved_path = extension->url().Resolve(resource_path);
      if (!resolved_path.is_valid()) {
        *error = ErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidExportResourcesString, base::IntToString(i));
        return false;
      }
      exported_set_.AddPattern(
          URLPattern(URLPattern::SCHEME_EXTENSION, resolved_path.spec()));
    }
  }

  if (has_import) {
    const base::ListValue* import_list = NULL;
    if (!extension->manifest()->GetList(keys::kImport, &import_list)) {
      *error = ASCIIToUTF16(errors::kInvalidImport);
      return false;
    }
    for (size_t i = 0; i < import_list->GetSize(); ++i) {
      const base::DictionaryValue* import_entry = NULL;
      if (!import_list->GetDictionary(i, &import_entry)) {
        *error = ASCIIToUTF16(errors::kInvalidImport);
        return false;
      }
      std::string extension_id;
      imports_.push_back(ImportInfo());
      if (!import_entry->GetString(keys::kId, &extension_id) ||
          !Extension::IdIsValid(extension_id)) {
        *error = ErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidImportId, base::IntToString(i));
        return false;
      }
      imports_.back().extension_id = extension_id;
      if (import_entry->HasKey(keys::kMinimumVersion)) {
        std::string min_version;
        if (!import_entry->GetString(keys::kMinimumVersion, &min_version)) {
          *error = ErrorUtils::FormatErrorMessageUTF16(
              errors::kInvalidImportVersion, base::IntToString(i));
          return false;
        }
        imports_.back().minimum_version = min_version;
        Version v(min_version);
        if (!v.IsValid()) {
          *error = ErrorUtils::FormatErrorMessageUTF16(
              errors::kInvalidImportVersion, base::IntToString(i));
          return false;
        }
      }
    }
  }
  return true;
}


SharedModuleHandler::SharedModuleHandler() {
}

SharedModuleHandler::~SharedModuleHandler() {
}

bool SharedModuleHandler::Parse(Extension* extension, string16* error) {
  scoped_ptr<SharedModuleInfo> info(new SharedModuleInfo);
  if (!info->Parse(extension, error))
    return false;
  extension->SetManifestData(kSharedModule, info.release());
  return true;
}

bool SharedModuleHandler::Validate(
    const Extension* extension,
    std::string* error,
    std::vector<InstallWarning>* warnings) const {
  // Extensions that export resources should not have any permissions of their
  // own, instead they rely on the permissions of the extensions which import
  // them.
  if (SharedModuleInfo::IsSharedModule(extension) &&
      !extension->GetActivePermissions()->IsEmpty()) {
    *error = errors::kInvalidExportPermissions;
    return false;
  }
  return true;
}

const std::vector<std::string> SharedModuleHandler::Keys() const {
  static const char* keys[] = {
    keys::kExport,
    keys::kImport
  };
  return std::vector<std::string>(keys, keys + arraysize(keys));
}

}  // namespace extensions
