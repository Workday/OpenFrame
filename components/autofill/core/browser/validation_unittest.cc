// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "components/autofill/core/browser/validation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill {
namespace {

struct ExpirationDate {
  const char* year;
  const char* month;
};

struct IntExpirationDate {
  const int year;
  const int month;
};

// From https://www.paypalobjects.com/en_US/vhelp/paypalmanager_help/credit_card_numbers.htm
const char* const kValidNumbers[] = {
  "378282246310005",
  "3714 4963 5398 431",
  "3787-3449-3671-000",
  "5610591081018250",
  "3056 9309 0259 04",
  "3852-0000-0232-37",
  "6011111111111117",
  "6011 0009 9013 9424",
  "3530-1113-3330-0000",
  "3566002020360505",
  "5555 5555 5555 4444",
  "5105-1051-0510-5100",
  "4111111111111111",
  "4012 8888 8888 1881",
  "4222-2222-2222-2",
  "5019717010103742",
  "6331101999990016",

  // A UnionPay card that doesn't pass the Luhn checksum
  "6200000000000000",
};
const char* const kInvalidNumbers[] = {
  "4111 1111 112", /* too short */
  "41111111111111111115", /* too long */
  "4111-1111-1111-1110", /* wrong Luhn checksum */
  "3056 9309 0259 04aa", /* non-digit characters */
};
const char kCurrentDate[]="1 May 2013";
const ExpirationDate kValidCreditCardExpirationDate[] = {
  { "2013", "5" },  // Valid month in current year.
  { "2014", "1" },  // Any month in next year.
  { "2014", " 1" },  // Whitespace in month.
  { " 2014", "1" },  // Whitespace in year.
};
const IntExpirationDate kValidCreditCardIntExpirationDate[] = {
  { 2013, 5 },  // Valid month in current year.
  { 2014, 1 },  // Any month in next year.
};
const ExpirationDate kInvalidCreditCardExpirationDate[] = {
  { "2013", "04" },  // Previous month in current year.
  { "2012", "12" },  // Any month in previous year.
};
const IntExpirationDate kInvalidCreditCardIntExpirationDate[] = {
  { 2013, 4 },  // Previous month in current year.
  { 2012, 12 },  // Any month in previous year.
};
const char* const kValidCreditCardSecurityCode[] = {
  "323",  // 3-digit CSC.
  "3234",  // 4-digit CSC.
};
const char* const kInvalidCreditCardSecurityCode[] = {
  "32",  // CSC too short.
  "12345",  // CSC too long.
  "asd",  // non-numeric CSC.
};
const char* const kValidEmailAddress[] = {
  "user@example",
  "user@example.com",
  "user@subdomain.example.com",
  "user+postfix@example.com",
};
const char* const kInvalidEmailAddress[] = {
  "user",
  "foo.com",
  "user@",
  "user@=example.com"
};
const char kAmericanExpressCard[] = "341111111111111";
const char kVisaCard[] = "4111111111111111";
const char kAmericanExpressCVC[] = "1234";
const char kVisaCVC[] = "123";
}  // namespace

TEST(AutofillValidation, IsValidCreditCardNumber) {
  for (size_t i = 0; i < arraysize(kValidNumbers); ++i) {
    SCOPED_TRACE(kValidNumbers[i]);
    EXPECT_TRUE(
        autofill::IsValidCreditCardNumber(ASCIIToUTF16(kValidNumbers[i])));
  }
  for (size_t i = 0; i < arraysize(kInvalidNumbers); ++i) {
    SCOPED_TRACE(kInvalidNumbers[i]);
    EXPECT_FALSE(
        autofill::IsValidCreditCardNumber(ASCIIToUTF16(kInvalidNumbers[i])));
  }
}

TEST(AutofillValidation, IsValidCreditCardExpirationDate)
{
  base::Time now;
  ASSERT_TRUE(base::Time::FromString(kCurrentDate, &now));

  for (size_t i = 0; i < arraysize(kValidCreditCardExpirationDate); ++i) {
    const ExpirationDate& data = kValidCreditCardExpirationDate[i];
    SCOPED_TRACE(data.year);
    SCOPED_TRACE(data.month);
    EXPECT_TRUE(
        autofill::IsValidCreditCardExpirationDate(ASCIIToUTF16(data.year),
                                                  ASCIIToUTF16(data.month),
                                                  now));
  }
  for (size_t i = 0; i < arraysize(kInvalidCreditCardExpirationDate); ++i) {
    const ExpirationDate& data = kInvalidCreditCardExpirationDate[i];
    SCOPED_TRACE(data.year);
    SCOPED_TRACE(data.month);
    EXPECT_TRUE(
        !autofill::IsValidCreditCardExpirationDate(ASCIIToUTF16(data.year),
                                                   ASCIIToUTF16(data.month),
                                                   now));
  }
}

