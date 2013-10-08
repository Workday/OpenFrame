// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/host_zoom_map_impl.h"

#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

class HostZoomMapTest : public testing::Test {
 public:
  HostZoomMapTest() : ui_thread_(BrowserThread::UI, &message_loop_) {
  }

 protected:
  base::MessageLoop message_loop_;
  TestBrowserThread ui_thread_;
};

TEST_F(HostZoomMapTest, GetSetZoomLevel) {
  HostZoomMapImpl host_zoom_map;

  double zoomed = 2.5;
  host_zoom_map.SetZoomLevelForHost("zoomed.com", zoomed);

  EXPECT_DOUBLE_EQ(0,
      host_zoom_map.GetZoomLevelForHostAndScheme("http", "normal.com"));
  EXPECT_DOUBLE_EQ(zoomed,
      host_zoom_map.GetZoomLevelForHostAndScheme("http", "zoomed.com"));
}

TEST_F(HostZoomMapTest, GetSetZoomLevelWithScheme) {
  HostZoomMapImpl host_zoom_map;

  double zoomed = 2.5;
  double default_zoom = 1.5;

  host_zoom_map.SetZoomLevelForHostAndScheme("chrome", "login", 0);

  host_zoom_map.SetDefaultZoomLevel(default_zoom);

  EXPECT_DOUBLE_EQ(0,
      host_zoom_map.GetZoomLevelForHostAndScheme("chrome", "login"));
  EXPECT_DOUBLE_EQ(default_zoom,
      host_zoom_map.GetZoomLevelForHostAndScheme("http", "login"));

  host_zoom_map.SetZoomLevelForHost("login", zoomed);

  EXPECT_DOUBLE_EQ(0,
      host_zoom_map.GetZoomLevelForHostAndScheme("chrome", "login"));
  EXPECT_DOUBLE_EQ(zoomed,
      host_zoom_map.GetZoomLevelForHostAndScheme("http", "login"));
}

}  // namespace content
