// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/tcp_client_socket.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#if defined(OS_POSIX)
#include <netinet/in.h>
#endif

#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/metrics/stats_counters.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_util.h"
#include "net/base/connection_type_histograms.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/net_log.h"
#include "net/base/net_util.h"
#include "net/base/network_change_notifier.h"
#include "net/socket/socket_net_log_params.h"

// If we don't have a definition for TCPI_OPT_SYN_DATA, create one.
#ifndef TCPI_OPT_SYN_DATA
#define TCPI_OPT_SYN_DATA 32
#endif

namespace net {

namespace {

const int kInvalidSocket = -1;
const int kTCPKeepAliveSeconds = 45;

// SetTCPNoDelay turns on/off buffering in the kernel. By default, TCP sockets
// will wait up to 200ms for more data to complete a packet before transmitting.
// After calling this function, the kernel will not wait. See TCP_NODELAY in
// `man 7 tcp`.
bool SetTCPNoDelay(int fd, bool no_delay) {
  int on = no_delay ? 1 : 0;
  int error = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on,
      sizeof(on));
  return error == 0;
}

// SetTCPKeepAlive sets SO_KEEPALIVE.
bool  SetTCPKeepAlive(int fd, bool enable, int delay) {
  int on = enable ? 1 : 0;
  if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on))) {
    PLOG(ERROR) << "Failed to set SO_KEEPALIVE on fd: " << fd;
    return false;
  }
#if defined(OS_LINUX) || defined(OS_ANDROID)
  // Set seconds until first TCP keep alive.
  if (setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &delay, sizeof(delay))) {
    PLOG(ERROR) << "Failed to set TCP_KEEPIDLE on fd: " << fd;
    return false;
  }
  // Set seconds between TCP keep alives.
  if (setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &delay, sizeof(delay))) {
    PLOG(ERROR) << "Failed to set TCP_KEEPINTVL on fd: " << fd;
    return false;
  }
#endif
  return true;
}

// Sets socket parameters. Returns the OS error code (or 0 on
// success).
int SetupSocket(int socket) {
  if (SetNonBlocking(socket))
    return errno;

  // This mirrors the behaviour on Windows. See the comment in
  // tcp_client_socket_win.cc after searching for "NODELAY".
  SetTCPNoDelay(socket, true);  // If SetTCPNoDelay fails, we don't care.
  SetTCPKeepAlive(socket, true, kTCPKeepAliveSeconds);

  return 0;
}

// Creates a new socket and sets default parameters for it. Returns
// the OS error code (or 0 on success).
int CreateSocket(int family, int* socket) {
  *socket = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
  if (*socket == kInvalidSocket)
    return errno;
  int error = SetupSocket(*socket);
  if (error) {
    if (HANDLE_EINTR(close(*socket)) < 0)
      PLOG(ERROR) << "close";
    *socket = kInvalidSocket;
    return error;
  }
  return 0;
}

int MapConnectError(int os_error) {
  switch (os_error) {
    case EACCES:
      return ERR_NETWORK_ACCESS_DENIED;
    case ETIMEDOUT:
      return ERR_CONNECTION_TIMED_OUT;
    default: {
      int net_error = MapSystemError(os_error);
      if (net_error == ERR_FAILED)
        return ERR_CONNECTION_FAILED;  // More specific than ERR_FAILED.

      // Give a more specific error when the user is offline.
      if (net_error == ERR_ADDRESS_UNREACHABLE &&
          NetworkChangeNotifier::IsOffline()) {
        return ERR_INTERNET_DISCONNECTED;
      }
      return net_error;
    }
  }
}

}  // namespace

//-----------------------------------------------------------------------------

TCPClientSocketLibevent::TCPClientSocketLibevent(
    const AddressList& addresses,
    net::NetLog* net_log,
    const net::NetLog::Source& source)
    : socket_(kInvalidSocket),
      bound_socket_(kInvalidSocket),
      addresses_(addresses),
      current_address_index_(-1),
      read_watcher_(this),
      write_watcher_(this),
      next_connect_state_(CONNECT_STATE_NONE),
      connect_os_error_(0),
      net_log_(BoundNetLog::Make(net_log, NetLog::SOURCE_SOCKET)),
      previously_disconnected_(false),
      use_tcp_fastopen_(IsTCPFastOpenEnabled()),
      tcp_fastopen_connected_(false),
      fast_open_status_(FAST_OPEN_STATUS_UNKNOWN) {
  net_log_.BeginEvent(NetLog::TYPE_SOCKET_ALIVE,
                      source.ToEventParametersCallback());
}