TEST(AutofillValidation, IsValidCreditCardIntExpirationDate)
{
  base::Time now;
  ASSERT_TRUE(base::Time::FromString(kCurrentDate, &now));

  for (size_t i = 0; i < arraysize(kValidCreditCardIntExpirationDate); ++i) {
    const IntExpirationDate& data = kValidCreditCardIntExpirationDate[i];
    SCOPED_TRACE(data.year);
    SCOPED_TRACE(data.month);
    EXPECT_TRUE(
        autofill::IsValidCreditCardExpirationDate(data.year, data.month, now));
  }
  for (size_t i = 0; i < arraysize(kInvalidCreditCardIntExpirationDate); ++i) {
    const IntExpirationDate& data = kInvalidCreditCardIntExpirationDate[i];
    SCOPED_TRACE(data.year);
    SCOPED_TRACE(data.month);
    EXPECT_TRUE(
        !autofill::IsValidCreditCardExpirationDate(data.year, data.month, now));
  }
}
TEST(AutofillValidation, IsValidCreditCardSecurityCode) {
  for (size_t i = 0; i < arraysize(kValidCreditCardSecurityCode); ++i) {
    SCOPED_TRACE(kValidCreditCardSecurityCode[i]);
    EXPECT_TRUE(
        autofill::IsValidCreditCardSecurityCode(
            ASCIIToUTF16(kValidCreditCardSecurityCode[i])));
  }
  for (size_t i = 0; i < arraysize(kInvalidCreditCardSecurityCode); ++i) {
    SCOPED_TRACE(kInvalidCreditCardSecurityCode[i]);
    EXPECT_FALSE(
        autofill::IsValidCreditCardSecurityCode(
            ASCIIToUTF16(kInvalidCreditCardSecurityCode[i])));
  }
}

TEST(AutofillValidation, IsValidEmailAddress) {
  for (size_t i = 0; i < arraysize(kValidEmailAddress); ++i) {
    SCOPED_TRACE(kValidEmailAddress[i]);
    EXPECT_TRUE(
        autofill::IsValidEmailAddress(ASCIIToUTF16(kValidEmailAddress[i])));
  }
  for (size_t i = 0; i < arraysize(kInvalidEmailAddress); ++i) {
    SCOPED_TRACE(kInvalidEmailAddress[i]);
    EXPECT_FALSE(
        autofill::IsValidEmailAddress(ASCIIToUTF16(kInvalidEmailAddress[i])));
  }
}

TEST(AutofillValidation, IsValidCreditCardSecurityCodeWithNumber) {
  EXPECT_TRUE(autofill::IsValidCreditCardSecurityCode(
      ASCIIToUTF16(kAmericanExpressCVC), ASCIIToUTF16(kAmericanExpressCard)));
  EXPECT_TRUE(autofill::IsValidCreditCardSecurityCode(
      ASCIIToUTF16(kVisaCVC), ASCIIToUTF16(kVisaCard)));
  EXPECT_FALSE(autofill::IsValidCreditCardSecurityCode(
      ASCIIToUTF16(kVisaCVC), ASCIIToUTF16(kAmericanExpressCard)));
  EXPECT_FALSE(autofill::IsValidCreditCardSecurityCode(
      ASCIIToUTF16(kAmericanExpressCVC), ASCIIToUTF16(kVisaCard)));
  EXPECT_TRUE(autofill::IsValidCreditCardSecurityCode(
      ASCIIToUTF16(kVisaCVC), ASCIIToUTF16(kInvalidNumbers[0])));
  EXPECT_FALSE(autofill::IsValidCreditCardSecurityCode(
      ASCIIToUTF16(kAmericanExpressCVC), ASCIIToUTF16(kInvalidNumbers[0])));
}

}  // namespace autofill
