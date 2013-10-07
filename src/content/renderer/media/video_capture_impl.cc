// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/video_capture_impl.h"

#include "base/bind.h"
#include "base/stl_util.h"
#include "content/child/child_process.h"
#include "content/common/media/video_capture_messages.h"
#include "media/base/limits.h"

namespace content {

struct VideoCaptureImpl::DIBBuffer {
 public:
  DIBBuffer(
      base::SharedMemory* d,
      media::VideoCapture::VideoFrameBuffer* ptr)
      : dib(d),
        mapped_memory(ptr),
        references(0) {
  }
  ~DIBBuffer() {}

  scoped_ptr<base::SharedMemory> dib;
  scoped_refptr<media::VideoCapture::VideoFrameBuffer> mapped_memory;

  // Number of clients which hold this DIB.
  int references;
};

bool VideoCaptureImpl::CaptureStarted() {
  return state_ == VIDEO_CAPTURE_STATE_STARTED;
}

int VideoCaptureImpl::CaptureWidth() {
  return capture_format_.width;
}

int VideoCaptureImpl::CaptureHeight() {
  return capture_format_.height;
}

int VideoCaptureImpl::CaptureFrameRate() {
  return capture_format_.frame_rate;
}

VideoCaptureImpl::VideoCaptureImpl(
    const media::VideoCaptureSessionId id,
    base::MessageLoopProxy* capture_message_loop_proxy,
    VideoCaptureMessageFilter* filter)
    : VideoCapture(),
      message_filter_(filter),
      capture_message_loop_proxy_(capture_message_loop_proxy),
      io_message_loop_proxy_(ChildProcess::current()->io_message_loop_proxy()),
      device_id_(0),
      video_type_(media::VideoCaptureCapability::kI420),
      device_info_available_(false),
      suspended_(false),
      state_(VIDEO_CAPTURE_STATE_STOPPED) {
  DCHECK(filter);
  capture_format_.session_id = id;
}

VideoCaptureImpl::~VideoCaptureImpl() {
  STLDeleteValues(&cached_dibs_);
}

void VideoCaptureImpl::Init() {
  if (!io_message_loop_proxy_->BelongsToCurrentThread()) {
    io_message_loop_proxy_->PostTask(FROM_HERE,
        base::Bind(&VideoCaptureImpl::AddDelegateOnIOThread,
                   base::Unretained(this)));
  } else {
    AddDelegateOnIOThread();
  }
}

void VideoCaptureImpl::DeInit(base::Closure task) {
  capture_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::DoDeInitOnCaptureThread,
                 base::Unretained(this), task));
}

void VideoCaptureImpl::StartCapture(
    media::VideoCapture::EventHandler* handler,
    const media::VideoCaptureCapability& capability) {
  DCHECK_EQ(capability.color, media::VideoCaptureCapability::kI420);

  capture_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::DoStartCaptureOnCaptureThread,
                 base::Unretained(this), handler, capability));
}

void VideoCaptureImpl::StopCapture(media::VideoCapture::EventHandler* handler) {
  capture_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::DoStopCaptureOnCaptureThread,
                 base::Unretained(this), handler));
}

void VideoCaptureImpl::FeedBuffer(scoped_refptr<VideoFrameBuffer> buffer) {
  capture_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::DoFeedBufferOnCaptureThread,
                 base::Unretained(this), buffer));
}

void VideoCaptureImpl::OnBufferCreated(
    base::SharedMemoryHandle handle,
    int length, int buffer_id) {
  capture_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::DoBufferCreatedOnCaptureThread,
                 base::Unretained(this), handle, length, buffer_id));
}

void VideoCaptureImpl::OnBufferReceived(int buffer_id, base::Time timestamp) {
  capture_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::DoBufferReceivedOnCaptureThread,
                 base::Unretained(this), buffer_id, timestamp));
}

void VideoCaptureImpl::OnStateChanged(VideoCaptureState state) {
  capture_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::DoStateChangedOnCaptureThread,
                 base::Unretained(this), state));
}

void VideoCaptureImpl::OnDeviceInfoReceived(
    const media::VideoCaptureParams& device_info) {
  capture_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::DoDeviceInfoReceivedOnCaptureThread,
                 base::Unretained(this), device_info));
}

