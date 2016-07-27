// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "components/autofill/core/browser/autofill_server_field_info.h"
#include "components/autofill/core/browser/autofill_xml_parser.h"
#include "components/autofill/core/browser/field_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill {
namespace {

class AutofillQueryXmlParserTest : public testing::Test {
 public:
  AutofillQueryXmlParserTest(): upload_required_(USE_UPLOAD_RATES) {}
  ~AutofillQueryXmlParserTest() override{};

 protected:
  void ParseQueryXML(std::string xml, bool should_succeed) {
    EXPECT_EQ(should_succeed,
              ParseAutofillQueryXml(std::move(xml), &field_infos_,
                                    &upload_required_));
  }

  std::vector<AutofillServerFieldInfo> field_infos_;
  UploadRequired upload_required_;
};

class AutofillUploadXmlParserTest : public testing::Test {
 public:
  AutofillUploadXmlParserTest(): positive_(0), negative_(0) {}
  ~AutofillUploadXmlParserTest() override{};

 protected:
  void ParseUploadXML(std::string xml, bool should_succeed) {
    EXPECT_EQ(should_succeed,
              ParseAutofillUploadXml(std::move(xml), &positive_, &negative_));
  }

  double positive_;
  double negative_;
};

TEST_F(AutofillQueryXmlParserTest, BasicQuery) {
  // An XML string representing a basic query response.
  std::string xml = "<autofillqueryresponse>"
                    "<field autofilltype=\"0\" />"
                    "<field autofilltype=\"1\" />"
                    "<field autofilltype=\"3\" />"
                    "<field autofilltype=\"2\" />"
                    "<field autofilltype=\"61\" defaultvalue=\"default\"/>"
                    "</autofillqueryresponse>";
  ParseQueryXML(std::move(xml), true);

  EXPECT_EQ(USE_UPLOAD_RATES, upload_required_);
  ASSERT_EQ(5U, field_infos_.size());
  EXPECT_EQ(NO_SERVER_DATA, field_infos_[0].field_type);
  EXPECT_EQ(UNKNOWN_TYPE, field_infos_[1].field_type);
  EXPECT_EQ(NAME_FIRST, field_infos_[2].field_type);
  EXPECT_EQ(EMPTY_TYPE, field_infos_[3].field_type);
  EXPECT_TRUE(field_infos_[3].default_value.empty());
  EXPECT_EQ(FIELD_WITH_DEFAULT_VALUE, field_infos_[4].field_type);
  EXPECT_EQ("default", field_infos_[4].default_value);
}

// Test parsing the upload required attribute.
TEST_F(AutofillQueryXmlParserTest, TestUploadRequired) {
  std::string xml = "<autofillqueryresponse uploadrequired=\"true\">"
                    "<field autofilltype=\"0\" />"
                    "</autofillqueryresponse>";

  ParseQueryXML(std::move(xml), true);

  EXPECT_EQ(upload_required_, upload_required_);
  ASSERT_EQ(1U, field_infos_.size());
  EXPECT_EQ(NO_SERVER_DATA, field_infos_[0].field_type);

  field_infos_.clear();
  xml = "<autofillqueryresponse uploadrequired=\"false\">"
        "<field autofilltype=\"0\" />"
        "</autofillqueryresponse>";

  ParseQueryXML(std::move(xml), true);

  EXPECT_EQ(UPLOAD_NOT_REQUIRED, upload_required_);
  ASSERT_EQ(1U, field_infos_.size());
  EXPECT_EQ(NO_SERVER_DATA, field_infos_[0].field_type);

  field_infos_.clear();
  xml = "<autofillqueryresponse uploadrequired=\"bad_value\">"
        "<field autofilltype=\"0\" />"
        "</autofillqueryresponse>";

  ParseQueryXML(std::move(xml), true);

  EXPECT_EQ(USE_UPLOAD_RATES, upload_required_);
  ASSERT_EQ(1U, field_infos_.size());
  EXPECT_EQ(NO_SERVER_DATA, field_infos_[0].field_type);
}

// Test badly formed XML queries.
TEST_F(AutofillQueryXmlParserTest, ParseErrors) {
  // Test no Autofill type.
  std::string xml = "<autofillqueryresponse>"
                    "<field/>"
                    "</autofillqueryresponse>";

  ParseQueryXML(std::move(xml), false);

  // Test an incorrect Autofill type.
  xml = "<autofillqueryresponse>"
        "<field autofilltype=\"-1\"/>"
        "</autofillqueryresponse>";

  ParseQueryXML(std::move(xml), true);

  EXPECT_EQ(USE_UPLOAD_RATES, upload_required_);
  ASSERT_EQ(1U, field_infos_.size());
  // AutofillType was out of range and should be set to NO_SERVER_DATA.
  EXPECT_EQ(NO_SERVER_DATA, field_infos_[0].field_type);

  // Test upper bound for the field type, MAX_VALID_FIELD_TYPE.
  field_infos_.clear();
  xml = "<autofillqueryresponse><field autofilltype=\"" +
      base::IntToString(MAX_VALID_FIELD_TYPE) + "\"/></autofillqueryresponse>";

  ParseQueryXML(std::move(xml), true);

  EXPECT_EQ(USE_UPLOAD_RATES, upload_required_);
  ASSERT_EQ(1U, field_infos_.size());
  // AutofillType was out of range and should be set to NO_SERVER_DATA.
  EXPECT_EQ(NO_SERVER_DATA, field_infos_[0].field_type);

  // Test an incorrect Autofill type.
  field_infos_.clear();
  xml = "<autofillqueryresponse>"
        "<field autofilltype=\"No Type\"/>"
        "</autofillqueryresponse>";

  // Unknown autofill type is handled gracefully.
  ParseQueryXML(std::move(xml), true);

  EXPECT_EQ(USE_UPLOAD_RATES, upload_required_);
  ASSERT_EQ(1U, field_infos_.size());
  EXPECT_EQ(NO_SERVER_DATA, field_infos_[0].field_type);
}

// Test successful upload response.
TEST_F(AutofillUploadXmlParserTest, TestSuccessfulResponse) {
  ParseUploadXML("<autofilluploadresponse positiveuploadrate=\"0.5\" "
                 "negativeuploadrate=\"0.3\"/>",
                 true);

  EXPECT_DOUBLE_EQ(0.5, positive_);
  EXPECT_DOUBLE_EQ(0.3, negative_);
}

// Test failed upload response.
TEST_F(AutofillUploadXmlParserTest, TestFailedResponse) {
  ParseUploadXML("<autofilluploadresponse positiveuploadrate=\"\" "
                 "negativeuploadrate=\"0.3\"/>",
                 false);

  ParseUploadXML("<autofilluploadresponse positiveuploadrate=\"0.5\" "
                 "negativeuploadrate=\"0.3\"",
                 false);

  ParseUploadXML("bad data", false);

  ParseUploadXML(std::string(), false);
}

}  // namespace
}  // namespace autofill
