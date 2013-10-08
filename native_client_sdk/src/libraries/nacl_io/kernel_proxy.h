// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBRARIES_NACL_IO_KERNEL_PROXY_H_
#define LIBRARIES_NACL_IO_KERNEL_PROXY_H_

#include <map>
#include <string>

#include "nacl_io/host_resolver.h"
#include "nacl_io/kernel_object.h"
#include "nacl_io/mount_factory.h"
#include "nacl_io/ossocket.h"
#include "nacl_io/ostypes.h"
#include "nacl_io/osutime.h"

struct timeval;

namespace nacl_io {

class PepperInterface;

// KernelProxy provide one-to-one mapping for libc kernel calls.  Calls to the
// proxy will result in IO access to the provided Mount and MountNode objects.
//
// NOTE: The KernelProxy does not directly take any kernel locks, all locking
// is done by the parent class KernelObject.  Instead, KernelProxy is
// responsible for taking the locks of the KernelHandle, and MountNode objects.
// For this reason, a KernelObject call should not be done while holding
// a handle or node lock.  In addition, to ensure locking order,
// a KernelHandle lock must never be taken after taking the associated
// MountNode's lock.
//
// NOTE: The KernelProxy is the only class that should be setting errno. All
// other classes should return Error (as defined by nacl_io/error.h).
class KernelProxy : protected KernelObject {
 public:
  typedef std::map<std::string, MountFactory*> MountFactoryMap_t;

  KernelProxy();
  virtual ~KernelProxy();

  // Takes ownership of |ppapi|.
  // |ppapi| may be NULL. If so, no mount that uses pepper calls can be mounted.
  virtual void Init(PepperInterface* ppapi);

  // NaCl-only function to read resources specified in the NMF file.
  virtual int open_resource(const char* file);

  // KernelHandle and FD allocation and manipulation functions.
  virtual int open(const char* path, int oflag);
  virtual int close(int fd);
  virtual int dup(int fd);
  virtual int dup2(int fd, int newfd);

  // Path related System calls handled by KernelProxy (not mount-specific)
  virtual int chdir(const char* path);
  virtual char* getcwd(char* buf, size_t size);
  virtual char* getwd(char* buf);
  virtual int mount(const char *source,
                    const char *target,
                    const char *filesystemtype,
                    unsigned long mountflags,
                    const void *data);
  virtual int umount(const char *path);

  // Stub system calls that don't do anything (yet), handled by KernelProxy.
  virtual int chown(const char* path, uid_t owner, gid_t group);
  virtual int fchown(int fd, uid_t owner, gid_t group);
  virtual int lchown(const char* path, uid_t owner, gid_t group);
  virtual int utime(const char* filename, const struct utimbuf* times);

  // System calls that take a path as an argument:
  // The kernel proxy will look for the Node associated to the path. To
  // find the node, the kernel proxy calls the corresponding mount's GetNode()
  // method. The corresponding  method will be called. If the node
  // cannot be found, errno is set and -1 is returned.
  virtual int chmod(const char *path, mode_t mode);
  virtual int mkdir(const char *path, mode_t mode);
  virtual int rmdir(const char *path);
  virtual int stat(const char *path, struct stat *buf);

  // System calls that take a file descriptor as an argument:
  // The kernel proxy will determine to which mount the file
  // descriptor's corresponding file handle belongs.  The
  // associated mount's function will be called.
  virtual ssize_t read(int fd, void *buf, size_t nbyte);
  virtual ssize_t write(int fd, const void *buf, size_t nbyte);

  virtual int fchmod(int fd, int prot);
  virtual int fstat(int fd, struct stat *buf);
  virtual int getdents(int fd, void *buf, unsigned int count);
  virtual int ftruncate(int fd, off_t length);
  virtual int fsync(int fd);
  virtual int isatty(int fd);
  virtual int ioctl(int d, int request, char *argp);

  // lseek() relies on the mount's Stat() to determine whether or not the
  // file handle corresponding to fd is a directory
  virtual off_t lseek(int fd, off_t offset, int whence);

  // remove() uses the mount's GetNode() and Stat() to determine whether or
  // not the path corresponds to a directory or a file.  The mount's Rmdir()
  // or Unlink() is called accordingly.
  virtual int remove(const char* path);
  // unlink() is a simple wrapper around the mount's Unlink function.
  virtual int unlink(const char* path);
  // access() uses the Mount's Stat().
  virtual int access(const char* path, int amode);

  virtual int link(const char* oldpath, const char* newpath);
  virtual int symlink(const char* oldpath, const char* newpath);

  virtual void* mmap(void* addr,
                     size_t length,
                     int prot,
                     int flags,
                     int fd,
                     size_t offset);
  virtual int munmap(void* addr, size_t length);
  virtual int tcflush(int fd, int queue_selector);
  virtual int tcgetattr(int fd, struct termios* termios_p);
  virtual int tcsetattr(int fd, int optional_actions,
                           const struct termios *termios_p);

#ifdef PROVIDES_SOCKET_API
  virtual int select(int nfds, fd_set* readfds, fd_set* writefds,
                    fd_set* exceptfds, struct timeval* timeout);

  virtual int poll(struct pollfd *fds, nfds_t nfds, int timeout);

  // Socket support functions
  virtual int accept(int fd, struct sockaddr* addr, socklen_t* len);
  virtual int bind(int fd, const struct sockaddr* addr, socklen_t len);
  virtual int connect(int fd, const struct sockaddr* addr, socklen_t len);
  virtual struct hostent* gethostbyname(const char* name);
  virtual int getpeername(int fd, struct sockaddr* addr, socklen_t* len);
  virtual int getsockname(int fd, struct sockaddr* addr, socklen_t* len);
  virtual int getsockopt(int fd,
                         int lvl,
                         int optname,
                         void* optval,
                         socklen_t* len);
  virtual int listen(int fd, int backlog);
  virtual ssize_t recv(int fd,
                       void* buf,
                       size_t len,
                       int flags);
  virtual ssize_t recvfrom(int fd,
                           void* buf,
                           size_t len,
                           int flags,
                           struct sockaddr* addr,
                           socklen_t* addrlen);
  virtual ssize_t recvmsg(int fd, struct msghdr* msg, int flags);
  virtual ssize_t send(int fd, const void* buf, size_t len, int flags);
  virtual ssize_t sendto(int fd,
                         const void* buf,
                         size_t len,
                         int flags,
                         const struct sockaddr* addr,
                         socklen_t addrlen);
  virtual ssize_t sendmsg(int fd, const struct msghdr* msg, int flags);
  virtual int setsockopt(int fd,
                         int lvl,
                         int optname,
                         const void* optval,
                         socklen_t len);
  virtual int shutdown(int fd, int how);
  virtual int socket(int domain, int type, int protocol);
  virtual int socketpair(int domain, int type, int protocol, int* sv);
#endif  // PROVIDES_SOCKET_API

 protected:
  MountFactoryMap_t factories_;
  int dev_;
  PepperInterface* ppapi_;
  static KernelProxy *s_instance_;
#ifdef PROVIDES_SOCKET_API
  HostResolver host_resolver_;
#endif

#ifdef PROVIDES_SOCKET_API
  virtual int AcquireSocketHandle(int fd, ScopedKernelHandle* handle);
#endif

  DISALLOW_COPY_AND_ASSIGN(KernelProxy);
};

}  // namespace nacl_io

#endif  // LIBRARIES_NACL_IO_KERNEL_PROXY_H_
