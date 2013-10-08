// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_SOCKET_UDP_SOCKET_H_
#define CHROME_BROWSER_EXTENSIONS_API_SOCKET_UDP_SOCKET_H_

#include <string>
#include <vector>

#include "chrome/browser/extensions/api/socket/socket.h"
#include "net/udp/udp_socket.h"

namespace extensions {

class UDPSocket : public Socket {
 public:
  explicit UDPSocket(const std::string& owner_extension_id);
  virtual ~UDPSocket();

  virtual void Connect(const std::string& address,
                       int port,
                       const CompletionCallback& callback) OVERRIDE;
  virtual void Disconnect() OVERRIDE;
  virtual int Bind(const std::string& address, int port) OVERRIDE;
  virtual void Read(int count,
                    const ReadCompletionCallback& callback) OVERRIDE;
  virtual void RecvFrom(int count,
                        const RecvFromCompletionCallback& callback) OVERRIDE;
  virtual void SendTo(scoped_refptr<net::IOBuffer> io_buffer,
                      int byte_count,
                      const std::string& address,
                      int port,
                      const CompletionCallback& callback) OVERRIDE;

  virtual bool IsConnected() OVERRIDE;

  virtual bool GetPeerAddress(net::IPEndPoint* address) OVERRIDE;
  virtual bool GetLocalAddress(net::IPEndPoint* address) OVERRIDE;
  virtual Socket::SocketType GetSocketType() const OVERRIDE;

  int JoinGroup(const std::string& address);
  int LeaveGroup(const std::string& address);

  int SetMulticastTimeToLive(int ttl);
  int SetMulticastLoopbackMode(bool loopback);

  const std::vector<std::string>& GetJoinedGroups() const;

 protected:
  virtual int WriteImpl(net::IOBuffer* io_buffer,
                        int io_buffer_size,
                        const net::CompletionCallback& callback) OVERRIDE;

 private:
  // Make net::IPEndPoint can be refcounted
  typedef base::RefCountedData<net::IPEndPoint> IPEndPoint;

  void OnReadComplete(scoped_refptr<net::IOBuffer> io_buffer,
                      int result);
  void OnRecvFromComplete(scoped_refptr<net::IOBuffer> io_buffer,
                          scoped_refptr<IPEndPoint> address,
                          int result);
  void OnSendToComplete(int result);

  net::UDPSocket socket_;

  ReadCompletionCallback read_callback_;

  RecvFromCompletionCallback recv_from_callback_;

  CompletionCallback send_to_callback_;

  std::vector<std::string> multicast_groups_;
};

}  //  namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_SOCKET_UDP_SOCKET_H_
