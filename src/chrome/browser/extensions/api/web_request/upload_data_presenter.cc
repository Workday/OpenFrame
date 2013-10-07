// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/web_request/upload_data_presenter.h"

#include "base/files/file_path.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/web_request/form_data_parser.h"
#include "chrome/browser/extensions/api/web_request/web_request_api_constants.h"
#include "net/base/upload_bytes_element_reader.h"
#include "net/base/upload_file_element_reader.h"
#include "net/url_request/url_request.h"

using base::BinaryValue;
using base::DictionaryValue;
using base::ListValue;
using base::StringValue;
using base::Value;

namespace keys = extension_web_request_api_constants;

namespace {

// Takes |dictionary| of <string, list of strings> pairs, and gets the list
// for |key|, creating it if necessary.
ListValue* GetOrCreateList(DictionaryValue* dictionary,
                           const std::string& key) {
  ListValue* list = NULL;
  if (!dictionary->GetList(key, &list)) {
    list = new ListValue();
    dictionary->SetWithoutPathExpansion(key, list);
  }
  return list;
}

}  // namespace

namespace extensions {

namespace subtle {

void AppendKeyValuePair(const char* key, Value* value, ListValue* list) {
  DictionaryValue* dictionary = new DictionaryValue;
  dictionary->SetWithoutPathExpansion(key, value);
  list->Append(dictionary);
}

}  // namespace subtle

// Implementation of UploadDataPresenter.

UploadDataPresenter::~UploadDataPresenter() {}

// Implementation of RawDataPresenter.

RawDataPresenter::RawDataPresenter()
  : success_(true),
    list_(new base::ListValue) {
}
RawDataPresenter::~RawDataPresenter() {}

void RawDataPresenter::FeedNext(const net::UploadElementReader& reader) {
  if (!success_)
    return;

  if (reader.AsBytesReader()) {
    const net::UploadBytesElementReader* bytes_reader = reader.AsBytesReader();
    FeedNextBytes(bytes_reader->bytes(), bytes_reader->length());
  } else if (reader.AsFileReader()) {
    // Insert the file path instead of the contents, which may be too large.
    const net::UploadFileElementReader* file_reader = reader.AsFileReader();
    FeedNextFile(file_reader->path().AsUTF8Unsafe());
  } else {
    NOTIMPLEMENTED();
  }
}

bool RawDataPresenter::Succeeded() {
  return success_;
}

scoped_ptr<Value> RawDataPresenter::Result() {
  if (!success_)
    return scoped_ptr<Value>();

  return list_.PassAs<Value>();
}

void RawDataPresenter::FeedNextBytes(const char* bytes, size_t size) {
  subtle::AppendKeyValuePair(keys::kRequestBodyRawBytesKey,
                             BinaryValue::CreateWithCopiedBuffer(bytes, size),
                             list_.get());
}

void RawDataPresenter::FeedNextFile(const std::string& filename) {
  // Insert the file path instead of the contents, which may be too large.
  subtle::AppendKeyValuePair(keys::kRequestBodyRawFileKey,
                             Value::CreateStringValue(filename),
                             list_.get());
}

// Implementation of ParsedDataPresenter.

ParsedDataPresenter::ParsedDataPresenter(const net::URLRequest& request)
  : parser_(FormDataParser::Create(request)),
    success_(parser_.get() != NULL),
    dictionary_(success_ ? new DictionaryValue() : NULL) {
}

ParsedDataPresenter::~ParsedDataPresenter() {}

void ParsedDataPresenter::FeedNext(const net::UploadElementReader& reader) {
  if (!success_)
    return;

  const net::UploadBytesElementReader* bytes_reader = reader.AsBytesReader();
  if (!bytes_reader) {
    return;
  }
  if (!parser_->SetSource(base::StringPiece(bytes_reader->bytes(),
                                            bytes_reader->length()))) {
    Abort();
    return;
  }

  FormDataParser::Result result;
  while (parser_->GetNextNameValue(&result)) {
    GetOrCreateList(dictionary_.get(), result.name())->Append(
        new StringValue(result.value()));
  }
}

bool ParsedDataPresenter::Succeeded() {
  if (success_ && !parser_->AllDataReadOK())
    Abort();
  return success_;
}

scoped_ptr<Value> ParsedDataPresenter::Result() {
  if (!success_)
    return scoped_ptr<Value>();

  return dictionary_.PassAs<Value>();
}

// static
scoped_ptr<ParsedDataPresenter> ParsedDataPresenter::CreateForTests() {
  const std::string form_type("application/x-www-form-urlencoded");
  return scoped_ptr<ParsedDataPresenter>(new ParsedDataPresenter(form_type));
}

ParsedDataPresenter::ParsedDataPresenter(const std::string& form_type)
  : parser_(FormDataParser::CreateFromContentTypeHeader(&form_type)),
    success_(parser_.get() != NULL),
    dictionary_(success_ ? new DictionaryValue() : NULL) {
}

void ParsedDataPresenter::Abort() {
  success_ = false;
  dictionary_.reset();
  parser_.reset();
}

}  // namespace extensions
