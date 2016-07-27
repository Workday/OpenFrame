// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DATA_REDUCTION_PROXY_CORE_BROWSER_LOFI_DECIDER_H_
#define COMPONENTS_DATA_REDUCTION_PROXY_CORE_BROWSER_LOFI_DECIDER_H_

#include "base/macros.h"
#include "net/url_request/url_request.h"

namespace net {
class HttpRequestHeaders;
}

namespace data_reduction_proxy {

// Interface to determine if a request should be made for a low fidelity version
// of the resource.
class LoFiDecider {
 public:
  virtual ~LoFiDecider() {}

  // Returns true when Lo-Fi mode is on for the given |request|. This means the
  // Lo-Fi header should be added to the given request.
  virtual bool IsUsingLoFiMode(const net::URLRequest& request) const = 0;

  // Returns true when Lo-Fi mode is on for the given |request|. If the
  // |request| is using Lo-Fi mode, adds the "q=low" directive to the |headers|.
  // If the |request| is using Lo-Fi preview mode, and it is a main frame
  // request adds the "q=preview" and it is a main frame request directive to
  // the |headers|. If the user is in the experiment control group and Lo-Fi is
  // on, adds the experiment directive "exp=lofi_active_control".
  virtual bool MaybeAddLoFiDirectiveToHeaders(
      const net::URLRequest& request,
      net::HttpRequestHeaders* headers) const = 0;
};

}  // namespace data_reduction_proxy

#endif  // COMPONENTS_DATA_REDUCTION_PROXY_CORE_BROWSER_LOFI_DECIDER_H_
