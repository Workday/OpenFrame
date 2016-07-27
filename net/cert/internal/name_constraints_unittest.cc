// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/internal/name_constraints.h"

#include "net/cert/internal/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

::testing::AssertionResult LoadTestData(const char* token,
                                        const std::string& basename,
                                        std::string* result) {
  std::string path = "net/data/name_constraints_unittest/" + basename;

  const PemBlockMapping mappings[] = {
      {token, result},
  };

  return ReadTestDataFromPemFile(path, mappings);
}

::testing::AssertionResult LoadTestName(const std::string& basename,
                                        std::string* result) {
  return LoadTestData("NAME", basename, result);
}

::testing::AssertionResult LoadTestNameConstraint(const std::string& basename,
                                                  std::string* result) {
  return LoadTestData("NAME CONSTRAINTS", basename, result);
}

::testing::AssertionResult LoadTestSubjectAltName(const std::string& basename,
                                                  std::string* result) {
  return LoadTestData("SUBJECT ALTERNATIVE NAME", basename, result);
}

}  // namespace

class ParseNameConstraints
    : public ::testing::TestWithParam<::testing::tuple<bool>> {
 public:
  bool is_critical() const { return ::testing::get<0>(GetParam()); }
};

// Run the tests with the name constraints marked critical and non-critical. For
// supported name types, the results should be the same for both.
INSTANTIATE_TEST_CASE_P(InstantiationName,
                        ParseNameConstraints,
                        ::testing::Values(true, false));

TEST_P(ParseNameConstraints, DNSNames) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("dnsname.pem", &a));

  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  EXPECT_TRUE(name_constraints->IsPermittedDNSName("permitted.example.com"));
  EXPECT_TRUE(name_constraints->IsPermittedDNSName("permitted.example.com."));
  EXPECT_TRUE(name_constraints->IsPermittedDNSName("a.permitted.example.com"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("apermitted.example.com"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("apermitted.example.com."));
  EXPECT_TRUE(
      name_constraints->IsPermittedDNSName("alsopermitted.example.com"));
  EXPECT_FALSE(
      name_constraints->IsPermittedDNSName("excluded.permitted.example.com"));
  EXPECT_FALSE(
      name_constraints->IsPermittedDNSName("a.excluded.permitted.example.com"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName(
      "stillnotpermitted.excluded.permitted.example.com"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName(
      "a.stillnotpermitted.excluded.permitted.example.com"));
  EXPECT_FALSE(
      name_constraints->IsPermittedDNSName("extraneousexclusion.example.com"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName(
      "a.extraneousexclusion.example.com"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("other.example.com"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("other.com"));

  // Wildcard names:
  // Pattern could match excluded.permitted.example.com, thus should not be
  // allowed.
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("*.permitted.example.com"));
  // Entirely within excluded name, obviously not allowed.
  EXPECT_FALSE(
      name_constraints->IsPermittedDNSName("*.excluded.permitted.example.com"));
  // Within permitted.example.com and cannot match any exclusion, thus these are
  // allowed.
  EXPECT_TRUE(
      name_constraints->IsPermittedDNSName("*.foo.permitted.example.com"));
  EXPECT_TRUE(
      name_constraints->IsPermittedDNSName("*.alsopermitted.example.com"));
  // Matches permitted.example2.com, but also matches other .example2.com names
  // which are not in either permitted or excluded, so not allowed.
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("*.example2.com"));
  // Partial wildcards are not supported, so these name are permitted even if
  // it seems like they shouldn't be. It's fine, since certificate verification
  // won't treat them as wildcard names either.
  EXPECT_TRUE(
      name_constraints->IsPermittedDNSName("*xcluded.permitted.example.com"));
  EXPECT_TRUE(
      name_constraints->IsPermittedDNSName("exclude*.permitted.example.com"));
  EXPECT_TRUE(
      name_constraints->IsPermittedDNSName("excl*ded.permitted.example.com"));
  // Garbage wildcard data.
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("*."));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("*.*"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName(".*"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("*"));
  // Matches SAN with trailing dot.
  EXPECT_TRUE(name_constraints->IsPermittedDNSName("permitted.example3.com"));
  EXPECT_TRUE(name_constraints->IsPermittedDNSName("permitted.example3.com."));
  EXPECT_TRUE(name_constraints->IsPermittedDNSName("a.permitted.example3.com"));
  EXPECT_TRUE(
      name_constraints->IsPermittedDNSName("a.permitted.example3.com."));

  EXPECT_EQ(GENERAL_NAME_DNS_NAME, name_constraints->ConstrainedNameTypes());

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-permitted.pem", &san));
  EXPECT_TRUE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));

  ASSERT_TRUE(LoadTestSubjectAltName("san-excluded-dnsname.pem", &san));
  EXPECT_FALSE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));

  ASSERT_TRUE(LoadTestSubjectAltName("san-excluded-directoryname.pem", &san));
  EXPECT_TRUE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));

  ASSERT_TRUE(LoadTestSubjectAltName("san-excluded-ipaddress.pem", &san));
  EXPECT_TRUE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints,
       DNSNamesWithMultipleLevelsBetweenExcludedAndPermitted) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("dnsname2.pem", &a));
  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  // Matches permitted exactly.
  EXPECT_TRUE(name_constraints->IsPermittedDNSName("com"));
  // Contained within permitted and doesn't match excluded (foo.bar.com).
  EXPECT_TRUE(name_constraints->IsPermittedDNSName("bar.com"));
  EXPECT_TRUE(name_constraints->IsPermittedDNSName("baz.bar.com"));
  // Matches excluded exactly.
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("foo.bar.com"));
  // Contained within excluded.
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("baz.foo.bar.com"));

  // Cannot match anything within excluded.
  EXPECT_TRUE(name_constraints->IsPermittedDNSName("*.baz.bar.com"));
  // Wildcard hostnames only match a single label, so cannot match excluded
  // which has two labels before .com.
  EXPECT_TRUE(name_constraints->IsPermittedDNSName("*.com"));

  // Partial match of foo.bar.com.
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("*.bar.com"));
  // All expansions of wildcard are within excluded.
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("*.foo.bar.com"));
}