TCPClientSocketLibevent::~TCPClientSocketLibevent() {
  Disconnect();
  net_log_.EndEvent(NetLog::TYPE_SOCKET_ALIVE);
  if (tcp_fastopen_connected_) {
    UMA_HISTOGRAM_ENUMERATION("Net.TcpFastOpenSocketConnection",
                              fast_open_status_, FAST_OPEN_MAX_VALUE);
  }
}

int TCPClientSocketLibevent::AdoptSocket(int socket) {
  DCHECK_EQ(socket_, kInvalidSocket);

  int error = SetupSocket(socket);
  if (error)
    return MapSystemError(error);

  socket_ = socket;

  // This is to make GetPeerAddress() work. It's up to the caller ensure
  // that |address_| contains a reasonable address for this
  // socket. (i.e. at least match IPv4 vs IPv6!).
  current_address_index_ = 0;
  use_history_.set_was_ever_connected();

  return OK;
}

int TCPClientSocketLibevent::Bind(const IPEndPoint& address) {
  if (current_address_index_ >= 0 || bind_address_.get()) {
    // Cannot bind the socket if we are already bound connected or
    // connecting.
    return ERR_UNEXPECTED;
  }

  SockaddrStorage storage;
  if (!address.ToSockAddr(storage.addr, &storage.addr_len))
    return ERR_INVALID_ARGUMENT;

  // Create |bound_socket_| and try to bind it to |address|.
  int error = CreateSocket(address.GetSockAddrFamily(), &bound_socket_);
  if (error)
    return MapSystemError(error);

  if (HANDLE_EINTR(bind(bound_socket_, storage.addr, storage.addr_len))) {
    error = errno;
    if (HANDLE_EINTR(close(bound_socket_)) < 0)
      PLOG(ERROR) << "close";
    bound_socket_ = kInvalidSocket;
    return MapSystemError(error);
  }

  bind_address_.reset(new IPEndPoint(address));

  return 0;
}

int TCPClientSocketLibevent::Connect(const CompletionCallback& callback) {
  DCHECK(CalledOnValidThread());

  // If already connected, then just return OK.
  if (socket_ != kInvalidSocket)
    return OK;

  base::StatsCounter connects("tcp.connect");
  connects.Increment();

  DCHECK(!waiting_connect());

  net_log_.BeginEvent(NetLog::TYPE_TCP_CONNECT,
                      addresses_.CreateNetLogCallback());

  // We will try to connect to each address in addresses_. Start with the
  // first one in the list.
  next_connect_state_ = CONNECT_STATE_CONNECT;
  current_address_index_ = 0;

  int rv = DoConnectLoop(OK);
  if (rv == ERR_IO_PENDING) {
    // Synchronous operation not supported.
    DCHECK(!callback.is_null());
    write_callback_ = callback;
  } else {
    LogConnectCompletion(rv);
  }

  return rv;
}

