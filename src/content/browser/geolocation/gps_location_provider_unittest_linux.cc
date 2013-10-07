// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/browser_thread_impl.h"
#include "content/browser/geolocation/gps_location_provider_linux.h"
#include "testing/gtest/include/gtest/gtest.h"

#include "base/bind.h"

namespace content {

class MockLibGps : public LibGps {
 public:
  MockLibGps();
  virtual ~MockLibGps();

  virtual bool GetPositionIfFixed(Geoposition* position) OVERRIDE {
    CHECK(position);
    ++get_position_calls_;
    *position = get_position_;
    return get_position_ret_;
  }

  static int gps_open_stub(const char*, const char*, struct gps_data_t*) {
    CHECK(g_instance_);
    g_instance_->gps_open_calls_++;
    return g_instance_->gps_open_ret_;
  }

  static int gps_close_stub(struct gps_data_t*) {
    return 0;
  }

  static int gps_read_stub(struct gps_data_t*) {
    CHECK(g_instance_);
    g_instance_->gps_read_calls_++;
    return g_instance_->gps_read_ret_;
  }

  int get_position_calls_;
  bool get_position_ret_;
  int gps_open_calls_;
  int gps_open_ret_;
  int gps_read_calls_;
  int gps_read_ret_;
  Geoposition get_position_;
  static MockLibGps* g_instance_;
};

class GeolocationGpsProviderLinuxTests : public testing::Test {
 public:
  GeolocationGpsProviderLinuxTests();
  virtual ~GeolocationGpsProviderLinuxTests();

  static LibGps* NewMockLibGps() {
    return new MockLibGps();
  }
  static LibGps* NoLibGpsFactory() {
    return NULL;
  }