TEST_P(ParseNameConstraints, DNSNamesWithLeadingDot) {
  std::string a;
  ASSERT_TRUE(
      LoadTestNameConstraint("dnsname-permitted_with_leading_dot.pem", &a));
  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  // dNSName constraints should be specified as a host. A dNSName constraint
  // with a leading "." doesn't make sense, though some certs include it
  // (probably confusing it with the rules for uniformResourceIdentifier
  // constraints). It should not match anything.
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("com"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("bar.com"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("foo.bar.com"));
}

TEST_P(ParseNameConstraints, DNSNamesExcludeOnly) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("dnsname-excluded.pem", &a));

  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  // Only "excluded.permitted.example.com" is excluded, but since no dNSNames
  // are permitted, everything is excluded.
  EXPECT_FALSE(name_constraints->IsPermittedDNSName(""));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("foo.com"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("permitted.example.com"));
  EXPECT_FALSE(
      name_constraints->IsPermittedDNSName("excluded.permitted.example.com"));
  EXPECT_FALSE(
      name_constraints->IsPermittedDNSName("a.excluded.permitted.example.com"));
}

TEST_P(ParseNameConstraints, DNSNamesExcludeAll) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("dnsname-excludeall.pem", &a));

  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  // "permitted.example.com" is in the permitted section, but since "" is
  // excluded, nothing is permitted.
  EXPECT_FALSE(name_constraints->IsPermittedDNSName(""));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("foo.com"));
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("permitted.example.com"));
  EXPECT_FALSE(
      name_constraints->IsPermittedDNSName("foo.permitted.example.com"));
}

TEST_P(ParseNameConstraints, DNSNamesExcludeDot) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("dnsname-exclude_dot.pem", &a));

  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  // "." is excluded, which should match nothing.
  EXPECT_FALSE(name_constraints->IsPermittedDNSName("foo.com"));
  EXPECT_TRUE(name_constraints->IsPermittedDNSName("permitted.example.com"));
  EXPECT_TRUE(
      name_constraints->IsPermittedDNSName("foo.permitted.example.com"));
}

