// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PARSERS_METADATA_PARSER_FILEBASE_H_
#define CHROME_BROWSER_PARSERS_METADATA_PARSER_FILEBASE_H_

#include <string>

#include "base/basictypes.h"
#include "base/containers/hash_tables.h"
#include "base/files/file_path.h"
#include "chrome/browser/parsers/metadata_parser.h"

typedef base::hash_map<std::string, std::string> PropertyMap;

// Parser for the file type. Allows for parsing of files, and gets
// properties associated with files.
class FileMetadataParser : public MetadataParser {
 public:
  explicit FileMetadataParser(const base::FilePath& path);

  virtual ~FileMetadataParser();

  // Implementation of MetadataParser
  virtual bool Parse() OVERRIDE;
  virtual bool GetProperty(const std::string& key, std::string* value) OVERRIDE;

  virtual MetadataPropertyIterator* GetPropertyIterator() OVERRIDE;

 protected:
  PropertyMap properties_;
  base::FilePath path_;

 private:
  DISALLOW_COPY_AND_ASSIGN(FileMetadataParser);
};

class FileMetadataPropertyIterator : public MetadataPropertyIterator {
 public:
  explicit FileMetadataPropertyIterator(PropertyMap& properties);

  virtual ~FileMetadataPropertyIterator();

  // Implementation of MetadataPropertyIterator
  virtual bool GetNext(std::string* key, std::string* value) OVERRIDE;
  virtual int Length() OVERRIDE;
  virtual bool IsEnd() OVERRIDE;

 private:
  PropertyMap& properties_;
  PropertyMap::iterator it;

  DISALLOW_COPY_AND_ASSIGN(FileMetadataPropertyIterator);
};

#endif  // CHROME_BROWSER_PARSERS_METADATA_PARSER_FILEBASE_H_