 protected:
  base::MessageLoop message_loop_;
  BrowserThreadImpl ui_thread_;
  scoped_ptr<LocationProvider> provider_;
};

void CheckValidPosition(const Geoposition& expected,
                        const Geoposition& actual) {
  EXPECT_TRUE(actual.Validate());
  EXPECT_DOUBLE_EQ(expected.latitude, actual.latitude);
  EXPECT_DOUBLE_EQ(expected.longitude, actual.longitude);
  EXPECT_DOUBLE_EQ(expected.accuracy, actual.accuracy);
}

void QuitMessageLoopAfterUpdate(const LocationProvider* provider,
                                const Geoposition& position) {
  base::MessageLoop::current()->Quit();
}

MockLibGps* MockLibGps::g_instance_ = NULL;

MockLibGps::MockLibGps()
    : get_position_calls_(0),
      get_position_ret_(true),
      gps_open_calls_(0),
      gps_open_ret_(0),
      gps_read_calls_(0),
      gps_read_ret_(0) {
  get_position_.error_code = Geoposition::ERROR_CODE_POSITION_UNAVAILABLE;
  EXPECT_FALSE(g_instance_);
  g_instance_ = this;
#if defined(USE_LIBGPS)
  libgps_loader_.gps_open = gps_open_stub;
  libgps_loader_.gps_close = gps_close_stub;
  libgps_loader_.gps_read = gps_read_stub;
#endif  // defined(USE_LIBGPS)
}

MockLibGps::~MockLibGps() {
  EXPECT_EQ(this, g_instance_);
  g_instance_ = NULL;
}

GeolocationGpsProviderLinuxTests::GeolocationGpsProviderLinuxTests()
    : ui_thread_(BrowserThread::IO, &message_loop_),
      provider_(new GpsLocationProviderLinux(NewMockLibGps)) {
  provider_->SetUpdateCallback(base::Bind(&QuitMessageLoopAfterUpdate));
}

GeolocationGpsProviderLinuxTests::~GeolocationGpsProviderLinuxTests() {
}

TEST_F(GeolocationGpsProviderLinuxTests, NoLibGpsInstalled) {
  provider_.reset(new GpsLocationProviderLinux(NoLibGpsFactory));
  ASSERT_TRUE(provider_.get());
  const bool ok = provider_->StartProvider(true);
  EXPECT_FALSE(ok);
  Geoposition position;
  provider_->GetPosition(&position);
  EXPECT_FALSE(position.Validate());
  EXPECT_EQ(Geoposition::ERROR_CODE_POSITION_UNAVAILABLE, position.error_code);
}

#if defined(OS_CHROMEOS)

TEST_F(GeolocationGpsProviderLinuxTests, GetPosition) {
  ASSERT_TRUE(provider_.get());
  const bool ok = provider_->StartProvider(true);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(MockLibGps::g_instance_);
  EXPECT_EQ(0, MockLibGps::g_instance_->get_position_calls_);
  EXPECT_EQ(0, MockLibGps::g_instance_->gps_open_calls_);
  EXPECT_EQ(0, MockLibGps::g_instance_->gps_read_calls_);
  Geoposition position;
  provider_->GetPosition(&position);
  EXPECT_FALSE(position.Validate());
  EXPECT_EQ(Geoposition::ERROR_CODE_POSITION_UNAVAILABLE, position.error_code);
  MockLibGps::g_instance_->get_position_.error_code =
      Geoposition::ERROR_CODE_NONE;
  MockLibGps::g_instance_->get_position_.latitude = 4.5;
  MockLibGps::g_instance_->get_position_.longitude = -34.1;
  MockLibGps::g_instance_->get_position_.accuracy = 345;
  MockLibGps::g_instance_->get_position_.timestamp =
      base::Time::FromDoubleT(200);
  EXPECT_TRUE(MockLibGps::g_instance_->get_position_.Validate());
  base::MessageLoop::current()->Run();
  EXPECT_EQ(1, MockLibGps::g_instance_->get_position_calls_);
  EXPECT_EQ(1, MockLibGps::g_instance_->gps_open_calls_);
  EXPECT_EQ(1, MockLibGps::g_instance_->gps_read_calls_);
  provider_->GetPosition(&position);
  CheckValidPosition(MockLibGps::g_instance_->get_position_, position);

  // Movement. This will block for up to half a second.
  MockLibGps::g_instance_->get_position_.latitude += 0.01;
  base::MessageLoop::current()->Run();
  provider_->GetPosition(&position);
  EXPECT_EQ(2, MockLibGps::g_instance_->get_position_calls_);
  EXPECT_EQ(1, MockLibGps::g_instance_->gps_open_calls_);
  EXPECT_EQ(2, MockLibGps::g_instance_->gps_read_calls_);
  CheckValidPosition(MockLibGps::g_instance_->get_position_, position);
}

void EnableGpsOpenCallback() {
  CHECK(MockLibGps::g_instance_);
  MockLibGps::g_instance_->gps_open_ret_ = 0;
}

TEST_F(GeolocationGpsProviderLinuxTests, LibGpsReconnect) {
  // Setup gpsd reconnect interval to be 1000ms to speed up test.
  GpsLocationProviderLinux* gps_provider =
      static_cast<GpsLocationProviderLinux*>(provider_.get());
  gps_provider->SetGpsdReconnectIntervalMillis(1000);
  gps_provider->SetPollPeriodMovingMillis(200);
  const bool ok = provider_->StartProvider(true);
  EXPECT_TRUE(ok);
  ASSERT_TRUE(MockLibGps::g_instance_);
  // Let gps_open() fails, and so will LibGps::Start().
  // Reconnect will happen in 1000ms.
  MockLibGps::g_instance_->gps_open_ret_ = 1;
  Geoposition position;
  MockLibGps::g_instance_->get_position_.error_code =
      Geoposition::ERROR_CODE_NONE;
  MockLibGps::g_instance_->get_position_.latitude = 4.5;
  MockLibGps::g_instance_->get_position_.longitude = -34.1;
  MockLibGps::g_instance_->get_position_.accuracy = 345;
  MockLibGps::g_instance_->get_position_.timestamp =
      base::Time::FromDoubleT(200);
  EXPECT_TRUE(MockLibGps::g_instance_->get_position_.Validate());
  // This task makes gps_open() and LibGps::Start() to succeed after
  // 1500ms.
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&EnableGpsOpenCallback),
      base::TimeDelta::FromMilliseconds(1500));
  base::MessageLoop::current()->Run();
  provider_->GetPosition(&position);
  EXPECT_TRUE(position.Validate());
  // 3 gps_open() calls are expected (2 failures and 1 success)
  EXPECT_EQ(1, MockLibGps::g_instance_->get_position_calls_);
  EXPECT_EQ(3, MockLibGps::g_instance_->gps_open_calls_);
  EXPECT_EQ(1, MockLibGps::g_instance_->gps_read_calls_);
}

#endif  // #if defined(OS_CHROMEOS)

}  // namespace content