TEST_P(ParseNameConstraints, DNSNamesFailOnInvalidIA5String) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("dnsname.pem", &a));

  size_t replace_location = a.find("permitted.example2.com");
  ASSERT_NE(std::string::npos, replace_location);
  a.replace(replace_location, 1, 1, -1);

  EXPECT_FALSE(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
}

TEST_P(ParseNameConstraints, DirectoryNames) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("directoryname.pem", &constraints_der));

  std::string name_us;
  ASSERT_TRUE(LoadTestName("name-us.pem", &name_us));
  std::string name_us_ca;
  ASSERT_TRUE(LoadTestName("name-us-california.pem", &name_us_ca));
  std::string name_us_ca_mountain_view;
  ASSERT_TRUE(LoadTestName("name-us-california-mountain_view.pem",
                           &name_us_ca_mountain_view));
  std::string name_us_az;
  ASSERT_TRUE(LoadTestName("name-us-arizona.pem", &name_us_az));
  std::string name_jp;
  ASSERT_TRUE(LoadTestName("name-jp.pem", &name_jp));
  std::string name_jp_tokyo;
  ASSERT_TRUE(LoadTestName("name-jp-tokyo.pem", &name_jp_tokyo));
  std::string name_de;
  ASSERT_TRUE(LoadTestName("name-de.pem", &name_de));
  std::string name_ca;
  ASSERT_TRUE(LoadTestName("name-ca.pem", &name_ca));

  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  // Not in any permitted subtree.
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_ca)));
  // Within the permitted C=US subtree.
  EXPECT_TRUE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_us)));
  // Within the permitted C=US subtree.
  EXPECT_TRUE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_us_az)));
  // Within the permitted C=US subtree, however the excluded C=US,ST=California
  // subtree takes priority.
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_us_ca)));
  // Within the permitted C=US subtree as well as the permitted
  // C=US,ST=California,L=Mountain View subtree, however the excluded
  // C=US,ST=California subtree still takes priority.
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_us_ca_mountain_view)));
  // Not in any permitted subtree, and also inside the extraneous excluded C=DE
  // subtree.
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_de)));
  // Not in any permitted subtree.
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_jp)));
  // Within the permitted C=JP,ST=Tokyo subtree.
  EXPECT_TRUE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_jp_tokyo)));

  EXPECT_EQ(GENERAL_NAME_DIRECTORY_NAME,
            name_constraints->ConstrainedNameTypes());

  // Within the permitted C=US subtree.
  EXPECT_TRUE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us), der::Input()));
  // Within the permitted C=US subtree, however the excluded C=US,ST=California
  // subtree takes priority.
  EXPECT_FALSE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_ca), der::Input()));

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-permitted.pem", &san));
  EXPECT_TRUE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));

  ASSERT_TRUE(LoadTestSubjectAltName("san-excluded-dnsname.pem", &san));
  EXPECT_TRUE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));

  ASSERT_TRUE(LoadTestSubjectAltName("san-excluded-directoryname.pem", &san));
  EXPECT_FALSE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));

  ASSERT_TRUE(LoadTestSubjectAltName("san-excluded-ipaddress.pem", &san));
  EXPECT_TRUE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, DirectoryNamesExcludeOnly) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("directoryname-excluded.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  std::string name_empty;
  ASSERT_TRUE(LoadTestName("name-empty.pem", &name_empty));
  std::string name_us;
  ASSERT_TRUE(LoadTestName("name-us.pem", &name_us));
  std::string name_us_ca;
  ASSERT_TRUE(LoadTestName("name-us-california.pem", &name_us_ca));
  std::string name_us_ca_mountain_view;
  ASSERT_TRUE(LoadTestName("name-us-california-mountain_view.pem",
                           &name_us_ca_mountain_view));

  // Only "C=US,ST=California" is excluded, but since no directoryNames are
  // permitted, everything is excluded.
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_empty)));
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_us)));
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_us_ca)));
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_us_ca_mountain_view)));
}

