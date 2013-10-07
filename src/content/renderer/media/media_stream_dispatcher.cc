// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/media_stream_dispatcher.h"

#include "base/logging.h"
#include "base/message_loop/message_loop_proxy.h"
#include "content/common/media/media_stream_messages.h"
#include "content/renderer/media/media_stream_dispatcher_eventhandler.h"
#include "content/renderer/render_thread_impl.h"
#include "content/renderer/render_view_impl.h"
#include "url/gurl.h"

namespace content {

// A request is identified by pair (request_id, handler), or ipc_request.
// There could be multiple clients making requests and each has its own
// request_id sequence.
// The ipc_request is garanteed to be unique when it's created in
// MediaStreamDispatcher.
struct MediaStreamDispatcher::Request {
  Request(const base::WeakPtr<MediaStreamDispatcherEventHandler>& handler,
          int request_id,
          int ipc_request)
      : handler(handler),
        request_id(request_id),
        ipc_request(ipc_request) {
  }
  bool IsThisRequest(
      int request_id1,
      const base::WeakPtr<MediaStreamDispatcherEventHandler>& handler1) {
    return (request_id1 == request_id && handler1.get() == handler.get());
  }
  base::WeakPtr<MediaStreamDispatcherEventHandler> handler;
  int request_id;
  int ipc_request;
};

struct MediaStreamDispatcher::Stream {
  Stream() {}
  ~Stream() {}
  base::WeakPtr<MediaStreamDispatcherEventHandler> handler;
  StreamDeviceInfoArray audio_array;
  StreamDeviceInfoArray video_array;
};

MediaStreamDispatcher::EnumerationRequest::EnumerationRequest(
    const base::WeakPtr<MediaStreamDispatcherEventHandler>& handler,
    int request_id)
    : handler(handler),
      request_id(request_id) {
}

MediaStreamDispatcher::EnumerationRequest::~EnumerationRequest() {}

bool MediaStreamDispatcher::EnumerationRequest::IsThisRequest(
    int request_id1,
    const base::WeakPtr<MediaStreamDispatcherEventHandler>& handler1) {
  return (request_id1 == request_id && handler1.get() == handler.get());
}

MediaStreamDispatcher::EnumerationState::EnumerationState()
    : ipc_id(-1) {
}

MediaStreamDispatcher::EnumerationState::~EnumerationState() {}

struct MediaStreamDispatcher::EnumerationState::CachedDevices {
  CachedDevices(const std::string& label,
                const StreamDeviceInfoArray& device_array)
      : label(label),
        devices(device_array) {
  }
  ~CachedDevices() {}

  std::string label;
  StreamDeviceInfoArray devices;
};

MediaStreamDispatcher::MediaStreamDispatcher(RenderViewImpl* render_view)
    : RenderViewObserver(render_view),
      main_loop_(base::MessageLoopProxy::current()),
      next_ipc_id_(0) {
}

MediaStreamDispatcher::~MediaStreamDispatcher() {}

void MediaStreamDispatcher::GenerateStream(
    int request_id,
    const base::WeakPtr<MediaStreamDispatcherEventHandler>& event_handler,
    const StreamOptions& components,
    const GURL& security_origin) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  DVLOG(1) << "MediaStreamDispatcher::GenerateStream(" << request_id << ")";

  requests_.push_back(Request(event_handler, request_id, next_ipc_id_));
  Send(new MediaStreamHostMsg_GenerateStream(routing_id(),
                                             next_ipc_id_++,
                                             components,
                                             security_origin));
}

void MediaStreamDispatcher::CancelGenerateStream(
    int request_id,
    const base::WeakPtr<MediaStreamDispatcherEventHandler>& event_handler) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  DVLOG(1) << "MediaStreamDispatcher::CancelGenerateStream"
           << ", {request_id = " << request_id << "}";

  RequestList::iterator it = requests_.begin();
  for (; it != requests_.end(); ++it) {
    if (it->IsThisRequest(request_id, event_handler)) {
      int ipc_request = it->ipc_request;
      requests_.erase(it);
      Send(new MediaStreamHostMsg_CancelGenerateStream(routing_id(),
                                                       ipc_request));
      break;
    }
  }
}

