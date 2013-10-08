// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_CLIENT_CONFIG_H_
#define REMOTING_CLIENT_CLIENT_CONFIG_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "remoting/protocol/authentication_method.h"
#include "remoting/protocol/negotiating_client_authenticator.h"

namespace remoting {

struct ClientConfig {
  ClientConfig();
  ~ClientConfig();

  std::string local_jid;

  std::string host_jid;
  std::string host_public_key;

  protocol::FetchSecretCallback fetch_secret_callback;

  std::vector<protocol::AuthenticationMethod> authentication_methods;
  std::string authentication_tag;

  // The set of all capabilities supported by the webapp.
  std::string capabilities;

  // The host-generated id and secret for paired clients. Paired clients
  // should set both of these in addition to fetch_secret_callback; the
  // latter is used if the paired connection fails (for example, if the
  // pairing has been revoked by the host) and the user needs to prompted
  // to enter their PIN.
  std::string client_pairing_id;
  std::string client_paired_secret;
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_CLIENT_CONFIG_H_