TEST_P(ParseNameConstraints, DirectoryNamesExcludeAll) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("directoryname-excluded.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  std::string name_empty;
  ASSERT_TRUE(LoadTestName("name-empty.pem", &name_empty));
  std::string name_us;
  ASSERT_TRUE(LoadTestName("name-us.pem", &name_us));
  std::string name_us_ca;
  ASSERT_TRUE(LoadTestName("name-us-california.pem", &name_us_ca));
  std::string name_us_ca_mountain_view;
  ASSERT_TRUE(LoadTestName("name-us-california-mountain_view.pem",
                           &name_us_ca_mountain_view));
  std::string name_jp;
  ASSERT_TRUE(LoadTestName("name-jp.pem", &name_jp));

  // "C=US" is in the permitted section, but since an empty
  // directoryName is excluded, nothing is permitted.
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_empty)));
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_us)));
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_us_ca)));
  EXPECT_FALSE(name_constraints->IsPermittedDirectoryName(
      SequenceValueFromString(&name_jp)));
}

TEST_P(ParseNameConstraints, IPAdresses) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("ipaddress.pem", &a));

  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  // IPv4 tests:
  {
    // Not in any permitted range.
    const uint8_t ip4[] = {192, 169, 0, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    // Within the permitted 192.168.0.0/255.255.0.0 range.
    const uint8_t ip4[] = {192, 168, 0, 1};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    // Within the permitted 192.168.0.0/255.255.0.0 range, however the
    // excluded 192.168.5.0/255.255.255.0 takes priority.
    const uint8_t ip4[] = {192, 168, 5, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    // Within the permitted 192.168.0.0/255.255.0.0 range as well as the
    // permitted 192.168.5.32/255.255.255.224 range, however the excluded
    // 192.168.5.0/255.255.255.0 still takes priority.
    const uint8_t ip4[] = {192, 168, 5, 33};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    // Not in any permitted range. (Just outside the
    // 192.167.5.32/255.255.255.224 range.)
    const uint8_t ip4[] = {192, 167, 5, 31};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    // Within the permitted 192.167.5.32/255.255.255.224 range.
    const uint8_t ip4[] = {192, 167, 5, 32};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    // Within the permitted 192.167.5.32/255.255.255.224 range.
    const uint8_t ip4[] = {192, 167, 5, 63};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    // Not in any permitted range. (Just outside the
    // 192.167.5.32/255.255.255.224 range.)
    const uint8_t ip4[] = {192, 167, 5, 64};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    // Not in any permitted range, and also inside the extraneous excluded
    // 192.166.5.32/255.255.255.224 range.
    const uint8_t ip4[] = {192, 166, 5, 32};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }

  // IPv6 tests:
  {
    // Not in any permitted range.
    const uint8_t ip6[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 0, 0, 0, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }
  {
    // Within the permitted
    // 102:304:506:708:90a:b0c::/ffff:ffff:ffff:ffff:ffff:ffff:: range.
    const uint8_t ip6[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 0, 0, 1};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }
  {
    // Within the permitted
    // 102:304:506:708:90a:b0c::/ffff:ffff:ffff:ffff:ffff:ffff:: range, however
    // the excluded
    // 102:304:506:708:90a:b0c:500:0/ffff:ffff:ffff:ffff:ffff:ffff:ff00:0 takes
    // priority.
    const uint8_t ip6[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 5, 0, 0, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }
  {
    // Within the permitted
    // 102:304:506:708:90a:b0c::/ffff:ffff:ffff:ffff:ffff:ffff:: range as well
    // as the permitted
    // 102:304:506:708:90a:b0c:520:0/ffff:ffff:ffff:ffff:ffff:ffff:ff60:0,
    // however the excluded
    // 102:304:506:708:90a:b0c:500:0/ffff:ffff:ffff:ffff:ffff:ffff:ff00:0 takes
    // priority.
    const uint8_t ip6[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 5, 33, 0, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }
  {
    // Not in any permitted range. (Just outside the
    // 102:304:506:708:90a:b0b:520:0/ffff:ffff:ffff:ffff:ffff:ffff:ff60:0
    // range.)
    const uint8_t ip6[] = {1, 2,  3,  4,  5, 6,  7,   8,
                           9, 10, 11, 11, 5, 31, 255, 255};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }
  {
    // Within the permitted
    // 102:304:506:708:90a:b0b:520:0/ffff:ffff:ffff:ffff:ffff:ffff:ff60:0 range.
    const uint8_t ip6[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11, 5, 32, 0, 0};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }
  {
    // Within the permitted
    // 102:304:506:708:90a:b0b:520:0/ffff:ffff:ffff:ffff:ffff:ffff:ff60:0 range.
    const uint8_t ip6[] = {1, 2,  3,  4,  5, 6,  7,   8,
                           9, 10, 11, 11, 5, 63, 255, 255};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }
  {
    // Not in any permitted range. (Just outside the
    // 102:304:506:708:90a:b0b:520:0/ffff:ffff:ffff:ffff:ffff:ffff:ff60:0
    // range.)
    const uint8_t ip6[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11, 5, 64, 0, 0};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }
  {
    // Not in any permitted range, and also inside the extraneous excluded
    // 102:304:506:708:90a:b0a:520:0/ffff:ffff:ffff:ffff:ffff:ffff:ff60:0 range.
    const uint8_t ip6[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 10, 5, 33, 0, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }

  EXPECT_EQ(GENERAL_NAME_IP_ADDRESS, name_constraints->ConstrainedNameTypes());

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-permitted.pem", &san));
  EXPECT_TRUE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));

  ASSERT_TRUE(LoadTestSubjectAltName("san-excluded-dnsname.pem", &san));
  EXPECT_TRUE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));

  ASSERT_TRUE(LoadTestSubjectAltName("san-excluded-directoryname.pem", &san));
  EXPECT_TRUE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));

  ASSERT_TRUE(LoadTestSubjectAltName("san-excluded-ipaddress.pem", &san));
  EXPECT_FALSE(
      name_constraints->IsPermittedCert(der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, IPAdressesExcludeOnly) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("ipaddress-excluded.pem", &a));

  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  // Only 192.168.5.0/255.255.255.0 is excluded, but since no iPAddresses
  // are permitted, everything is excluded.
  {
    const uint8_t ip4[] = {192, 168, 0, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {192, 168, 5, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip6[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 0, 0, 0, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }
}

TEST_P(ParseNameConstraints, IPAdressesExcludeAll) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("ipaddress-excludeall.pem", &a));

  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  // 192.168.0.0/255.255.0.0 and
  // 102:304:506:708:90a:b0c::/ffff:ffff:ffff:ffff:ffff:ffff:: are permitted,
  // but since 0.0.0.0/0 and ::/0 are excluded nothing is permitted.
  {
    const uint8_t ip4[] = {192, 168, 0, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {1, 1, 1, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip6[] = {2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }
  {
    const uint8_t ip6[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 0, 0, 0, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip6, ip6 + arraysize(ip6))));
  }
}

TEST_P(ParseNameConstraints, IPAdressesNetmaskPermitSingleHost) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("ipaddress-permit_singlehost.pem", &a));

  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  {
    const uint8_t ip4[] = {0, 0, 0, 0};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {192, 168, 1, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {192, 168, 1, 2};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {192, 168, 1, 3};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {192, 168, 1, 4};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {255, 255, 255, 255};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
}

TEST_P(ParseNameConstraints, IPAdressesNetmaskPermitPrefixLen31) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("ipaddress-permit_prefix31.pem", &a));

  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  {
    const uint8_t ip4[] = {0, 0, 0, 0};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {192, 168, 1, 1};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {192, 168, 1, 2};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {192, 168, 1, 3};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {192, 168, 1, 4};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {192, 168, 1, 5};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {255, 255, 255, 255};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
}

