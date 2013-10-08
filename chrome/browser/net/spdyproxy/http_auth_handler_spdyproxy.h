// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NET_SPDYPROXY_HTTP_AUTH_HANDLER_SPDYPROXY_H_
#define CHROME_BROWSER_NET_SPDYPROXY_HTTP_AUTH_HANDLER_SPDYPROXY_H_

#include <string>

#include "base/gtest_prod_util.h"
#include "net/http/http_auth_handler.h"
#include "net/http/http_auth_handler_factory.h"

namespace spdyproxy {

// Code for handling http SpdyProxy authentication.
class HttpAuthHandlerSpdyProxy : public net::HttpAuthHandler {
 public:
  class Factory : public net::HttpAuthHandlerFactory {
   public:
    // Constructs a new spdyproxy handler factory which mints handlers that
    // respond to challenges only from the given |authorized_spdyproxy_origin|.
    explicit Factory(const GURL& authorized_spdyproxy_origin);
    virtual ~Factory();

    virtual int CreateAuthHandler(
        net::HttpAuth::ChallengeTokenizer* challenge,
        net::HttpAuth::Target target,
        const GURL& origin,
        CreateReason reason,
        int digest_nonce_count,
        const net::BoundNetLog& net_log,
        scoped_ptr<HttpAuthHandler>* handler) OVERRIDE;

   private:
    // The origin for which we will respond to SpdyProxy auth challenges.
    GURL authorized_spdyproxy_origin_;
  };

  // Constructs a new spdyproxy handler which responds to challenges
  // from the given |authorized_spdyproxy_origin|.
  explicit HttpAuthHandlerSpdyProxy(
      const GURL& authorized_spdyproxy_origin);

  // Overrides from net::HttpAuthHandler.
  virtual net::HttpAuth::AuthorizationResult HandleAnotherChallenge(
      net::HttpAuth::ChallengeTokenizer* challenge) OVERRIDE;
  virtual bool NeedsIdentity() OVERRIDE;
  virtual bool AllowsDefaultCredentials() OVERRIDE;
  virtual bool AllowsExplicitCredentials() OVERRIDE;

 private:
  FRIEND_TEST_ALL_PREFIXES(HttpAuthHandlerSpdyProxyTest, ParseChallenge);

  virtual ~HttpAuthHandlerSpdyProxy() {}

  virtual bool Init(net::HttpAuth::ChallengeTokenizer* challenge) OVERRIDE;

  virtual int GenerateAuthTokenImpl(const net::AuthCredentials* credentials,
                                    const net::HttpRequestInfo* request,
                                    const net::CompletionCallback& callback,
                                    std::string* auth_token) OVERRIDE;

  bool ParseChallenge(net::HttpAuth::ChallengeTokenizer* challenge);

  bool ParseChallengeProperty(const std::string& name,
                              const std::string& value);

  // The origin for which we will respond to SpdyProxy auth challenges.
  GURL authorized_spdyproxy_origin_;

  // The ps token, encoded as UTF-8.
  std::string ps_token_;

  DISALLOW_COPY_AND_ASSIGN(HttpAuthHandlerSpdyProxy);
};

}  // namespace spdyproxy

#endif  // CHROME_BROWSER_NET_SPDYPROXY_HTTP_AUTH_HANDLER_SPDYPROXY_H_