int TCPClientSocketLibevent::DoConnectLoop(int result) {
  DCHECK_NE(next_connect_state_, CONNECT_STATE_NONE);

  int rv = result;
  do {
    ConnectState state = next_connect_state_;
    next_connect_state_ = CONNECT_STATE_NONE;
    switch (state) {
      case CONNECT_STATE_CONNECT:
        DCHECK_EQ(OK, rv);
        rv = DoConnect();
        break;
      case CONNECT_STATE_CONNECT_COMPLETE:
        rv = DoConnectComplete(rv);
        break;
      default:
        LOG(DFATAL) << "bad state";
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_connect_state_ != CONNECT_STATE_NONE);

  return rv;
}

int TCPClientSocketLibevent::DoConnect() {
  DCHECK_GE(current_address_index_, 0);
  DCHECK_LT(current_address_index_, static_cast<int>(addresses_.size()));
  DCHECK_EQ(0, connect_os_error_);

  const IPEndPoint& endpoint = addresses_[current_address_index_];

  if (previously_disconnected_) {
    use_history_.Reset();
    previously_disconnected_ = false;
  }

  net_log_.BeginEvent(NetLog::TYPE_TCP_CONNECT_ATTEMPT,
                      CreateNetLogIPEndPointCallback(&endpoint));

  next_connect_state_ = CONNECT_STATE_CONNECT_COMPLETE;

  if (bound_socket_ != kInvalidSocket) {
    DCHECK(bind_address_.get());
    socket_ = bound_socket_;
    bound_socket_ = kInvalidSocket;
  } else {
    // Create a non-blocking socket.
    connect_os_error_ = CreateSocket(endpoint.GetSockAddrFamily(), &socket_);
    if (connect_os_error_)
      return MapSystemError(connect_os_error_);

    if (bind_address_.get()) {
      SockaddrStorage storage;
      if (!bind_address_->ToSockAddr(storage.addr, &storage.addr_len))
        return ERR_INVALID_ARGUMENT;
      if (HANDLE_EINTR(bind(socket_, storage.addr, storage.addr_len)))
        return MapSystemError(errno);
    }
  }

  // Connect the socket.
  if (!use_tcp_fastopen_) {
    SockaddrStorage storage;
    if (!endpoint.ToSockAddr(storage.addr, &storage.addr_len))
      return ERR_INVALID_ARGUMENT;

    if (!HANDLE_EINTR(connect(socket_, storage.addr, storage.addr_len))) {
      // Connected without waiting!
      return OK;
    }
  } else {
    // With TCP FastOpen, we pretend that the socket is connected.
    DCHECK(!tcp_fastopen_connected_);
    return OK;
  }

  // Check if the connect() failed synchronously.
  connect_os_error_ = errno;
  if (connect_os_error_ != EINPROGRESS)
    return MapConnectError(connect_os_error_);

  // Otherwise the connect() is going to complete asynchronously, so watch
  // for its completion.
  if (!base::MessageLoopForIO::current()->WatchFileDescriptor(
          socket_, true, base::MessageLoopForIO::WATCH_WRITE,
          &write_socket_watcher_, &write_watcher_)) {
    connect_os_error_ = errno;
    DVLOG(1) << "WatchFileDescriptor failed: " << connect_os_error_;
    return MapSystemError(connect_os_error_);
  }

  return ERR_IO_PENDING;
}

int TCPClientSocketLibevent::DoConnectComplete(int result) {
  // Log the end of this attempt (and any OS error it threw).
  int os_error = connect_os_error_;
  connect_os_error_ = 0;
  if (result != OK) {
    net_log_.EndEvent(NetLog::TYPE_TCP_CONNECT_ATTEMPT,
                      NetLog::IntegerCallback("os_error", os_error));
  } else {
    net_log_.EndEvent(NetLog::TYPE_TCP_CONNECT_ATTEMPT);
  }

  if (result == OK) {
    write_socket_watcher_.StopWatchingFileDescriptor();
    use_history_.set_was_ever_connected();
    return OK;  // Done!
  }

  // Close whatever partially connected socket we currently have.
  DoDisconnect();

  // Try to fall back to the next address in the list.
  if (current_address_index_ + 1 < static_cast<int>(addresses_.size())) {
    next_connect_state_ = CONNECT_STATE_CONNECT;
    ++current_address_index_;
    return OK;
  }

  // Otherwise there is nothing to fall back to, so give up.
  return result;
}

void TCPClientSocketLibevent::Disconnect() {
  DCHECK(CalledOnValidThread());

  DoDisconnect();
  current_address_index_ = -1;
  bind_address_.reset();
}

void TCPClientSocketLibevent::DoDisconnect() {
  if (socket_ == kInvalidSocket)
    return;

  bool ok = read_socket_watcher_.StopWatchingFileDescriptor();
  DCHECK(ok);
  ok = write_socket_watcher_.StopWatchingFileDescriptor();
  DCHECK(ok);
  if (HANDLE_EINTR(close(socket_)) < 0)
    PLOG(ERROR) << "close";
  socket_ = kInvalidSocket;
  previously_disconnected_ = true;
}

bool TCPClientSocketLibevent::IsConnected() const {
  DCHECK(CalledOnValidThread());

  if (socket_ == kInvalidSocket || waiting_connect())
    return false;

  if (use_tcp_fastopen_ && !tcp_fastopen_connected_) {
    // With TCP FastOpen, we pretend that the socket is connected.
    // This allows GetPeerAddress() to return current_ai_ as the peer
    // address.  Since we don't fail over to the next address if
    // sendto() fails, current_ai_ is the only possible peer address.
    CHECK_LT(current_address_index_, static_cast<int>(addresses_.size()));
    return true;
  }

  // Check if connection is alive.
  char c;
  int rv = HANDLE_EINTR(recv(socket_, &c, 1, MSG_PEEK));
  if (rv == 0)
    return false;
  if (rv == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
    return false;

  return true;
}

bool TCPClientSocketLibevent::IsConnectedAndIdle() const {
  DCHECK(CalledOnValidThread());

  if (socket_ == kInvalidSocket || waiting_connect())
    return false;

  // TODO(wtc): should we also handle the TCP FastOpen case here,
  // as we do in IsConnected()?

  // Check if connection is alive and we haven't received any data
  // unexpectedly.
  char c;
  int rv = HANDLE_EINTR(recv(socket_, &c, 1, MSG_PEEK));
  if (rv >= 0)
    return false;
  if (errno != EAGAIN && errno != EWOULDBLOCK)
    return false;

  return true;
}

int TCPClientSocketLibevent::Read(IOBuffer* buf,
                                  int buf_len,
                                  const CompletionCallback& callback) {
  DCHECK(CalledOnValidThread());
  DCHECK_NE(kInvalidSocket, socket_);
  DCHECK(!waiting_connect());
  DCHECK(read_callback_.is_null());
  // Synchronous operation not supported
  DCHECK(!callback.is_null());
  DCHECK_GT(buf_len, 0);

  int nread = HANDLE_EINTR(read(socket_, buf->data(), buf_len));
  if (nread >= 0) {
    base::StatsCounter read_bytes("tcp.read_bytes");
    read_bytes.Add(nread);
    if (nread > 0)
      use_history_.set_was_used_to_convey_data();
    net_log_.AddByteTransferEvent(NetLog::TYPE_SOCKET_BYTES_RECEIVED, nread,
                                  buf->data());
    RecordFastOpenStatus();
    return nread;
  }
  if (errno != EAGAIN && errno != EWOULDBLOCK) {
    int net_error = MapSystemError(errno);
    net_log_.AddEvent(NetLog::TYPE_SOCKET_READ_ERROR,
                      CreateNetLogSocketErrorCallback(net_error, errno));
    return net_error;
  }

  if (!base::MessageLoopForIO::current()->WatchFileDescriptor(
          socket_, true, base::MessageLoopForIO::WATCH_READ,
          &read_socket_watcher_, &read_watcher_)) {
    DVLOG(1) << "WatchFileDescriptor failed on read, errno " << errno;
    return MapSystemError(errno);
  }

  read_buf_ = buf;
  read_buf_len_ = buf_len;
  read_callback_ = callback;
  return ERR_IO_PENDING;
}

int TCPClientSocketLibevent::Write(IOBuffer* buf,
                                   int buf_len,
                                   const CompletionCallback& callback) {
  DCHECK(CalledOnValidThread());
  DCHECK_NE(kInvalidSocket, socket_);
  DCHECK(!waiting_connect());
  DCHECK(write_callback_.is_null());
  // Synchronous operation not supported
  DCHECK(!callback.is_null());
  DCHECK_GT(buf_len, 0);

  int nwrite = InternalWrite(buf, buf_len);
  if (nwrite >= 0) {
    base::StatsCounter write_bytes("tcp.write_bytes");
    write_bytes.Add(nwrite);
    if (nwrite > 0)
      use_history_.set_was_used_to_convey_data();
    net_log_.AddByteTransferEvent(NetLog::TYPE_SOCKET_BYTES_SENT, nwrite,
                                  buf->data());
    return nwrite;
  }
  if (errno != EAGAIN && errno != EWOULDBLOCK) {
    int net_error = MapSystemError(errno);
    net_log_.AddEvent(NetLog::TYPE_SOCKET_WRITE_ERROR,
                      CreateNetLogSocketErrorCallback(net_error, errno));
    return net_error;
  }

  if (!base::MessageLoopForIO::current()->WatchFileDescriptor(
          socket_, true, base::MessageLoopForIO::WATCH_WRITE,
          &write_socket_watcher_, &write_watcher_)) {
    DVLOG(1) << "WatchFileDescriptor failed on write, errno " << errno;
    return MapSystemError(errno);
  }

  write_buf_ = buf;
  write_buf_len_ = buf_len;
  write_callback_ = callback;
  return ERR_IO_PENDING;
}

int TCPClientSocketLibevent::InternalWrite(IOBuffer* buf, int buf_len) {
  int nwrite;
  if (use_tcp_fastopen_ && !tcp_fastopen_connected_) {
    SockaddrStorage storage;
    if (!addresses_[current_address_index_].ToSockAddr(storage.addr,
                                                       &storage.addr_len)) {
      errno = EINVAL;
      return -1;
    }

    int flags = 0x20000000; // Magic flag to enable TCP_FASTOPEN.
#if defined(OS_LINUX)
    // sendto() will fail with EPIPE when the system doesn't support TCP Fast
    // Open. Theoretically that shouldn't happen since the caller should check
    // for system support on startup, but users may dynamically disable TCP Fast
    // Open via sysctl.
    flags |= MSG_NOSIGNAL;
#endif // defined(OS_LINUX)
    nwrite = HANDLE_EINTR(sendto(socket_,
                                 buf->data(),
                                 buf_len,
                                 flags,
                                 storage.addr,
                                 storage.addr_len));
    tcp_fastopen_connected_ = true;

    if (nwrite < 0) {
      DCHECK_NE(EPIPE, errno);

      // If errno == EINPROGRESS, that means the kernel didn't have a cookie
      // and would block. The kernel is internally doing a connect() though.
      // Remap EINPROGRESS to EAGAIN so we treat this the same as our other
      // asynchronous cases. Note that the user buffer has not been copied to
      // kernel space.
      if (errno == EINPROGRESS) {
        errno = EAGAIN;
        fast_open_status_ = FAST_OPEN_SLOW_CONNECT_RETURN;
      } else {
        fast_open_status_ = FAST_OPEN_ERROR;
      }
    } else {
      fast_open_status_ = FAST_OPEN_FAST_CONNECT_RETURN;
    }
  } else {
    nwrite = HANDLE_EINTR(write(socket_, buf->data(), buf_len));
  }
  return nwrite;
}

bool TCPClientSocketLibevent::SetReceiveBufferSize(int32 size) {
  DCHECK(CalledOnValidThread());
  int rv = setsockopt(socket_, SOL_SOCKET, SO_RCVBUF,
      reinterpret_cast<const char*>(&size),
      sizeof(size));
  DCHECK(!rv) << "Could not set socket receive buffer size: " << errno;
  return rv == 0;
}

bool TCPClientSocketLibevent::SetSendBufferSize(int32 size) {
  DCHECK(CalledOnValidThread());
  int rv = setsockopt(socket_, SOL_SOCKET, SO_SNDBUF,
      reinterpret_cast<const char*>(&size),
      sizeof(size));
  DCHECK(!rv) << "Could not set socket send buffer size: " << errno;
  return rv == 0;
}

bool TCPClientSocketLibevent::SetKeepAlive(bool enable, int delay) {
  int socket = socket_ != kInvalidSocket ? socket_ : bound_socket_;
  return SetTCPKeepAlive(socket, enable, delay);
}

bool TCPClientSocketLibevent::SetNoDelay(bool no_delay) {
  int socket = socket_ != kInvalidSocket ? socket_ : bound_socket_;
  return SetTCPNoDelay(socket, no_delay);
}

void TCPClientSocketLibevent::ReadWatcher::OnFileCanReadWithoutBlocking(int) {
  socket_->RecordFastOpenStatus();
  if (!socket_->read_callback_.is_null())
    socket_->DidCompleteRead();
}

void TCPClientSocketLibevent::WriteWatcher::OnFileCanWriteWithoutBlocking(int) {
  if (socket_->waiting_connect()) {
    socket_->DidCompleteConnect();
  } else if (!socket_->write_callback_.is_null()) {
    socket_->DidCompleteWrite();
  }
}

void TCPClientSocketLibevent::LogConnectCompletion(int net_error) {
  if (net_error == OK)
    UpdateConnectionTypeHistograms(CONNECTION_ANY);

  if (net_error != OK) {
    net_log_.EndEventWithNetErrorCode(NetLog::TYPE_TCP_CONNECT, net_error);
    return;
  }

  SockaddrStorage storage;
  int rv = getsockname(socket_, storage.addr, &storage.addr_len);
  if (rv != 0) {
    PLOG(ERROR) << "getsockname() [rv: " << rv << "] error: ";
    NOTREACHED();
    net_log_.EndEventWithNetErrorCode(NetLog::TYPE_TCP_CONNECT, rv);
    return;
  }

  net_log_.EndEvent(NetLog::TYPE_TCP_CONNECT,
                    CreateNetLogSourceAddressCallback(storage.addr,
                                                      storage.addr_len));
}

void TCPClientSocketLibevent::DoReadCallback(int rv) {
  DCHECK_NE(rv, ERR_IO_PENDING);
  DCHECK(!read_callback_.is_null());

  // since Run may result in Read being called, clear read_callback_ up front.
  CompletionCallback c = read_callback_;
  read_callback_.Reset();
  c.Run(rv);
}

void TCPClientSocketLibevent::DoWriteCallback(int rv) {
  DCHECK_NE(rv, ERR_IO_PENDING);
  DCHECK(!write_callback_.is_null());

  // since Run may result in Write being called, clear write_callback_ up front.
  CompletionCallback c = write_callback_;
  write_callback_.Reset();
  c.Run(rv);
}

void TCPClientSocketLibevent::DidCompleteConnect() {
  DCHECK_EQ(next_connect_state_, CONNECT_STATE_CONNECT_COMPLETE);

  // Get the error that connect() completed with.
  int os_error = 0;
  socklen_t len = sizeof(os_error);
  if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, &os_error, &len) < 0)
    os_error = errno;

  // TODO(eroman): Is this check really necessary?
  if (os_error == EINPROGRESS || os_error == EALREADY) {
    NOTREACHED();  // This indicates a bug in libevent or our code.
    return;
  }

  connect_os_error_ = os_error;
  int rv = DoConnectLoop(MapConnectError(os_error));
  if (rv != ERR_IO_PENDING) {
    LogConnectCompletion(rv);
    DoWriteCallback(rv);
  }
}