TEST_P(ParseNameConstraints, IPAdressesNetmaskPermitPrefixLen1) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("ipaddress-permit_prefix1.pem", &a));

  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  {
    const uint8_t ip4[] = {0, 0, 0, 0};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {0x7F, 0xFF, 0xFF, 0xFF};
    EXPECT_FALSE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {0x80, 0, 0, 0};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
}

TEST_P(ParseNameConstraints, IPAdressesNetmaskPermitAll) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("ipaddress-permit_all.pem", &a));

  scoped_ptr<NameConstraints> name_constraints(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
  ASSERT_TRUE(name_constraints);

  {
    const uint8_t ip4[] = {0, 0, 0, 0};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {192, 168, 1, 1};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
  {
    const uint8_t ip4[] = {255, 255, 255, 255};
    EXPECT_TRUE(name_constraints->IsPermittedIP(
        IPAddressNumber(ip4, ip4 + arraysize(ip4))));
  }
}

TEST_P(ParseNameConstraints, IPAdressesFailOnInvalidAddr) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint("ipaddress-invalid_addr.pem", &a));

  EXPECT_FALSE(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
}

TEST_P(ParseNameConstraints, IPAdressesFailOnInvalidMaskNotContiguous) {
  std::string a;
  ASSERT_TRUE(LoadTestNameConstraint(
      "ipaddress-invalid_mask_not_contiguous_1.pem", &a));
  EXPECT_FALSE(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));

  ASSERT_TRUE(LoadTestNameConstraint(
      "ipaddress-invalid_mask_not_contiguous_2.pem", &a));
  EXPECT_FALSE(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));

  ASSERT_TRUE(LoadTestNameConstraint(
      "ipaddress-invalid_mask_not_contiguous_3.pem", &a));
  EXPECT_FALSE(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));

  ASSERT_TRUE(LoadTestNameConstraint(
      "ipaddress-invalid_mask_not_contiguous_4.pem", &a));
  EXPECT_FALSE(
      NameConstraints::CreateFromDer(InputFromString(&a), is_critical()));
}