void MediaStreamDispatcher::StopStream(const std::string& label) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  DVLOG(1) << "MediaStreamDispatcher::StopStream"
           << ", {label = " << label << "}";

  LabelStreamMap::iterator it = label_stream_map_.find(label);
  if (it == label_stream_map_.end())
    return;

  Send(new MediaStreamHostMsg_StopGeneratedStream(routing_id(), label));
  label_stream_map_.erase(it);
}

void MediaStreamDispatcher::EnumerateDevices(
    int request_id,
    const base::WeakPtr<MediaStreamDispatcherEventHandler>& event_handler,
    MediaStreamType type,
    const GURL& security_origin) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  DCHECK(type == MEDIA_DEVICE_AUDIO_CAPTURE ||
         type == MEDIA_DEVICE_VIDEO_CAPTURE);
  DVLOG(1) << "MediaStreamDispatcher::EnumerateDevices("
           << request_id << ")";

  EnumerationState* state =
      (type == MEDIA_DEVICE_AUDIO_CAPTURE ?
       &audio_enumeration_state_ : &video_enumeration_state_);
  state->requests.push_back(EnumerationRequest(event_handler, request_id));

  if (state->cached_devices) {
    event_handler->OnDevicesEnumerated(
        request_id, state->cached_devices->devices);
  } else if (state->ipc_id < 0) {
    Send(new MediaStreamHostMsg_EnumerateDevices(routing_id(),
                                                 next_ipc_id_,
                                                 type,
                                                 security_origin));
    state->ipc_id = next_ipc_id_++;
  }
}

void MediaStreamDispatcher::StopEnumerateDevices(
    int request_id,
    const base::WeakPtr<MediaStreamDispatcherEventHandler>& event_handler) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  DVLOG(1) << "MediaStreamDispatcher::StopEnumerateDevices("
           << request_id << ")";

  // Remove the request.
  RemoveEnumerationRequest(
      request_id, event_handler, &audio_enumeration_state_);
  RemoveEnumerationRequest(
      request_id, event_handler, &video_enumeration_state_);
}

void MediaStreamDispatcher::RemoveEnumerationRequest(
    int request_id,
    const base::WeakPtr<MediaStreamDispatcherEventHandler>& event_handler,
    EnumerationState* state) {
  EnumerationRequestList* requests = &state->requests;
  for (EnumerationRequestList::iterator it = requests->begin();
       it != requests->end(); ++it) {
    if (it->IsThisRequest(request_id, event_handler)) {
      requests->erase(it);
      if (requests->empty() && state->cached_devices) {
        // No more request and has a label, try to stop the label
        // and invalidate the state.
        Send(new MediaStreamHostMsg_StopGeneratedStream(
            routing_id(), state->cached_devices->label));
        state->ipc_id = -1;
        state->cached_devices.reset();
      }
      return;
    }
  }
}

void MediaStreamDispatcher::OpenDevice(
    int request_id,
    const base::WeakPtr<MediaStreamDispatcherEventHandler>& event_handler,
    const std::string& device_id,
    MediaStreamType type,
    const GURL& security_origin) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  DVLOG(1) << "MediaStreamDispatcher::OpenDevice(" << request_id << ")";

  requests_.push_back(Request(event_handler, request_id, next_ipc_id_));
  Send(new MediaStreamHostMsg_OpenDevice(routing_id(),
                                         next_ipc_id_++,
                                         device_id,
                                         type,
                                         security_origin));
}

void MediaStreamDispatcher::CancelOpenDevice(
    int request_id,
    const base::WeakPtr<MediaStreamDispatcherEventHandler>& event_handler) {
  CancelGenerateStream(request_id, event_handler);
}