void TCPClientSocketLibevent::DidCompleteRead() {
  int bytes_transferred;
  bytes_transferred = HANDLE_EINTR(read(socket_, read_buf_->data(),
                                        read_buf_len_));

  int result;
  if (bytes_transferred >= 0) {
    result = bytes_transferred;
    base::StatsCounter read_bytes("tcp.read_bytes");
    read_bytes.Add(bytes_transferred);
    if (bytes_transferred > 0)
      use_history_.set_was_used_to_convey_data();
    net_log_.AddByteTransferEvent(NetLog::TYPE_SOCKET_BYTES_RECEIVED, result,
                                  read_buf_->data());
  } else {
    result = MapSystemError(errno);
    if (result != ERR_IO_PENDING) {
      net_log_.AddEvent(NetLog::TYPE_SOCKET_READ_ERROR,
                        CreateNetLogSocketErrorCallback(result, errno));
    }
  }

  if (result != ERR_IO_PENDING) {
    read_buf_ = NULL;
    read_buf_len_ = 0;
    bool ok = read_socket_watcher_.StopWatchingFileDescriptor();
    DCHECK(ok);
    DoReadCallback(result);
  }
}

void TCPClientSocketLibevent::DidCompleteWrite() {
  int bytes_transferred;
  bytes_transferred = HANDLE_EINTR(write(socket_, write_buf_->data(),
                                         write_buf_len_));

  int result;
  if (bytes_transferred >= 0) {
    result = bytes_transferred;
    base::StatsCounter write_bytes("tcp.write_bytes");
    write_bytes.Add(bytes_transferred);
    if (bytes_transferred > 0)
      use_history_.set_was_used_to_convey_data();
    net_log_.AddByteTransferEvent(NetLog::TYPE_SOCKET_BYTES_SENT, result,
                                  write_buf_->data());
  } else {
    result = MapSystemError(errno);
    if (result != ERR_IO_PENDING) {
      net_log_.AddEvent(NetLog::TYPE_SOCKET_WRITE_ERROR,
                        CreateNetLogSocketErrorCallback(result, errno));
    }
  }

  if (result != ERR_IO_PENDING) {
    write_buf_ = NULL;
    write_buf_len_ = 0;
    write_socket_watcher_.StopWatchingFileDescriptor();
    DoWriteCallback(result);
  }
}

