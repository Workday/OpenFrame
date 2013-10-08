// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "content/renderer/media/media_stream_extra_data.h"
#include "content/renderer/media/media_stream_impl.h"
#include "content/renderer/media/mock_media_stream_dependency_factory.h"
#include "content/renderer/media/mock_media_stream_dispatcher.h"
#include "content/renderer/media/video_capture_impl_manager.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/public/platform/WebMediaStream.h"
#include "third_party/WebKit/public/platform/WebMediaStreamSource.h"
#include "third_party/WebKit/public/platform/WebMediaStreamTrack.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebVector.h"

namespace content {

class MediaStreamImplUnderTest : public MediaStreamImpl {
 public:
  enum RequestState {
    REQUEST_NOT_STARTED,
    REQUEST_NOT_COMPLETE,
    REQUEST_SUCCEEDED,
    REQUEST_FAILED,
  };

  MediaStreamImplUnderTest(MediaStreamDispatcher* media_stream_dispatcher,
                           MediaStreamDependencyFactory* dependency_factory)
      : MediaStreamImpl(NULL, media_stream_dispatcher, dependency_factory),
        state_(REQUEST_NOT_STARTED) {
  }

  void RequestUserMedia() {
    WebKit::WebUserMediaRequest user_media_request;
    state_ = REQUEST_NOT_COMPLETE;
    requestUserMedia(user_media_request);
  }

  virtual void CompleteGetUserMediaRequest(
      const WebKit::WebMediaStream& stream,
      WebKit::WebUserMediaRequest* request_info,
      bool request_succeeded) OVERRIDE {
    last_generated_stream_ = stream;
    state_ = request_succeeded ? REQUEST_SUCCEEDED : REQUEST_FAILED;
  }

  virtual WebKit::WebMediaStream GetMediaStream(
      const GURL& url) OVERRIDE {
    return last_generated_stream_;
  }

  using MediaStreamImpl::OnLocalMediaStreamStop;

  const WebKit::WebMediaStream& last_generated_stream() {
    return last_generated_stream_;
  }

  RequestState request_state() const { return state_; }

 private:
  WebKit::WebMediaStream last_generated_stream_;
  RequestState state_;
};

class MediaStreamImplTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    // Create our test object.
    ms_dispatcher_.reset(new MockMediaStreamDispatcher());
    dependency_factory_.reset(new MockMediaStreamDependencyFactory());
    ms_impl_.reset(new MediaStreamImplUnderTest(ms_dispatcher_.get(),
                                                dependency_factory_.get()));
  }

  WebKit::WebMediaStream RequestLocalMediaStream() {
    ms_impl_->RequestUserMedia();
    FakeMediaStreamDispatcherComplete();
    ChangeVideoSourceStateToLive();
    ChangeAudioSourceStateToLive();

    EXPECT_EQ(MediaStreamImplUnderTest::REQUEST_SUCCEEDED,
              ms_impl_->request_state());

    WebKit::WebMediaStream desc = ms_impl_->last_generated_stream();
    content::MediaStreamExtraData* extra_data =
        static_cast<content::MediaStreamExtraData*>(desc.extraData());
    if (!extra_data || !extra_data->stream().get()) {
      ADD_FAILURE();
      return desc;
    }

    EXPECT_EQ(1u, extra_data->stream()->GetAudioTracks().size());
    EXPECT_EQ(1u, extra_data->stream()->GetVideoTracks().size());
    EXPECT_NE(extra_data->stream()->GetAudioTracks()[0]->id(),
              extra_data->stream()->GetVideoTracks()[0]->id());
    return desc;
  }

  void FakeMediaStreamDispatcherComplete() {
    ms_impl_->OnStreamGenerated(ms_dispatcher_->request_id(),
                                ms_dispatcher_->stream_label(),
                                ms_dispatcher_->audio_array(),
                                ms_dispatcher_->video_array());
  }

  void ChangeVideoSourceStateToLive() {
    if (dependency_factory_->last_video_source() != NULL) {
      dependency_factory_->last_video_source()->SetLive();
    }
  }

  void ChangeAudioSourceStateToLive() {
    if (dependency_factory_->last_audio_source() != NULL) {
      dependency_factory_->last_audio_source()->SetLive();
    }
  }

  void ChangeVideoSourceStateToEnded() {
    if (dependency_factory_->last_video_source() != NULL) {
      dependency_factory_->last_video_source()->SetEnded();
    }
  }

  void ChangeAudioSourceStateToEnded() {
    if (dependency_factory_->last_audio_source() != NULL) {
      dependency_factory_->last_audio_source()->SetEnded();
    }
  }

 protected:
  scoped_ptr<MockMediaStreamDispatcher> ms_dispatcher_;
  scoped_ptr<MediaStreamImplUnderTest> ms_impl_;
  scoped_ptr<MockMediaStreamDependencyFactory> dependency_factory_;
};

