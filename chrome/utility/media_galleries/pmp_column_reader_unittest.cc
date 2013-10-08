// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <vector>

#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/platform_file.h"
#include "chrome/common/media_galleries/pmp_constants.h"
#include "chrome/common/media_galleries/pmp_test_helper.h"
#include "chrome/utility/media_galleries/pmp_column_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace picasa {

namespace {

bool InitColumnReaderFromBytes(
    PmpColumnReader* const reader,
    const std::vector<char>& data,
    const PmpFieldType expected_type) {
  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir())
    return false;

  base::FilePath temp_path;
  if (!file_util::CreateTemporaryFileInDir(temp_dir.path(), &temp_path))
    return false;

  // Explicit conversion from signed to unsigned.
  size_t bytes_written = file_util::WriteFile(temp_path, &data[0], data.size());
  if (bytes_written != data.size())
    return false;

  int flags = base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_READ;
  base::PlatformFile platform_file =
      base::CreatePlatformFile(temp_path, flags, NULL, NULL);
  if (platform_file == base::kInvalidPlatformFileValue)
    return false;

  bool read_success = reader->ReadFile(platform_file, expected_type);

  base::ClosePlatformFile(platform_file);

  return read_success;
}

// Overridden version of Read method to make test code templatable.
bool DoRead(const PmpColumnReader* reader, uint32 row, std::string* target) {
  return reader->ReadString(row, target);
}

bool DoRead(const PmpColumnReader* reader, uint32 row, uint32* target) {
  return reader->ReadUInt32(row, target);
}

bool DoRead(const PmpColumnReader* reader, uint32 row, double* target) {
  return reader->ReadDouble64(row, target);
}

bool DoRead(const PmpColumnReader* reader, uint32 row, uint8* target) {
  return reader->ReadUInt8(row, target);
}

bool DoRead(const PmpColumnReader* reader, uint32 row, uint64* target) {
  return reader->ReadUInt64(row, target);
}

// TestValid
template<class T>
void TestValid(const PmpFieldType field_type,
               const std::vector<T>& elems) {
  PmpTestHelper test_helper("test");
  ASSERT_TRUE(test_helper.Init());

  PmpColumnReader reader;
  std::vector<char> data =
      PmpTestHelper::MakeHeaderAndBody(field_type, elems.size(), elems);
  ASSERT_TRUE(InitColumnReaderFromBytes(&reader, data, field_type));
  EXPECT_EQ(elems.size(), reader.rows_read());

  for (uint32 i = 0; i < elems.size() && i < reader.rows_read(); i++) {
    T target;
    EXPECT_TRUE(DoRead(&reader, i, &target));
    EXPECT_EQ(elems[i], target);
  }
}

template<class T>
void TestMalformed(const PmpFieldType field_type,
                   const std::vector<T>& elems) {
  PmpTestHelper test_helper("test");
  ASSERT_TRUE(test_helper.Init());

  PmpColumnReader reader_too_few_declared_rows;
  std::vector<char> data_too_few_declared_rows =
      PmpTestHelper::MakeHeaderAndBody(field_type, elems.size()-1, elems);
  EXPECT_FALSE(InitColumnReaderFromBytes(&reader_too_few_declared_rows,
                                         data_too_few_declared_rows,
                                         field_type));

  PmpColumnReader reader_too_many_declared_rows;
  std::vector<char> data_too_many_declared_rows =
      PmpTestHelper::MakeHeaderAndBody(field_type, elems.size()+1, elems);
  EXPECT_FALSE(InitColumnReaderFromBytes(&reader_too_many_declared_rows,
                                         data_too_many_declared_rows,
                                         field_type));

  PmpColumnReader reader_truncated;
  std::vector<char> data_truncated =
      PmpTestHelper::MakeHeaderAndBody(field_type, elems.size(), elems);
  data_truncated.resize(data_truncated.size()-10);
  EXPECT_FALSE(InitColumnReaderFromBytes(&reader_truncated,
                                         data_truncated,
                                         field_type));

  PmpColumnReader reader_padded;
  std::vector<char> data_padded =
      PmpTestHelper::MakeHeaderAndBody(field_type, elems.size(), elems);
  data_padded.resize(data_padded.size()+10);
  EXPECT_FALSE(InitColumnReaderFromBytes(&reader_padded,
                                         data_padded,
                                         field_type));
}