int TCPClientSocketLibevent::GetPeerAddress(IPEndPoint* address) const {
  DCHECK(CalledOnValidThread());
  DCHECK(address);
  if (!IsConnected())
    return ERR_SOCKET_NOT_CONNECTED;
  *address = addresses_[current_address_index_];
  return OK;
}

int TCPClientSocketLibevent::GetLocalAddress(IPEndPoint* address) const {
  DCHECK(CalledOnValidThread());
  DCHECK(address);
  if (socket_ == kInvalidSocket) {
    if (bind_address_.get()) {
      *address = *bind_address_;
      return OK;
    }
    return ERR_SOCKET_NOT_CONNECTED;
  }

  SockaddrStorage storage;
  if (getsockname(socket_, storage.addr, &storage.addr_len))
    return MapSystemError(errno);
  if (!address->FromSockAddr(storage.addr, storage.addr_len))
    return ERR_FAILED;

  return OK;
}

void TCPClientSocketLibevent::RecordFastOpenStatus() {
  if (use_tcp_fastopen_ &&
      (fast_open_status_ == FAST_OPEN_FAST_CONNECT_RETURN ||
       fast_open_status_ == FAST_OPEN_SLOW_CONNECT_RETURN)) {
    DCHECK_NE(FAST_OPEN_STATUS_UNKNOWN, fast_open_status_);
    bool getsockopt_success(false);
    bool server_acked_data(false);
#if defined(TCP_INFO)
    // Probe to see the if the socket used TCP Fast Open.
    tcp_info info;
    socklen_t info_len = sizeof(tcp_info);
    getsockopt_success =
        getsockopt(socket_, IPPROTO_TCP, TCP_INFO, &info, &info_len) == 0 &&
        info_len == sizeof(tcp_info);
    server_acked_data = getsockopt_success &&
        (info.tcpi_options & TCPI_OPT_SYN_DATA);
#endif
    if (getsockopt_success) {
      if (fast_open_status_ == FAST_OPEN_FAST_CONNECT_RETURN) {
        fast_open_status_ = (server_acked_data ? FAST_OPEN_SYN_DATA_ACK :
                             FAST_OPEN_SYN_DATA_NACK);
      } else {
        fast_open_status_ = (server_acked_data ? FAST_OPEN_NO_SYN_DATA_ACK :
                             FAST_OPEN_NO_SYN_DATA_NACK);
      }
    } else {
      fast_open_status_ = (fast_open_status_ == FAST_OPEN_FAST_CONNECT_RETURN ?
                           FAST_OPEN_SYN_DATA_FAILED :
                           FAST_OPEN_NO_SYN_DATA_FAILED);
    }
  }
}

const BoundNetLog& TCPClientSocketLibevent::NetLog() const {
  return net_log_;
}

void TCPClientSocketLibevent::SetSubresourceSpeculation() {
  use_history_.set_subresource_speculation();
}

void TCPClientSocketLibevent::SetOmniboxSpeculation() {
  use_history_.set_omnibox_speculation();
}

bool TCPClientSocketLibevent::WasEverUsed() const {
  return use_history_.was_used_to_convey_data();
}

bool TCPClientSocketLibevent::UsingTCPFastOpen() const {
  return use_tcp_fastopen_;
}

bool TCPClientSocketLibevent::WasNpnNegotiated() const {
  return false;
}

NextProto TCPClientSocketLibevent::GetNegotiatedProtocol() const {
  return kProtoUnknown;
}

bool TCPClientSocketLibevent::GetSSLInfo(SSLInfo* ssl_info) {
  return false;
}

}  // namespace net
