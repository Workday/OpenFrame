/* Copyright 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. */

#ifndef LIBRARIES_NACL_IO_OSSOCKET_H_
#define LIBRARIES_NACL_IO_OSSOCKET_H_

#include <sys/types.h>

#if defined(__native_client__)
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/select.h>
#define PROVIDES_SOCKET_API
#endif

#endif  /* LIBRARIES_NACL_IO_OSSOCKET_H_ */
