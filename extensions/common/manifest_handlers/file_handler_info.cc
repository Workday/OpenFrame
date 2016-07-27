// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/manifest_handlers/file_handler_info.h"

#include "base/memory/scoped_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_constants.h"

namespace extensions {

namespace keys = manifest_keys;
namespace errors = manifest_errors;

namespace {
const int kMaxTypeAndExtensionHandlers = 200;
const char kNotRecognized[] = "'%s' is not a recognized file handler property.";
}

FileHandlerInfo::FileHandlerInfo() {}
FileHandlerInfo::~FileHandlerInfo() {}

FileHandlers::FileHandlers() {}
FileHandlers::~FileHandlers() {}

// static
const FileHandlersInfo* FileHandlers::GetFileHandlers(
    const Extension* extension) {
  FileHandlers* info = static_cast<FileHandlers*>(
      extension->GetManifestData(keys::kFileHandlers));
  return info ? &info->file_handlers : NULL;
}

FileHandlersParser::FileHandlersParser() {
}

FileHandlersParser::~FileHandlersParser() {
}

bool LoadFileHandler(const std::string& handler_id,
                     const base::DictionaryValue& handler_info,
                     FileHandlersInfo* file_handlers,
                     base::string16* error,
                     std::vector<InstallWarning>* install_warnings) {
  DCHECK(error);
  FileHandlerInfo handler;

  handler.id = handler_id;

  const base::ListValue* mime_types = NULL;
  if (handler_info.HasKey(keys::kFileHandlerTypes) &&
      !handler_info.GetList(keys::kFileHandlerTypes, &mime_types)) {
    *error = ErrorUtils::FormatErrorMessageUTF16(
        errors::kInvalidFileHandlerType, handler_id);
    return false;
  }

  const base::ListValue* file_extensions = NULL;
  if (handler_info.HasKey(keys::kFileHandlerExtensions) &&
      !handler_info.GetList(keys::kFileHandlerExtensions, &file_extensions)) {
    *error = ErrorUtils::FormatErrorMessageUTF16(
        errors::kInvalidFileHandlerExtension, handler_id);
    return false;
  }

  if ((!mime_types || mime_types->empty()) &&
      (!file_extensions || file_extensions->empty())) {
    *error = ErrorUtils::FormatErrorMessageUTF16(
        errors::kInvalidFileHandlerNoTypeOrExtension,
        handler_id);
    return false;
  }

  if (mime_types) {
    std::string type;
    for (size_t i = 0; i < mime_types->GetSize(); ++i) {
      if (!mime_types->GetString(i, &type)) {
        *error = ErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidFileHandlerTypeElement, handler_id,
            base::SizeTToString(i));
        return false;
      }
      handler.types.insert(type);
    }
  }

  if (file_extensions) {
    std::string file_extension;
    for (size_t i = 0; i < file_extensions->GetSize(); ++i) {
      if (!file_extensions->GetString(i, &file_extension)) {
        *error = ErrorUtils::FormatErrorMessageUTF16(
            errors::kInvalidFileHandlerExtensionElement, handler_id,
            base::SizeTToString(i));
        return false;
      }
      handler.extensions.insert(file_extension);
    }
  }

  file_handlers->push_back(handler);

  // Check for unknown keys.
  for (base::DictionaryValue::Iterator it(handler_info); !it.IsAtEnd();
       it.Advance()) {
    if (it.key() != keys::kFileHandlerExtensions &&
        it.key() != keys::kFileHandlerTypes) {
      install_warnings->push_back(
          InstallWarning(base::StringPrintf(kNotRecognized, it.key().c_str()),
                         keys::kFileHandlers,
                         it.key()));
    }
  }

  return true;
}

bool FileHandlersParser::Parse(Extension* extension, base::string16* error) {
  scoped_ptr<FileHandlers> info(new FileHandlers);
  const base::DictionaryValue* all_handlers = NULL;
  if (!extension->manifest()->GetDictionary(keys::kFileHandlers,
                                            &all_handlers)) {
    *error = base::ASCIIToUTF16(errors::kInvalidFileHandlers);
    return false;
  }

  std::vector<InstallWarning> install_warnings;
  for (base::DictionaryValue::Iterator iter(*all_handlers);
       !iter.IsAtEnd();
       iter.Advance()) {
    const base::DictionaryValue* handler = NULL;
    if (iter.value().GetAsDictionary(&handler)) {
      if (!LoadFileHandler(iter.key(),
                           *handler,
                           &info->file_handlers,
                           error,
                           &install_warnings))
        return false;
    } else {
      *error = base::ASCIIToUTF16(errors::kInvalidFileHandlers);
      return false;
    }
  }

  int filter_count = 0;
  for (FileHandlersInfo::const_iterator iter = info->file_handlers.begin();
       iter != info->file_handlers.end();
       iter++) {
    filter_count += iter->types.size();
    filter_count += iter->extensions.size();
  }

  if (filter_count > kMaxTypeAndExtensionHandlers) {
    *error = base::ASCIIToUTF16(
        errors::kInvalidFileHandlersTooManyTypesAndExtensions);
    return false;
  }

  extension->SetManifestData(keys::kFileHandlers, info.release());
  extension->AddInstallWarnings(install_warnings);
  return true;
}

const std::vector<std::string> FileHandlersParser::Keys() const {
  return SingleKey(keys::kFileHandlers);
}

}  // namespace extensions