void VideoCaptureImpl::OnDeviceInfoChanged(
    const media::VideoCaptureParams& device_info) {
  capture_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::DoDeviceInfoChangedOnCaptureThread,
                 base::Unretained(this), device_info));
}

void VideoCaptureImpl::OnDelegateAdded(int32 device_id) {
  capture_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::DoDelegateAddedOnCaptureThread,
                 base::Unretained(this), device_id));
}

void VideoCaptureImpl::SuspendCapture(bool suspend) {
  capture_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::DoSuspendCaptureOnCaptureThread,
                 base::Unretained(this), suspend));
}

void VideoCaptureImpl::DoDeInitOnCaptureThread(base::Closure task) {
  if (state_ == VIDEO_CAPTURE_STATE_STARTED)
    Send(new VideoCaptureHostMsg_Stop(device_id_));

  io_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(&VideoCaptureImpl::RemoveDelegateOnIOThread,
                 base::Unretained(this), task));
}

void VideoCaptureImpl::DoStartCaptureOnCaptureThread(
    media::VideoCapture::EventHandler* handler,
    const media::VideoCaptureCapability& capability) {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());

  if (state_ == VIDEO_CAPTURE_STATE_ERROR) {
    handler->OnError(this, 1);
    handler->OnRemoved(this);
  } else if ((clients_pending_on_filter_.find(handler) !=
              clients_pending_on_filter_.end()) ||
             (clients_pending_on_restart_.find(handler) !=
              clients_pending_on_restart_.end()) ||
             clients_.find(handler) != clients_.end() ) {
    // This client has started.
  } else if (!device_id_) {
    clients_pending_on_filter_[handler] = capability;
  } else {
    handler->OnStarted(this);
    if (state_ == VIDEO_CAPTURE_STATE_STARTED) {
      // TODO(wjia): Temporarily disable restarting till client supports
      // resampling.
#if 0
      if (capability.width > capture_format_.width ||
          capability.height > capture_format_.height) {
        StopDevice();
        DVLOG(1) << "StartCapture: Got client with higher resolution ("
                 << capability.width << ", " << capability.height << ") "
                 << "after started, try to restart.";
        clients_pending_on_restart_[handler] = capability;
      } else {
#endif
      {
        if (device_info_available_) {
          handler->OnDeviceInfoReceived(this, device_info_);
        }

        clients_[handler] = capability;
      }
    } else if (state_ == VIDEO_CAPTURE_STATE_STOPPING) {
      clients_pending_on_restart_[handler] = capability;
      DVLOG(1) << "StartCapture: Got new resolution ("
               << capability.width << ", " << capability.height << ") "
               << ", during stopping.";
    } else {
      clients_[handler] = capability;
      DCHECK_EQ(1ul, clients_.size());
      video_type_ = capability.color;
      int session_id = capture_format_.session_id;
      DCHECK_EQ(capability.session_id, 0);
      capture_format_ = capability;
      capture_format_.session_id = session_id;
      if (capture_format_.frame_rate > media::limits::kMaxFramesPerSecond)
        capture_format_.frame_rate = media::limits::kMaxFramesPerSecond;
      DVLOG(1) << "StartCapture: starting with first resolution ("
               << capture_format_.width << "," << capture_format_.height << ")";

      StartCaptureInternal();
    }
  }
}

void VideoCaptureImpl::DoStopCaptureOnCaptureThread(
    media::VideoCapture::EventHandler* handler) {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());

  // A handler can be in only one client list.
  // If this handler is in any client list, we can just remove it from
  // that client list and don't have to run the other following RemoveClient().
  RemoveClient(handler, &clients_pending_on_filter_) ||
  RemoveClient(handler, &clients_pending_on_restart_) ||
  RemoveClient(handler, &clients_);

  if (clients_.empty()) {
    DVLOG(1) << "StopCapture: No more client, stopping ...";
    StopDevice();
  }
}

void VideoCaptureImpl::DoFeedBufferOnCaptureThread(
    scoped_refptr<VideoFrameBuffer> buffer) {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());

  CachedDIB::iterator it;
  for (it = cached_dibs_.begin(); it != cached_dibs_.end(); ++it) {
    if (buffer.get() == it->second->mapped_memory.get())
      break;
  }

  if (it != cached_dibs_.end() && it->second) {
    DCHECK_GT(it->second->references, 0);
    --it->second->references;
    if (it->second->references == 0) {
      Send(new VideoCaptureHostMsg_BufferReady(device_id_, it->first));
    }
  }
}