template<class T>
void TestPrimitive(const PmpFieldType field_type) {
  // Make an ascending vector of the primitive.
  uint32 n = 100;
  std::vector<T> data(n, 0);
  for (uint32 i = 0; i < n; i++) {
    data[i] = i*3;
  }

  TestValid<T>(field_type, data);
  TestMalformed<T>(field_type, data);
}


TEST(PmpColumnReaderTest, HeaderParsingAndValidation) {
  PmpTestHelper test_helper("test");
  ASSERT_TRUE(test_helper.Init());

  PmpColumnReader reader_good_header;
  std::vector<char> good_header =
      PmpTestHelper::MakeHeader(PMP_TYPE_STRING, 0);
  EXPECT_TRUE(InitColumnReaderFromBytes(&reader_good_header,
                                        good_header,
                                        PMP_TYPE_STRING));
  EXPECT_EQ(0U, reader_good_header.rows_read()) <<
      "Read non-zero rows from header-only data.";

  PmpColumnReader reader_bad_magic_bytes;
  std::vector<char> bad_magic_bytes =
      PmpTestHelper::MakeHeader(PMP_TYPE_STRING, 0);
  bad_magic_bytes[0] = (char)0xff;
  EXPECT_FALSE(InitColumnReaderFromBytes(&reader_bad_magic_bytes,
                                         bad_magic_bytes,
                                         PMP_TYPE_STRING));

  PmpColumnReader reader_inconsistent_types;
  std::vector<char> inconsistent_type =
      PmpTestHelper::MakeHeader(PMP_TYPE_STRING, 0);
  inconsistent_type[kPmpFieldType1Offset] = (char)0xff;
  EXPECT_FALSE(InitColumnReaderFromBytes(&reader_inconsistent_types,
                                         inconsistent_type,
                                         PMP_TYPE_STRING));

  PmpColumnReader reader_invalid_type;
  std::vector<char> invalid_type =
      PmpTestHelper::MakeHeader(PMP_TYPE_STRING, 0);
  invalid_type[kPmpFieldType1Offset] = (char)0xff;
  invalid_type[kPmpFieldType2Offset] = (char)0xff;
  EXPECT_FALSE(InitColumnReaderFromBytes(&reader_invalid_type,
                                         invalid_type,
                                         PMP_TYPE_STRING));

  PmpColumnReader reader_incomplete_header;
  std::vector<char> incomplete_header =
      PmpTestHelper::MakeHeader(PMP_TYPE_STRING, 0);
  incomplete_header.resize(10);
  EXPECT_FALSE(InitColumnReaderFromBytes(&reader_incomplete_header,
                                         incomplete_header,
                                         PMP_TYPE_STRING));
}

TEST(PmpColumnReaderTest, StringParsing) {
  std::vector<std::string> empty_strings(100, "");

  // Test empty strings read okay.
  TestValid(PMP_TYPE_STRING, empty_strings);

  std::vector<std::string> mixed_strings;
  mixed_strings.push_back("");
  mixed_strings.push_back("Hello");
  mixed_strings.push_back("World");
  mixed_strings.push_back("");
  mixed_strings.push_back("123123");
  mixed_strings.push_back("Q");
  mixed_strings.push_back("");

  // Test that a mixed set of strings read correctly.
  TestValid(PMP_TYPE_STRING, mixed_strings);

  // Test with the data messed up in a variety of ways.
  TestMalformed(PMP_TYPE_STRING, mixed_strings);
}

TEST(PmpColumnReaderTest, PrimitiveParsing) {
  TestPrimitive<uint32>(PMP_TYPE_UINT32);
  TestPrimitive<double>(PMP_TYPE_DOUBLE64);
  TestPrimitive<uint8>(PMP_TYPE_UINT8);
  TestPrimitive<uint64>(PMP_TYPE_UINT64);
}

}  // namespace

}  // namespace picasa
