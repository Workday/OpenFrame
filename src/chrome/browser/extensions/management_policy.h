// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_MANAGEMENT_POLICY_H_
#define CHROME_BROWSER_EXTENSIONS_MANAGEMENT_POLICY_H_

#include <set>
#include <string>

#include "base/basictypes.h"
#include "chrome/common/extensions/extension.h"

namespace extensions {

// This class registers providers that want to prohibit certain actions from
// being applied to extensions. It must be called, via the ExtensionService,
// before allowing a user or a user-level mechanism to perform the respective
// action. (That is, installing or otherwise modifying an extension in order
// to conform to enterprise administrator policy must be exempted from these
// checks.)
//
// This "policy" and its providers should not be confused with administrator
// policy, although admin policy is one of the sources ("Providers") of
// restrictions registered with and exposed by the ManagementPolicy.
class ManagementPolicy {
 public:
  // Each mechanism that wishes to limit users' ability to control extensions,
  // whether one individual extension or the whole system, should implement
  // the methods of this Provider interface that it needs. In each case, if the
  // provider does not need to control a certain action, that method does not
  // need to be implemented.
  //
  // It is not guaranteed that a particular Provider's methods will be called
  // each time a user tries to perform one of the controlled actions (the list
  // of providers is short-circuited as soon as a decision is possible), so
  // implementations of these methods must have no side effects.
  //
  // For all of the Provider methods below, if |error| is not NULL and the
  // method imposes a restriction on the desired action, |error| may be set
  // to an applicable error message, but this is not required.
  class Provider {
   public:
    Provider() {}
    virtual ~Provider() {}

    // A human-readable name for this provider, for use in debug messages.
    // Implementers should return an empty string in non-debug builds, to save
    // executable size.
    virtual std::string GetDebugPolicyProviderName() const = 0;

    // Providers should return false if a user may not install the |extension|,
    // or load or run it if it has already been installed.
    virtual bool UserMayLoad(const Extension* extension,
                             string16* error) const;

    // Providers should return false if a user may not enable, disable, or
    // uninstall the |extension|, or change its usage options (incognito
    // permission, file access, etc.).
    virtual bool UserMayModifySettings(const Extension* extension,
                                       string16* error) const;

    // Providers should return true if the |extension| must always remain
    // enabled. This is distinct from UserMayModifySettings() in that the latter
    // also prohibits enabling the extension if it is currently disabled.
    // Providers implementing this method should also implement the others
    // above, if they wish to completely lock in an extension.
    virtual bool MustRemainEnabled(const Extension* extension,
                                   string16* error) const;

   private:
    DISALLOW_COPY_AND_ASSIGN(Provider);
  };

  ManagementPolicy();
  ~ManagementPolicy();

  // Registers or unregisters a provider, causing it to be added to or removed
  // from the list of providers queried. Ownership of the provider remains with
  // the caller. Providers do not need to be unregistered on shutdown.
  void RegisterProvider(Provider* provider);
  void UnregisterProvider(Provider* provider);

  // Returns true if the user is permitted to install, load, and run the given
  // extension. If not, |error| may be set to an appropriate message.
  bool UserMayLoad(const Extension* extension, string16* error) const;

  // Returns true if the user is permitted to enable, disable, or uninstall the
  // given extension, or change the extension's usage options (incognito mode,
  // file access, etc.). If not, |error| may be set to an appropriate message.
  bool UserMayModifySettings(const Extension* extension,
                             string16* error) const;

  // Returns true if the extension must remain enabled at all times (e.g. a
  // compoment extension). In that case, |error| may be set to an appropriate
  // message.
  bool MustRemainEnabled(const Extension* extension,
                         string16* error) const;

  // For use in testing.
  void UnregisterAllProviders();
  int GetNumProviders() const;

 private:
  typedef std::set<Provider*> ProviderList;
  ProviderList providers_;

  DISALLOW_COPY_AND_ASSIGN(ManagementPolicy);
};

}  // namespace

#endif  // CHROME_BROWSER_EXTENSIONS_MANAGEMENT_POLICY_H_