TEST_F(MediaStreamImplTest, LocalMediaStream) {
  // Test a stream with both audio and video.
  WebKit::WebMediaStream mixed_desc = RequestLocalMediaStream();

  // Test a stream with audio only.
  WebKit::WebMediaStream audio_desc = RequestLocalMediaStream();

  // Test a stream with video only.
  WebKit::WebMediaStream video_desc = RequestLocalMediaStream();

  // Stop generated local streams.
  ms_impl_->OnLocalMediaStreamStop(mixed_desc.id().utf8());
  EXPECT_EQ(1, ms_dispatcher_->stop_stream_counter());
  ms_impl_->OnLocalMediaStreamStop(audio_desc.id().utf8());
  EXPECT_EQ(2, ms_dispatcher_->stop_stream_counter());

  // Test that the MediaStreams are deleted if the owning WebFrame is deleted.
  // In the unit test the owning frame is NULL.
  ms_impl_->FrameWillClose(NULL);
  EXPECT_EQ(3, ms_dispatcher_->stop_stream_counter());
}

// This test what happens if a source to a MediaSteam fails to start.
TEST_F(MediaStreamImplTest, MediaSourceFailToStart) {
  ms_impl_->RequestUserMedia();
  FakeMediaStreamDispatcherComplete();
  ChangeVideoSourceStateToEnded();
  ChangeAudioSourceStateToEnded();
  EXPECT_EQ(MediaStreamImplUnderTest::REQUEST_FAILED,
            ms_impl_->request_state());
  EXPECT_EQ(1, ms_dispatcher_->request_stream_counter());
  EXPECT_EQ(1, ms_dispatcher_->stop_stream_counter());
}

// This test what happens if MediaStreamImpl is deleted while the sources of a
// MediaStream is being started.
TEST_F(MediaStreamImplTest, MediaStreamImplShutDown) {
  ms_impl_->RequestUserMedia();
  FakeMediaStreamDispatcherComplete();
  EXPECT_EQ(1, ms_dispatcher_->request_stream_counter());
  EXPECT_EQ(MediaStreamImplUnderTest::REQUEST_NOT_COMPLETE,
            ms_impl_->request_state());
  ms_impl_.reset();
  ChangeAudioSourceStateToLive();
  ChangeVideoSourceStateToLive();
}

// This test what happens if the WebFrame is closed while the MediaStream is
// being generated by the MediaStreamDispatcher.
TEST_F(MediaStreamImplTest, ReloadFrameWhileGeneratingStream) {
  ms_impl_->RequestUserMedia();
  ms_impl_->FrameWillClose(NULL);
  EXPECT_EQ(1, ms_dispatcher_->request_stream_counter());
  EXPECT_EQ(0, ms_dispatcher_->stop_stream_counter());
  ChangeAudioSourceStateToLive();
  ChangeVideoSourceStateToLive();
  EXPECT_EQ(MediaStreamImplUnderTest::REQUEST_NOT_COMPLETE,
            ms_impl_->request_state());
}

// This test what happens if the WebFrame is closed while the sources are being
// started by MediaStreamDependencyFactory.
TEST_F(MediaStreamImplTest, ReloadFrameWhileGeneratingSources) {
  ms_impl_->RequestUserMedia();
  FakeMediaStreamDispatcherComplete();
  EXPECT_EQ(0, ms_dispatcher_->stop_stream_counter());
  EXPECT_EQ(1, ms_dispatcher_->request_stream_counter());
  ms_impl_->FrameWillClose(NULL);
  EXPECT_EQ(1, ms_dispatcher_->stop_stream_counter());
  ChangeAudioSourceStateToLive();
  ChangeVideoSourceStateToLive();
  EXPECT_EQ(MediaStreamImplUnderTest::REQUEST_NOT_COMPLETE,
            ms_impl_->request_state());
}

// This test what happens if stop is called on a stream after the frame has
// been reloaded.
TEST_F(MediaStreamImplTest, StopStreamAfterReload) {
  WebKit::WebMediaStream mixed_desc = RequestLocalMediaStream();
  EXPECT_EQ(0, ms_dispatcher_->stop_stream_counter());
  EXPECT_EQ(1, ms_dispatcher_->request_stream_counter());
  ms_impl_->FrameWillClose(NULL);
  EXPECT_EQ(1, ms_dispatcher_->stop_stream_counter());
  ms_impl_->OnLocalMediaStreamStop(mixed_desc.id().utf8());
  EXPECT_EQ(1, ms_dispatcher_->stop_stream_counter());
}

}  // namespace content
