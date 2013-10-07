// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DEVTOOLS_ADB_ANDROID_USB_DEVICE_H_
#define CHROME_BROWSER_DEVTOOLS_ADB_ANDROID_USB_DEVICE_H_

#include <map>
#include <queue>
#include <vector>
#include "base/memory/ref_counted.h"
#include "chrome/browser/usb/usb_device_handle.h"

namespace base {
class MessageLoop;
}

namespace crypto {
class RSAPrivateKey;
}

namespace net {
class StreamSocket;
}

class AndroidUsbSocket;

class AdbMessage : public base::RefCounted<AdbMessage> {
 public:
  enum Command {
    kCommandSYNC = 0x434e5953,
    kCommandCNXN = 0x4e584e43,
    kCommandOPEN = 0x4e45504f,
    kCommandOKAY = 0x59414b4f,
    kCommandCLSE = 0x45534c43,
    kCommandWRTE = 0x45545257,
    kCommandAUTH = 0x48545541
  };

  enum Auth {
    kAuthToken = 1,
    kAuthSignature = 2,
    kAuthRSAPublicKey = 3
  };

  AdbMessage(uint32 command,
             uint32 arg0,
             uint32 arg1,
             const std::string& body);

  uint32 command;
  uint32 arg0;
  uint32 arg1;
  std::string body;
 private:
  friend class base::RefCounted<AdbMessage>;
  ~AdbMessage();

  DISALLOW_COPY_AND_ASSIGN(AdbMessage);
};

class AndroidUsbDevice;
typedef std::vector<scoped_refptr<AndroidUsbDevice> > AndroidUsbDevices;
typedef base::Callback<void(const AndroidUsbDevices&)>
    AndroidUsbDevicesCallback;

class AndroidUsbDevice : public base::RefCountedThreadSafe<AndroidUsbDevice> {
 public:
  static void Enumerate(crypto::RSAPrivateKey* rsa_key,
                        const AndroidUsbDevicesCallback& callback);

  AndroidUsbDevice(crypto::RSAPrivateKey* rsa_key,
                   scoped_refptr<UsbDeviceHandle> device,
                   const std::string& serial,
                   int inbound_address,
                   int outbound_address,
                   int zero_mask);

  void InitOnCallerThread();

  net::StreamSocket* CreateSocket(const std::string& command);

  void Send(uint32 command,
            uint32 arg0,
            uint32 arg1,
            const std::string& body);

  scoped_refptr<UsbDeviceHandle> usb_device() { return usb_device_; }

  std::string serial() { return serial_; }

  bool terminated() { return terminated_; }

 private:
  friend class base::RefCountedThreadSafe<AndroidUsbDevice>;
  virtual ~AndroidUsbDevice();

  void Queue(scoped_refptr<AdbMessage> message);
  void ProcessOutgoing();
  void OutgoingMessageSent(UsbTransferStatus status,
                           scoped_refptr<net::IOBuffer> buffer,
                           size_t result);

  void ReadHeader(bool initial);
  void ParseHeader(UsbTransferStatus status,
                   scoped_refptr<net::IOBuffer> buffer,
                   size_t result);

  void ReadBody(scoped_refptr<AdbMessage> message,
                uint32 data_length,
                uint32 data_check);
  void ParseBody(scoped_refptr<AdbMessage> message,
                 uint32 data_length,
                 uint32 data_check,
                 UsbTransferStatus status,
                 scoped_refptr<net::IOBuffer> buffer,
                 size_t result);

  void HandleIncoming(scoped_refptr<AdbMessage> message);

  void TransferError(UsbTransferStatus status);

  void Terminate();

  void SocketDeleted(uint32 socket_id);

  base::MessageLoop* message_loop_;

  scoped_ptr<crypto::RSAPrivateKey> rsa_key_;

  // Device info
  scoped_refptr<UsbDeviceHandle> usb_device_;
  std::string serial_;
  int inbound_address_;
  int outbound_address_;
  int zero_mask_;

  bool is_connected_;
  bool signature_sent_;

  // Created sockets info
  uint32 last_socket_id_;
  bool terminated_;
  typedef std::map<uint32, AndroidUsbSocket*> AndroidUsbSockets;
  AndroidUsbSockets sockets_;

  // Outgoing bulk queue
  typedef std::pair<scoped_refptr<net::IOBuffer>, size_t> BulkMessage;
  std::queue<BulkMessage> outgoing_queue_;

  // Outgoing messages pending connect
  typedef std::vector<scoped_refptr<AdbMessage> > PendingMessages;
  PendingMessages pending_messages_;

  DISALLOW_COPY_AND_ASSIGN(AndroidUsbDevice);
};

#endif  // CHROME_BROWSER_DEVTOOLS_ADB_ANDROID_USB_DEVICE_H_