void MediaStreamDispatcher::CloseDevice(const std::string& label) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  DVLOG(1) << "MediaStreamDispatcher::CloseDevice"
           << ", {label = " << label << "}";

  StopStream(label);
}

bool MediaStreamDispatcher::Send(IPC::Message* message) {
  if (!RenderThread::Get()) {
    delete message;
    return false;
  }

  return RenderThread::Get()->Send(message);
}

bool MediaStreamDispatcher::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(MediaStreamDispatcher, message)
    IPC_MESSAGE_HANDLER(MediaStreamMsg_StreamGenerated,
                        OnStreamGenerated)
    IPC_MESSAGE_HANDLER(MediaStreamMsg_StreamGenerationFailed,
                        OnStreamGenerationFailed)
    IPC_MESSAGE_HANDLER(MediaStreamMsg_StopGeneratedStream,
                        OnStopGeneratedStream)
    IPC_MESSAGE_HANDLER(MediaStreamMsg_DevicesEnumerated,
                        OnDevicesEnumerated)
    IPC_MESSAGE_HANDLER(MediaStreamMsg_DevicesEnumerationFailed,
                        OnDevicesEnumerationFailed)
    IPC_MESSAGE_HANDLER(MediaStreamMsg_DeviceOpened,
                        OnDeviceOpened)
    IPC_MESSAGE_HANDLER(MediaStreamMsg_DeviceOpenFailed,
                        OnDeviceOpenFailed)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void MediaStreamDispatcher::OnStreamGenerated(
    int request_id,
    const std::string& label,
    const StreamDeviceInfoArray& audio_array,
    const StreamDeviceInfoArray& video_array) {
  DCHECK(main_loop_->BelongsToCurrentThread());

  for (RequestList::iterator it = requests_.begin();
       it != requests_.end(); ++it) {
    Request& request = *it;
    if (request.ipc_request == request_id) {
      Stream new_stream;
      new_stream.handler = request.handler;
      new_stream.audio_array = audio_array;
      new_stream.video_array = video_array;
      label_stream_map_[label] = new_stream;
      if (request.handler.get()) {
        request.handler->OnStreamGenerated(
            request.request_id, label, audio_array, video_array);
        DVLOG(1) << "MediaStreamDispatcher::OnStreamGenerated("
                 << request.request_id << ", " << label << ")";
      }
      requests_.erase(it);
      break;
    }
  }
}

void MediaStreamDispatcher::OnStreamGenerationFailed(int request_id) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  for (RequestList::iterator it = requests_.begin();
       it != requests_.end(); ++it) {
    Request& request = *it;
    if (request.ipc_request == request_id) {
      if (request.handler.get()) {
        request.handler->OnStreamGenerationFailed(request.request_id);
        DVLOG(1) << "MediaStreamDispatcher::OnStreamGenerationFailed("
                 << request.request_id << ")\n";
      }
      requests_.erase(it);
      break;
    }
  }
}

void MediaStreamDispatcher::OnStopGeneratedStream(const std::string& label) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  LabelStreamMap::iterator it = label_stream_map_.find(label);
  if (it == label_stream_map_.end())
    return;

  if (it->second.handler.get()) {
    it->second.handler->OnStopGeneratedStream(label);
    DVLOG(1) << "MediaStreamDispatcher::OnStopGeneratedStream("
             << label << ")\n";
  }
  label_stream_map_.erase(it);
}

