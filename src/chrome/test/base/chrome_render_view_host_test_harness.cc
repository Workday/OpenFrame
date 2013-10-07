// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/base/chrome_render_view_host_test_harness.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/signin/fake_signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/test/base/testing_profile.h"

#if defined(USE_ASH)
#include "ash/shell.h"
#endif

#if defined(USE_AURA)
#include "ui/aura/env.h"
#include "ui/aura/root_window.h"
#endif

using content::RenderViewHostTester;
using content::RenderViewHostTestHarness;

ChromeRenderViewHostTestHarness::ChromeRenderViewHostTestHarness() {
}

ChromeRenderViewHostTestHarness::~ChromeRenderViewHostTestHarness() {
}

TestingProfile* ChromeRenderViewHostTestHarness::profile() {
  return static_cast<TestingProfile*>(browser_context());
}

RenderViewHostTester* ChromeRenderViewHostTestHarness::rvh_tester() {
  return RenderViewHostTester::For(rvh());
}

static BrowserContextKeyedService* BuildSigninManagerFake(
    content::BrowserContext* context) {
  Profile* profile = static_cast<Profile*>(context);
#if defined (OS_CHROMEOS)
  SigninManagerBase* signin = new SigninManagerBase();
  signin->Initialize(profile, NULL);
  return signin;
#else
  FakeSigninManager* manager = new FakeSigninManager(profile);
  manager->Initialize(profile, g_browser_process->local_state());
  return manager;
#endif
}

void ChromeRenderViewHostTestHarness::SetUp() {
  RenderViewHostTestHarness::SetUp();
  SigninManagerFactory::GetInstance()->SetTestingFactory(
          profile(), BuildSigninManagerFake);
}

void ChromeRenderViewHostTestHarness::TearDown() {
  RenderViewHostTestHarness::TearDown();
#if defined(USE_ASH)
  ash::Shell::DeleteInstance();
#endif
#if defined(USE_AURA)
  aura::Env::DeleteInstance();
#endif
}

content::BrowserContext*
ChromeRenderViewHostTestHarness::CreateBrowserContext() {
  return new TestingProfile();
}