void VideoCaptureImpl::DoBufferCreatedOnCaptureThread(
    base::SharedMemoryHandle handle,
    int length, int buffer_id) {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());

  // In case client calls StopCapture before the arrival of created buffer,
  // just close this buffer and return.
  if (state_ != VIDEO_CAPTURE_STATE_STARTED) {
    base::SharedMemory::CloseHandle(handle);
    return;
  }

  DCHECK(device_info_available_);

  media::VideoCapture::VideoFrameBuffer* buffer;
  DCHECK(cached_dibs_.find(buffer_id) == cached_dibs_.end());

  base::SharedMemory* dib = new base::SharedMemory(handle, false);
  dib->Map(length);
  buffer = new VideoFrameBuffer();
  buffer->memory_pointer = static_cast<uint8*>(dib->memory());
  buffer->buffer_size = length;
  buffer->width = device_info_.width;
  buffer->height = device_info_.height;
  buffer->stride = device_info_.width;

  DIBBuffer* dib_buffer = new DIBBuffer(dib, buffer);
  cached_dibs_[buffer_id] = dib_buffer;
}

void VideoCaptureImpl::DoBufferReceivedOnCaptureThread(
    int buffer_id, base::Time timestamp) {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());

  if (state_ != VIDEO_CAPTURE_STATE_STARTED || suspended_) {
    Send(new VideoCaptureHostMsg_BufferReady(device_id_, buffer_id));
    return;
  }

  media::VideoCapture::VideoFrameBuffer* buffer;
  DCHECK(cached_dibs_.find(buffer_id) != cached_dibs_.end());
  buffer = cached_dibs_[buffer_id]->mapped_memory.get();
  buffer->timestamp = timestamp;

  for (ClientInfo::iterator it = clients_.begin(); it != clients_.end(); ++it) {
    it->first->OnBufferReady(this, buffer);
  }
  cached_dibs_[buffer_id]->references = clients_.size();
}

void VideoCaptureImpl::DoStateChangedOnCaptureThread(VideoCaptureState state) {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());

  switch (state) {
    case VIDEO_CAPTURE_STATE_STARTED:
      break;
    case VIDEO_CAPTURE_STATE_STOPPED:
      state_ = VIDEO_CAPTURE_STATE_STOPPED;
      DVLOG(1) << "OnStateChanged: stopped!, device_id = " << device_id_;
      STLDeleteValues(&cached_dibs_);
      if (!clients_.empty() || !clients_pending_on_restart_.empty())
        RestartCapture();
      break;
    case VIDEO_CAPTURE_STATE_PAUSED:
      for (ClientInfo::iterator it = clients_.begin();
           it != clients_.end(); ++it) {
        it->first->OnPaused(this);
      }
      break;
    case VIDEO_CAPTURE_STATE_ERROR:
      DVLOG(1) << "OnStateChanged: error!, device_id = " << device_id_;
      for (ClientInfo::iterator it = clients_.begin();
           it != clients_.end(); ++it) {
        // TODO(wjia): browser process would send error code.
        it->first->OnError(this, 1);
        it->first->OnRemoved(this);
      }
      clients_.clear();
      state_ = VIDEO_CAPTURE_STATE_ERROR;
      break;
    case VIDEO_CAPTURE_STATE_ENDED:
      DVLOG(1) << "OnStateChanged: ended!, device_id = " << device_id_;
      for (ClientInfo::iterator it = clients_.begin();
          it != clients_.end(); ++it) {
        it->first->OnRemoved(this);
      }
      clients_.clear();
      state_ = VIDEO_CAPTURE_STATE_ENDED;
      break;
    default:
      break;
  }
}

void VideoCaptureImpl::DoDeviceInfoReceivedOnCaptureThread(
    const media::VideoCaptureParams& device_info) {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());
  DCHECK(!ClientHasDIB());

  STLDeleteValues(&cached_dibs_);

  device_info_ = device_info;
  device_info_available_ = true;
  for (ClientInfo::iterator it = clients_.begin(); it != clients_.end(); ++it) {
    it->first->OnDeviceInfoReceived(this, device_info);
  }
}

