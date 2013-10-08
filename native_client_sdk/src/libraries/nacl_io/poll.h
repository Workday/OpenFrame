/* Copyright (c) 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. */

#ifndef LIBRARIES_NACL_IO_POLL_H_
#define LIBRARIES_NACL_IO_POLL_H_

#include <stdint.h>

#include "sdk_util/macros.h"

EXTERN_C_BEGIN

/* This header adds definitions of flags and structures for use with poll on
 * toolchains with 'C' libraries which do not normally supply poll. */

/* Node state flags */
#define POLLIN   0x0001   /* Will not block READ select/poll. */
#define POLLOUT  0x0002   /* Will not block WRITE select/poll. */
#define POLLERR  0x0008   /* Will not block EXECPT select/poll. */
#define POLLHUP  0x0010   /* Connection closed on far side. */
#define POLLNVAL 0x0020   /* Invalid FD. */

/* Number of file descriptors. */
typedef int nfds_t;

struct pollfd {
  int fd;
  uint16_t events;
  uint16_t revents;
};

int poll (struct pollfd *__fds, nfds_t __nfds, int __timeout);

EXTERN_C_END

#endif  /* LIBRARIES_NACL_IO_POLL_H_ */