void MediaStreamDispatcher::OnDevicesEnumerated(
    int request_id,
    const std::string& label,
    const StreamDeviceInfoArray& device_array) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  DCHECK_GE(request_id, 0);

  EnumerationState* state;
  if (request_id == audio_enumeration_state_.ipc_id) {
    state = &audio_enumeration_state_;
  } else if (request_id == video_enumeration_state_.ipc_id) {
    state = &video_enumeration_state_;
  } else {
    // This could happen when requester has stopped enumeration while some
    // enumerated response is on the way. Have to stop the |label| because
    // this might be the first enumerated device list is received. This also
    // lead to same label being stopped multiple times.
    Send(new MediaStreamHostMsg_StopGeneratedStream(routing_id(), label));
    return;
  }

  DCHECK(!label.empty());
  state->cached_devices.reset(new EnumerationState::CachedDevices(
      label, device_array));

  for (EnumerationRequestList::iterator it = state->requests.begin();
       it != state->requests.end(); ++it) {
    if (it->handler.get()) {
      it->handler->OnDevicesEnumerated(it->request_id, device_array);
      DVLOG(1) << "MediaStreamDispatcher::OnDevicesEnumerated("
               << it->request_id << ")";
    }
  }
}

void MediaStreamDispatcher::OnDevicesEnumerationFailed(int request_id) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  for (RequestList::iterator it = requests_.begin();
       it != requests_.end(); ++it) {
    Request& request = *it;
    if (request.ipc_request == request_id) {
      if (request.handler.get()) {
        request.handler->OnDevicesEnumerationFailed(request.request_id);
        DVLOG(1) << "MediaStreamDispatcher::OnDevicesEnumerationFailed("
                 << request.request_id << ")\n";
      }
      requests_.erase(it);
      break;
    }
  }
}

void MediaStreamDispatcher::OnDeviceOpened(
    int request_id,
    const std::string& label,
    const StreamDeviceInfo& device_info) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  for (RequestList::iterator it = requests_.begin();
       it != requests_.end(); ++it) {
    Request& request = *it;
    if (request.ipc_request == request_id) {
      Stream new_stream;
      new_stream.handler = request.handler;
      if (IsAudioMediaType(device_info.device.type)) {
        new_stream.audio_array.push_back(device_info);
      } else if (IsVideoMediaType(device_info.device.type)) {
        new_stream.video_array.push_back(device_info);
      } else {
        NOTREACHED();
      }
      label_stream_map_[label] = new_stream;
      if (request.handler.get()) {
        request.handler->OnDeviceOpened(request.request_id, label, device_info);
        DVLOG(1) << "MediaStreamDispatcher::OnDeviceOpened("
                 << request.request_id << ", " << label << ")";
      }
      requests_.erase(it);
      break;
    }
  }
}

void MediaStreamDispatcher::OnDeviceOpenFailed(int request_id) {
  DCHECK(main_loop_->BelongsToCurrentThread());
  for (RequestList::iterator it = requests_.begin();
       it != requests_.end(); ++it) {
    Request& request = *it;
    if (request.ipc_request == request_id) {
      if (request.handler.get()) {
        request.handler->OnDeviceOpenFailed(request.request_id);
        DVLOG(1) << "MediaStreamDispatcher::OnDeviceOpenFailed("
                 << request.request_id << ")\n";
      }
      requests_.erase(it);
      break;
    }
  }
}

int MediaStreamDispatcher::audio_session_id(const std::string& label,
                                            int index) {
  LabelStreamMap::iterator it = label_stream_map_.find(label);
  if (it == label_stream_map_.end())
    return StreamDeviceInfo::kNoId;

  DCHECK_GT(it->second.audio_array.size(), static_cast<size_t>(index));
  return it->second.audio_array[index].session_id;
}

bool MediaStreamDispatcher::IsStream(const std::string& label) {
  return label_stream_map_.find(label) != label_stream_map_.end();
}

int MediaStreamDispatcher::video_session_id(const std::string& label,
                                            int index) {
  LabelStreamMap::iterator it = label_stream_map_.find(label);
  if (it == label_stream_map_.end())
    return StreamDeviceInfo::kNoId;

  DCHECK_GT(it->second.video_array.size(), static_cast<size_t>(index));
  return it->second.video_array[index].session_id;
}

}  // namespace content
