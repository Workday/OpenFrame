// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBRARIES_NACL_IO_KERNEL_INTERCEPT_H_
#define LIBRARIES_NACL_IO_KERNEL_INTERCEPT_H_

#include <ppapi/c/ppb.h>
#include <ppapi/c/pp_instance.h>

#include "nacl_io/ossocket.h"
#include "nacl_io/osstat.h"
#include "nacl_io/ostermios.h"
#include "nacl_io/ostypes.h"
#include "nacl_io/osutime.h"
#include "sdk_util/macros.h"

EXTERN_C_BEGIN

// The kernel intercept module provides a C->C++ thunk between the libc
// kernel calls and the KernelProxy singleton.

// ki_init must be called with an uninitialized KernelProxy object.  Calling
// with NULL will instantiate a default kernel proxy object.  ki_init must
// be called before any other ki_XXX function can be used.
void ki_init(void* kernel_proxy);
void ki_init_ppapi(void* kernel_proxy,
                   PP_Instance instance,
                   PPB_GetInterface get_browser_interface);
int ki_is_initialized();
void ki_uninit();

int ki_chdir(const char* path);
char* ki_getcwd(char* buf, size_t size);
char* ki_getwd(char* buf);
int ki_dup(int oldfd);
int ki_dup2(int oldfd, int newfd);
int ki_chmod(const char* path, mode_t mode);
int ki_stat(const char* path, struct stat* buf);
int ki_mkdir(const char* path, mode_t mode);
int ki_rmdir(const char* path);
int ki_mount(const char* source, const char* target, const char* filesystemtype,
             unsigned long mountflags, const void *data);
int ki_umount(const char* path);
int ki_open(const char* path, int oflag);
ssize_t ki_read(int fd, void* buf, size_t nbyte);
ssize_t ki_write(int fd, const void* buf, size_t nbyte);
int ki_fstat(int fd, struct stat *buf);
int ki_getdents(int fd, void* buf, unsigned int count);
int ki_ftruncate(int fd, off_t length);
int ki_fsync(int fd);
int ki_isatty(int fd);
int ki_close(int fd);
off_t ki_lseek(int fd, off_t offset, int whence);
int ki_remove(const char* path);
int ki_unlink(const char* path);
int ki_access(const char* path, int amode);
int ki_link(const char* oldpath, const char* newpath);
int ki_symlink(const char* oldpath, const char* newpath);
void* ki_mmap(void* addr, size_t length, int prot, int flags, int fd,
              off_t offset);
int ki_munmap(void* addr, size_t length);
int ki_open_resource(const char* file);
int ki_ioctl(int d, int request, char* argp);
int ki_chown(const char* path, uid_t owner, gid_t group);
int ki_fchown(int fd, uid_t owner, gid_t group);
int ki_lchown(const char* path, uid_t owner, gid_t group);
int ki_utime(const char* filename, const struct utimbuf* times);

int ki_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int ki_select(int nfds, fd_set* readfds, fd_set* writefds,
              fd_set* exceptfds, struct timeval* timeout);

int ki_tcflush(int fd, int queue_selector);
int ki_tcgetattr(int fd, struct termios* termios_p);
int ki_tcsetattr(int fd, int optional_actions,
                 const struct termios *termios_p);

#ifdef PROVIDES_SOCKET_API
// Socket Functions
int ki_accept(int fd, struct sockaddr* addr, socklen_t* len);
int ki_bind(int fd, const struct sockaddr* addr, socklen_t len);
int ki_connect(int fd, const struct sockaddr* addr, socklen_t len);
struct hostent* ki_gethostbyname(const char* name);
int ki_getpeername(int fd, struct sockaddr* addr, socklen_t* len);
int ki_getsockname(int fd, struct sockaddr* addr, socklen_t* len);
int ki_getsockopt(int fd, int lvl, int optname, void* optval, socklen_t* len);
int ki_listen(int fd, int backlog);
ssize_t ki_recv(int fd, void* buf, size_t len, int flags);
ssize_t ki_recvfrom(int fd, void* buf, size_t len, int flags,
                    struct sockaddr* addr, socklen_t* addrlen);
ssize_t ki_recvmsg(int fd, struct msghdr* msg, int flags);
ssize_t ki_send(int fd, const void* buf, size_t len, int flags);
ssize_t ki_sendto(int fd, const void* buf, size_t len, int flags,
                  const struct sockaddr* addr, socklen_t addrlen);
ssize_t ki_sendmsg(int fd, const struct msghdr* msg, int flags);
int ki_setsockopt(int fd, int lvl, int optname, const void* optval,
                  socklen_t len);
int ki_shutdown(int fd, int how);
int ki_socket(int domain, int type, int protocol);
int ki_socketpair(int domain, int type, int protocl, int* sv);
#endif  // PROVIDES_SOCKET_API

EXTERN_C_END

#endif  // LIBRARIES_NACL_IO_KERNEL_INTERCEPT_H_
