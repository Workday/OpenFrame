// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/tab_utils.h"

#include "chrome/browser/media/audio_stream_indicator.h"
#include "chrome/browser/media/media_capture_devices_dispatcher.h"
#include "chrome/browser/media/media_stream_capture_indicator.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"

namespace chrome {

bool ShouldShowProjectingIndicator(content::WebContents* contents) {
  scoped_refptr<MediaStreamCaptureIndicator> indicator =
      MediaCaptureDevicesDispatcher::GetInstance()->
          GetMediaStreamCaptureIndicator();
  return indicator->IsBeingMirrored(contents);
}

bool ShouldShowRecordingIndicator(content::WebContents* contents) {
  scoped_refptr<MediaStreamCaptureIndicator> indicator =
      MediaCaptureDevicesDispatcher::GetInstance()->
          GetMediaStreamCaptureIndicator();
  // The projecting indicator takes precedence over the recording indicator, but
  // if we are projecting and we don't handle the projecting case we want to
  // still show the recording indicator.
  return indicator->IsCapturingUserMedia(contents) ||
         indicator->IsBeingMirrored(contents);
}

bool IsPlayingAudio(content::WebContents* contents) {
  AudioStreamIndicator* audio_indicator =
      MediaCaptureDevicesDispatcher::GetInstance()->GetAudioStreamIndicator()
          .get();
  return audio_indicator->IsPlayingAudio(contents);
}

bool IsCapturingVideo(content::WebContents* contents) {
  scoped_refptr<MediaStreamCaptureIndicator> indicator =
      MediaCaptureDevicesDispatcher::GetInstance()->
          GetMediaStreamCaptureIndicator();
  return indicator->IsCapturingVideo(contents);
}

bool IsCapturingAudio(content::WebContents* contents) {
  scoped_refptr<MediaStreamCaptureIndicator> indicator =
      MediaCaptureDevicesDispatcher::GetInstance()->
          GetMediaStreamCaptureIndicator();
  return indicator->IsCapturingAudio(contents);
}

}  // namespace chrome
