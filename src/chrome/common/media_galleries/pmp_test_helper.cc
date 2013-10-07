// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/media_galleries/pmp_test_helper.h"

#include <algorithm>
#include <iterator>

#include "base/file_util.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/media_galleries/picasa_types.h"

namespace picasa {

namespace {

// Flatten a vector of elements into an array of bytes.
template<class T>
std::vector<char> Flatten(const std::vector<T>& elems) {
  if (elems.empty())
    return std::vector<char>();

  const uint8* elems0 = reinterpret_cast<const uint8*>(&elems[0]);
  std::vector<char> data_body(elems0, elems0 + sizeof(T) * elems.size());
  return data_body;
}

// Custom specialization for std::string.
template<>
std::vector<char> Flatten(const std::vector<std::string>& strings) {
  std::vector<char> totalchars;

  for (std::vector<std::string>::const_iterator it = strings.begin();
      it != strings.end(); ++it) {
    std::copy(it->begin(), it->end(), std::back_inserter(totalchars));
    totalchars.push_back('\0');  // Add the null termination too.
  }

  return totalchars;
}

// Returns a new vector with the concatenated contents of |a| and |b|.
std::vector<char> CombinedVectors(const std::vector<char>& a,
                                  const std::vector<char>& b) {
  std::vector<char> total;

  std::copy(a.begin(), a.end(), std::back_inserter(total));
  std::copy(b.begin(), b.end(), std::back_inserter(total));

  return total;
}

}  // namespace

PmpTestHelper::PmpTestHelper(const std::string& table_name)
    : table_name_(table_name) {
}

bool PmpTestHelper::Init() {
  if (!temp_dir_.CreateUniqueTempDir() || !temp_dir_.IsValid())
    return false;

  base::FilePath indicator_path = temp_dir_.path().Append(
      base::FilePath::FromUTF8Unsafe(table_name_ + "_0"));

  return file_util::WriteFile(indicator_path, NULL, 0) == 0;
}

base::FilePath PmpTestHelper::GetTempDirPath() {
  DCHECK(temp_dir_.IsValid());
  return temp_dir_.path();
}

template<class T>
bool PmpTestHelper::WriteColumnFileFromVector(
    const std::string& column_name, const PmpFieldType field_type,
    const std::vector<T>& elements_vector) {
  DCHECK(temp_dir_.IsValid());

  std::string file_name = table_name_ + "_" + column_name + "." + kPmpExtension;

  base::FilePath path = temp_dir_.path().Append(
      base::FilePath::FromUTF8Unsafe(file_name));

  std::vector<char> data = PmpTestHelper::MakeHeaderAndBody(
      field_type, elements_vector.size(), elements_vector);

  size_t bytes_written = file_util::WriteFile(path, &data[0], data.size());
  return (bytes_written == data.size());
}

// Explicit Instantiation for all the valid types.
template bool PmpTestHelper::WriteColumnFileFromVector<std::string>(
    const std::string&, const PmpFieldType, const std::vector<std::string>&);
template bool PmpTestHelper::WriteColumnFileFromVector<uint32>(
    const std::string&, const PmpFieldType, const std::vector<uint32>&);
template bool PmpTestHelper::WriteColumnFileFromVector<double>(
    const std::string&, const PmpFieldType, const std::vector<double>&);
template bool PmpTestHelper::WriteColumnFileFromVector<uint8>(
    const std::string&, const PmpFieldType, const std::vector<uint8>&);
template bool PmpTestHelper::WriteColumnFileFromVector<uint64>(
    const std::string&, const PmpFieldType, const std::vector<uint64>&);

// Return a vector so we don't have to worry about memory management.
std::vector<char> PmpTestHelper::MakeHeader(const PmpFieldType field_type,
                                            const uint32 row_count) {
  std::vector<char> header(picasa::kPmpHeaderSize);

  // Copy in magic bytes.
  memcpy(&header[picasa::kPmpMagic1Offset], &picasa::kPmpMagic1,
         sizeof(picasa::kPmpMagic1));
  memcpy(&header[picasa::kPmpMagic2Offset], &picasa::kPmpMagic2,
         sizeof(picasa::kPmpMagic2));
  memcpy(&header[picasa::kPmpMagic3Offset], &picasa::kPmpMagic3,
         sizeof(picasa::kPmpMagic3));
  memcpy(&header[picasa::kPmpMagic4Offset], &picasa::kPmpMagic4,
         sizeof(picasa::kPmpMagic4));

  // Copy in field type.
  uint16 field_type_short = static_cast<uint16>(field_type);
  memcpy(&header[picasa::kPmpFieldType1Offset], &field_type_short,
         sizeof(uint16));
  memcpy(&header[picasa::kPmpFieldType2Offset], &field_type_short,
         sizeof(uint16));

  // Copy in row count.
  memcpy(&header[picasa::kPmpRowCountOffset], &row_count, sizeof(uint32));

  return header;
}

template<class T>
std::vector<char> PmpTestHelper::MakeHeaderAndBody(
    const PmpFieldType field_type, const uint32 row_count,
    const std::vector<T>& elems) {
  return CombinedVectors(PmpTestHelper::MakeHeader(field_type, row_count),
                         Flatten(elems));
}

// Explicit Instantiation for all the valid types.
template std::vector<char> PmpTestHelper::MakeHeaderAndBody<std::string>(
    const PmpFieldType, const uint32, const std::vector<std::string>&);
template std::vector<char> PmpTestHelper::MakeHeaderAndBody<uint32>(
    const PmpFieldType, const uint32, const std::vector<uint32>&);
template std::vector<char> PmpTestHelper::MakeHeaderAndBody<double>(
    const PmpFieldType, const uint32, const std::vector<double>&);
template std::vector<char> PmpTestHelper::MakeHeaderAndBody<uint8>(
    const PmpFieldType, const uint32, const std::vector<uint8>&);
template std::vector<char> PmpTestHelper::MakeHeaderAndBody<uint64>(
    const PmpFieldType, const uint32, const std::vector<uint64>&);

}  // namespace picasa