TEST_P(ParseNameConstraints, OtherNamesInPermitted) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("othername-permitted.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_OTHER_NAME,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-othername.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, OtherNamesInExcluded) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("othername-excluded.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_OTHER_NAME,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-othername.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, Rfc822NamesInPermitted) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("rfc822name-permitted.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_RFC822_NAME,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-rfc822name.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, Rfc822NamesInExcluded) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("rfc822name-excluded.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_RFC822_NAME,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-rfc822name.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, X400AddresssInPermitted) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("x400address-permitted.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_X400_ADDRESS,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-x400address.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, X400AddresssInExcluded) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("x400address-excluded.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_X400_ADDRESS,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-x400address.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, EdiPartyNamesInPermitted) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("edipartyname-permitted.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_EDI_PARTY_NAME,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-edipartyname.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, EdiPartyNamesInExcluded) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("edipartyname-excluded.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_EDI_PARTY_NAME,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-edipartyname.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, URIsInPermitted) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("uri-permitted.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_UNIFORM_RESOURCE_IDENTIFIER,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-uri.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, URIsInExcluded) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("uri-excluded.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_UNIFORM_RESOURCE_IDENTIFIER,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-uri.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, RegisteredIDsInPermitted) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("registeredid-permitted.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_REGISTERED_ID,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-registeredid.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, RegisteredIDsInExcluded) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("registeredid-excluded.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  if (is_critical()) {
    EXPECT_EQ(GENERAL_NAME_REGISTERED_ID,
              name_constraints->ConstrainedNameTypes());
  } else {
    EXPECT_EQ(0, name_constraints->ConstrainedNameTypes());
  }

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-registeredid.pem", &san));
  EXPECT_EQ(!is_critical(), name_constraints->IsPermittedCert(
                                der::Input(), InputFromString(&san)));
}

TEST_P(ParseNameConstraints,
       failsOnGeneralSubtreeWithMinimumZeroEncodedUnnecessarily) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("dnsname-with_min_0.pem", &constraints_der));
  // The value should not be in the DER encoding if it is the default. But this
  // could be changed to allowed if there are buggy encoders out there that
  // include it anyway.
  EXPECT_FALSE(NameConstraints::CreateFromDer(InputFromString(&constraints_der),
                                              is_critical()));
}

TEST_P(ParseNameConstraints, FailsOnGeneralSubtreeWithMinimum) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("dnsname-with_min_1.pem", &constraints_der));
  EXPECT_FALSE(NameConstraints::CreateFromDer(InputFromString(&constraints_der),
                                              is_critical()));
}

