// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/fetch/http_server.h"

HttpServer::HttpServer(std::string ip, int port)
    : session_(new HttpSession(ip, port)) {
}

HttpServer::~HttpServer() {
}