void VideoCaptureImpl::DoDeviceInfoChangedOnCaptureThread(
    const media::VideoCaptureParams& device_info) {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());

  for (ClientInfo::iterator it = clients_.begin(); it != clients_.end(); ++it) {
    it->first->OnDeviceInfoChanged(this, device_info);
  }
}

void VideoCaptureImpl::DoDelegateAddedOnCaptureThread(int32 device_id) {
  DVLOG(1) << "DoDelegateAdded: device_id " << device_id;
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());

  device_id_ = device_id;
  for (ClientInfo::iterator it = clients_pending_on_filter_.begin();
       it != clients_pending_on_filter_.end(); ) {
    media::VideoCapture::EventHandler* handler = it->first;
    const media::VideoCaptureCapability capability = it->second;
    clients_pending_on_filter_.erase(it++);
    StartCapture(handler, capability);
  }
}

void VideoCaptureImpl::DoSuspendCaptureOnCaptureThread(bool suspend) {
  DVLOG(1) << "DoSuspendCapture: suspend " << (suspend ? "yes" : "no");
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());

  suspended_ = suspend;
}

void VideoCaptureImpl::StopDevice() {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());

  device_info_available_ = false;
  if (state_ == VIDEO_CAPTURE_STATE_STARTED) {
    state_ = VIDEO_CAPTURE_STATE_STOPPING;
    Send(new VideoCaptureHostMsg_Stop(device_id_));
    capture_format_.width = capture_format_.height = 0;
  }
}

void VideoCaptureImpl::RestartCapture() {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());
  DCHECK_EQ(state_, VIDEO_CAPTURE_STATE_STOPPED);

  int width = 0;
  int height = 0;
  for (ClientInfo::iterator it = clients_.begin();
       it != clients_.end(); ++it) {
    width = std::max(width, it->second.width);
    height = std::max(height, it->second.height);
  }
  for (ClientInfo::iterator it = clients_pending_on_restart_.begin();
       it != clients_pending_on_restart_.end(); ) {
    width = std::max(width, it->second.width);
    height = std::max(height, it->second.height);
    clients_[it->first] = it->second;
    clients_pending_on_restart_.erase(it++);
  }
  capture_format_.width = width;
  capture_format_.height = height;
  DVLOG(1) << "RestartCapture, " << capture_format_.width << ", "
           << capture_format_.height;
  StartCaptureInternal();
}

void VideoCaptureImpl::StartCaptureInternal() {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());
  DCHECK(device_id_);

  media::VideoCaptureParams capability_as_params_copy;
  capability_as_params_copy.width = capture_format_.width;
  capability_as_params_copy.height = capture_format_.height;
  capability_as_params_copy.frame_per_second = capture_format_.frame_rate;
  capability_as_params_copy.session_id = capture_format_.session_id;
  capability_as_params_copy.frame_size_type = capture_format_.frame_size_type;
  Send(new VideoCaptureHostMsg_Start(device_id_, capability_as_params_copy));
  state_ = VIDEO_CAPTURE_STATE_STARTED;
}

void VideoCaptureImpl::AddDelegateOnIOThread() {
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());
  message_filter_->AddDelegate(this);
}

void VideoCaptureImpl::RemoveDelegateOnIOThread(base::Closure task) {
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());
  message_filter_->RemoveDelegate(this);
  capture_message_loop_proxy_->PostTask(FROM_HERE, task);
}

void VideoCaptureImpl::Send(IPC::Message* message) {
  io_message_loop_proxy_->PostTask(FROM_HERE,
      base::Bind(base::IgnoreResult(&VideoCaptureMessageFilter::Send),
                 message_filter_.get(), message));
}

bool VideoCaptureImpl::ClientHasDIB() const {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());
  for (CachedDIB::const_iterator it = cached_dibs_.begin();
       it != cached_dibs_.end(); ++it) {
    if (it->second->references > 0)
      return true;
  }
  return false;
}

bool VideoCaptureImpl::RemoveClient(
    media::VideoCapture::EventHandler* handler,
    ClientInfo* clients) {
  DCHECK(capture_message_loop_proxy_->BelongsToCurrentThread());
  bool found = false;

  ClientInfo::iterator it = clients->find(handler);
  if (it != clients->end()) {
    handler->OnStopped(this);
    handler->OnRemoved(this);
    clients->erase(it);
    found = true;
  }
  return found;
}

}  // namespace content