TEST_P(ParseNameConstraints,
       failsOnGeneralSubtreeWithMinimumZeroEncodedUnnecessarilyAndMaximum) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("dnsname-with_min_0_and_max.pem",
                                     &constraints_der));
  EXPECT_FALSE(NameConstraints::CreateFromDer(InputFromString(&constraints_der),
                                              is_critical()));
}

TEST_P(ParseNameConstraints, FailsOnGeneralSubtreeWithMinimumAndMaximum) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("dnsname-with_min_1_and_max.pem",
                                     &constraints_der));
  EXPECT_FALSE(NameConstraints::CreateFromDer(InputFromString(&constraints_der),
                                              is_critical()));
}

TEST_P(ParseNameConstraints, FailsOnGeneralSubtreeWithMaximum) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("dnsname-with_max.pem", &constraints_der));
  EXPECT_FALSE(NameConstraints::CreateFromDer(InputFromString(&constraints_der),
                                              is_critical()));
}

TEST_P(ParseNameConstraints, FailsOnEmptyExtensionValue) {
  std::string constraints_der = "";
  EXPECT_FALSE(NameConstraints::CreateFromDer(InputFromString(&constraints_der),
                                              is_critical()));
}

TEST_P(ParseNameConstraints, FailsOnNoPermittedAndExcluded) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("invalid-no_subtrees.pem", &constraints_der));
  EXPECT_FALSE(NameConstraints::CreateFromDer(InputFromString(&constraints_der),
                                              is_critical()));
}

TEST_P(ParseNameConstraints, FailsOnEmptyPermitted) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("invalid-empty_permitted_subtree.pem",
                                     &constraints_der));
  EXPECT_FALSE(NameConstraints::CreateFromDer(InputFromString(&constraints_der),
                                              is_critical()));
}

TEST_P(ParseNameConstraints, FailsOnEmptyExcluded) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("invalid-empty_excluded_subtree.pem",
                                     &constraints_der));
  EXPECT_FALSE(NameConstraints::CreateFromDer(InputFromString(&constraints_der),
                                              is_critical()));
}

TEST_P(ParseNameConstraints, IsPermittedCertSubjectEmailAddressIsOk) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("directoryname.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  std::string name_us_arizona_email;
  ASSERT_TRUE(
      LoadTestName("name-us-arizona-email.pem", &name_us_arizona_email));

  // Name constraints don't contain rfc822Name, so emailAddress in subject is
  // allowed regardless.
  EXPECT_TRUE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_arizona_email), der::Input()));
}

TEST_P(ParseNameConstraints, IsPermittedCertSubjectEmailAddressIsNotOk) {
  std::string constraints_der;
  ASSERT_TRUE(
      LoadTestNameConstraint("rfc822name-permitted.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  std::string name_us_arizona_email;
  ASSERT_TRUE(
      LoadTestName("name-us-arizona-email.pem", &name_us_arizona_email));

  // Name constraints contain rfc822Name, so emailAddress in subject is not
  // allowed if the constraints were critical.
  EXPECT_EQ(!is_critical(),
            name_constraints->IsPermittedCert(
                SequenceValueFromString(&name_us_arizona_email), der::Input()));
}

// Hostname in commonName is not allowed (crbug.com/308330), so these are tests
// are not particularly interesting, just verifying that the commonName is
// ignored for dNSName constraints.
TEST_P(ParseNameConstraints, IsPermittedCertSubjectDnsNames) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("directoryname_and_dnsname.pem",
                                     &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  std::string name_us_az_foocom;
  ASSERT_TRUE(LoadTestName("name-us-arizona-foo.com.pem", &name_us_az_foocom));
  // The subject is within permitted directoryName constraints, so permitted.
  // (The commonName hostname is not within permitted dNSName constraints, so
  // this would not be permitted if hostnames in commonName were checked.)
  EXPECT_TRUE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_az_foocom), der::Input()));

  std::string name_us_az_permitted;
  ASSERT_TRUE(LoadTestName("name-us-arizona-permitted.example.com.pem",
                           &name_us_az_permitted));
  // The subject is in permitted directoryName and the commonName is within
  // permitted dNSName constraints, so this should be permitted regardless if
  // hostnames in commonName are checked or not.
  EXPECT_TRUE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_az_permitted), der::Input()));

  std::string name_us_ca_permitted;
  ASSERT_TRUE(LoadTestName("name-us-california-permitted.example.com.pem",
                           &name_us_ca_permitted));
  // The subject is within the excluded C=US,ST=California directoryName, so
  // this should not be allowed, regardless of checking the
  // permitted.example.com in commonName.
  EXPECT_FALSE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_ca_permitted), der::Input()));
}

