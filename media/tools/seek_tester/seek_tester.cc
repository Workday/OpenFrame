// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This standalone binary is a helper for diagnosing seek behavior of the
// demuxer setup in media/ code.  It answers the question: "if I ask the demuxer
// to Seek to X ms, where will it actually seek to? (necessitating
// frame-dropping until the original seek target is reached)".  Sample run:
//
// $ ./out/Debug/seek_tester .../LayoutTests/media/content/test.ogv 6300
// [0207/130327:INFO:seek_tester.cc(63)] Requested: 6123ms
// [0207/130327:INFO:seek_tester.cc(68)]   audio seeked to: 5526ms
// [0207/130327:INFO:seek_tester.cc(74)]   video seeked to: 5577ms


#include "base/at_exit.h"
#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_number_conversions.h"
#include "media/base/media.h"
#include "media/base/media_log.h"
#include "media/filters/ffmpeg_demuxer.h"
#include "media/filters/file_data_source.h"

class DemuxerHostImpl : public media::DemuxerHost {
 public:
  // DataSourceHost implementation.
  virtual void SetTotalBytes(int64 total_bytes) OVERRIDE {}
  virtual void AddBufferedByteRange(int64 start, int64 end) OVERRIDE {}
  virtual void AddBufferedTimeRange(base::TimeDelta start,
                                    base::TimeDelta end) OVERRIDE {}

  // DemuxerHost implementation.
  virtual void SetDuration(base::TimeDelta duration) OVERRIDE {}
  virtual void OnDemuxerError(media::PipelineStatus error) OVERRIDE {}
};

void QuitMessageLoop(base::MessageLoop* loop, media::PipelineStatus status) {
  CHECK_EQ(status, media::PIPELINE_OK);
  loop->PostTask(FROM_HERE, base::MessageLoop::QuitClosure());
}

void TimestampExtractor(uint64* timestamp_ms,
                        base::MessageLoop* loop,
                        media::DemuxerStream::Status status,
                        const scoped_refptr<media::DecoderBuffer>& buffer) {
  CHECK_EQ(status, media::DemuxerStream::kOk);
  if (buffer->timestamp() == media::kNoTimestamp())
    *timestamp_ms = -1;
  else
    *timestamp_ms = buffer->timestamp().InMillisecondsF();
  loop->PostTask(FROM_HERE, base::MessageLoop::QuitClosure());
}

static void NeedKey(const std::string& type, scoped_ptr<uint8[]> init_data,
             int init_data_size) {
  LOG(INFO) << "File is encrypted.";
}

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  media::InitializeMediaLibraryForTesting();

  CHECK_EQ(argc, 3) << "\nUsage: " << argv[0] << " <file> <seekTimeInMs>";
  uint64 seek_target_ms;
  CHECK(base::StringToUint64(argv[2], &seek_target_ms));
  scoped_ptr<media::FileDataSource> file_data_source(
      new media::FileDataSource());
  CHECK(file_data_source->Initialize(base::FilePath::FromUTF8Unsafe(argv[1])));

  DemuxerHostImpl host;
  base::MessageLoop loop;
  media::PipelineStatusCB quitter = base::Bind(&QuitMessageLoop, &loop);
  media::FFmpegNeedKeyCB need_key_cb = base::Bind(&NeedKey);
  scoped_ptr<media::FFmpegDemuxer> demuxer(
      new media::FFmpegDemuxer(loop.message_loop_proxy(),
                               file_data_source.get(),
                               need_key_cb,
                               new media::MediaLog()));
  demuxer->Initialize(&host, quitter);
  loop.Run();

  demuxer->Seek(base::TimeDelta::FromMilliseconds(seek_target_ms), quitter);
  loop.Run();

  uint64 audio_seeked_to_ms;
  uint64 video_seeked_to_ms;
  media::DemuxerStream* audio_stream =
      demuxer->GetStream(media::DemuxerStream::AUDIO);
  media::DemuxerStream* video_stream =
      demuxer->GetStream(media::DemuxerStream::VIDEO);
  LOG(INFO) << "Requested: " << seek_target_ms << "ms";
  if (audio_stream) {
    audio_stream->Read(base::Bind(
        &TimestampExtractor, &audio_seeked_to_ms, &loop));
    loop.Run();
    LOG(INFO) << "  audio seeked to: " << audio_seeked_to_ms << "ms";
  }
  if (video_stream) {
    video_stream->Read(
        base::Bind(&TimestampExtractor, &video_seeked_to_ms, &loop));
    loop.Run();
    LOG(INFO) << "  video seeked to: " << video_seeked_to_ms << "ms";
  }

  demuxer->Stop(base::Bind(&base::MessageLoop::Quit, base::Unretained(&loop)));
  loop.Run();

  return 0;
}