// IP addresses in commonName are not allowed (crbug.com/308330), so these are
// tests are not particularly interesting, just verifying that the commonName is
// ignored for iPAddress constraints.
TEST_P(ParseNameConstraints, IsPermittedCertSubjectIpAddresses) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint(
      "directoryname_and_dnsname_and_ipaddress.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  std::string name_us_az_1_1_1_1;
  ASSERT_TRUE(LoadTestName("name-us-arizona-1.1.1.1.pem", &name_us_az_1_1_1_1));
  // The subject is within permitted directoryName constraints, so permitted.
  // (The commonName IP address is not within permitted iPAddresses constraints,
  // so this would not be permitted if IP addresses in commonName were checked.)
  EXPECT_TRUE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_az_1_1_1_1), der::Input()));

  std::string name_us_az_192_168_1_1;
  ASSERT_TRUE(
      LoadTestName("name-us-arizona-192.168.1.1.pem", &name_us_az_192_168_1_1));
  // The subject is in permitted directoryName and the commonName is within
  // permitted iPAddress constraints, so this should be permitted regardless if
  // IP addresses in commonName are checked or not.
  EXPECT_TRUE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_az_192_168_1_1), der::Input()));

  std::string name_us_ca_192_168_1_1;
  ASSERT_TRUE(LoadTestName("name-us-california-192.168.1.1.pem",
                           &name_us_ca_192_168_1_1));
  // The subject is within the excluded C=US,ST=California directoryName, so
  // this should not be allowed, regardless of checking the
  // IP address in commonName.
  EXPECT_FALSE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_ca_192_168_1_1), der::Input()));

  std::string name_us_az_ipv6;
  ASSERT_TRUE(LoadTestName("name-us-arizona-ipv6.pem", &name_us_az_ipv6));
  // The subject is within permitted directoryName constraints, so permitted.
  // (The commonName is an ipv6 address which wasn't supported in the past, but
  // since commonName checking is ignored entirely, this is permitted.)
  EXPECT_TRUE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_az_ipv6), der::Input()));
}

TEST_P(ParseNameConstraints, IsPermittedCertFailsOnEmptySubjectAltName) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("dnsname.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  std::string name_us_az;
  ASSERT_TRUE(LoadTestName("name-us-arizona.pem", &name_us_az));

  // No constraints on directoryName type, so name_us_az should be allowed when
  // subjectAltName is not present.
  EXPECT_TRUE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_az), der::Input()));

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-invalid-empty.pem", &san));
  // Should fail if subjectAltName is present but empty.
  EXPECT_FALSE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_az), InputFromString(&san)));
}

TEST_P(ParseNameConstraints, IsPermittedCertFailsOnInvalidIpInSubjectAltName) {
  std::string constraints_der;
  ASSERT_TRUE(LoadTestNameConstraint("ipaddress.pem", &constraints_der));
  scoped_ptr<NameConstraints> name_constraints(NameConstraints::CreateFromDer(
      InputFromString(&constraints_der), is_critical()));
  ASSERT_TRUE(name_constraints);

  std::string name_us_az_192_168_1_1;
  ASSERT_TRUE(
      LoadTestName("name-us-arizona-192.168.1.1.pem", &name_us_az_192_168_1_1));

  // Without the invalid subjectAltName, it passes.
  EXPECT_TRUE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_az_192_168_1_1), der::Input()));

  std::string san;
  ASSERT_TRUE(LoadTestSubjectAltName("san-invalid-ipaddress.pem", &san));
  // Should fail if subjectAltName contains an invalid ip address.
  EXPECT_FALSE(name_constraints->IsPermittedCert(
      SequenceValueFromString(&name_us_az_192_168_1_1), InputFromString(&san)));
}

}  // namespace net
