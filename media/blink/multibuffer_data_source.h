// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BLINK_MULTIBUFFER_DATA_SOURCE_H_
#define MEDIA_BLINK_MULTIBUFFER_DATA_SOURCE_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "media/base/data_source.h"
#include "media/base/ranges.h"
#include "media/blink/buffered_data_source.h"
#include "media/blink/media_blink_export.h"
#include "media/blink/url_index.h"
#include "url/gurl.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace media {
class MediaLog;
class MultiBufferReader;

// A data source capable of loading URLs and buffering the data using an
// in-memory sliding window.
//
// MultibufferDataSource must be created and destroyed on the thread associated
// with the |task_runner| passed in the constructor.
class MEDIA_BLINK_EXPORT MultibufferDataSource
    : NON_EXPORTED_BASE(public BufferedDataSourceInterface) {
 public:
  typedef base::Callback<void(bool)> DownloadingCB;

  // |url| and |cors_mode| are passed to the object. Buffered byte range changes
  // will be reported to |host|. |downloading_cb| will be called whenever the
  // downloading/paused state of the source changes.
  MultibufferDataSource(
      const GURL& url,
      UrlData::CORSMode cors_mode,
      const scoped_refptr<base::SingleThreadTaskRunner>& task_runner,
      linked_ptr<UrlIndex> url_index,
      blink::WebFrame* frame,
      MediaLog* media_log,
      BufferedDataSourceHost* host,
      const DownloadingCB& downloading_cb);
  ~MultibufferDataSource() override;

  // Executes |init_cb| with the result of initialization when it has completed.
  //
  // Method called on the render thread.
  void Initialize(const InitializeCB& init_cb) override;

  // Adjusts the buffering algorithm based on the given preload value.
  void SetPreload(Preload preload) override;

  // Returns true if the media resource has a single origin, false otherwise.
  // Only valid to call after Initialize() has completed.
  //
  // Method called on the render thread.
  bool HasSingleOrigin() override;

  // Returns true if the media resource passed a CORS access control check.
  bool DidPassCORSAccessCheck() const override;

  // Cancels initialization, any pending loaders, and any pending read calls
  // from the demuxer. The caller is expected to release its reference to this
  // object and never call it again.
  //
  // Method called on the render thread.
  void Abort() override;

  // Notifies changes in playback state for controlling media buffering
  // behavior.
  void MediaPlaybackRateChanged(double playback_rate) override;
  void MediaIsPlaying() override;
  void MediaIsPaused() override;
  bool media_has_played() const override;

  // Returns true if the resource is local.
  bool assume_fully_buffered() override;

  // Cancels any open network connections once reaching the deferred state for
  // preload=metadata, non-streaming resources that have not started playback.
  // If already deferred, connections will be immediately closed.
  void OnBufferingHaveEnough() override;

  int64_t GetMemoryUsage() const override;

  // DataSource implementation.
  // Called from demuxer thread.
  void Stop() override;

  void Read(int64 position,
            int size,
            uint8* data,
            const DataSource::ReadCB& read_cb) override;
  bool GetSize(int64* size_out) override;
  bool IsStreaming() override;
  void SetBitrate(int bitrate) override;

 protected:
  void OnRedirect(const scoped_refptr<UrlData>& destination);

  // A factory method to create a BufferedResourceLoader based on the read
  // parameters.
  void CreateResourceLoader(int64 first_byte_position,
                            int64 last_byte_position);

  friend class MultibufferDataSourceTest;

  // Task posted to perform actual reading on the render thread.
  void ReadTask();

  // Cancels oustanding callbacks and sets |stop_signal_received_|. Safe to call
  // from any thread.
  void StopInternal_Locked();

  // Stops |reader_| if present. Used by Abort() and Stop().
  void StopLoader();

  // Tells |reader_| the bitrate of the media.
  void SetBitrateTask(int bitrate);

  // BufferedResourceLoader::Start() callback for initial load.
  void StartCallback();

  // Check if we've moved to a new url and update has_signgle_origin_.
  void UpdateSingleOrigin();

  // MultiBufferReader progress callback.
  void ProgressCallback(int64 begin, int64 end);

  // call downloading_cb_ if needed.
  void UpdateLoadingState();

  // Update |reader_|'s preload and buffer settings.
  void UpdateBufferSizes();

  // crossorigin attribute on the corresponding HTML media element, if any.
  UrlData::CORSMode cors_mode_;

  // URL of the resource requested.
  scoped_refptr<UrlData> url_data_;

  // The total size of the resource. Set during StartCallback() if the size is
  // known, otherwise it will remain kPositionNotSpecified until the size is
  // determined by reaching EOF.
  int64 total_bytes_;

  // This value will be true if this data source can only support streaming.
  // i.e. range request is not supported.
  bool streaming_;

  bool loading_;

  // The task runner of the render thread.
  const scoped_refptr<base::SingleThreadTaskRunner> render_task_runner_;

  // Shared cache.
  linked_ptr<UrlIndex> url_index_;

  // A webframe for loading.
  blink::WebFrame* frame_;

  // A resource reader for the media resource.
  scoped_ptr<MultiBufferReader> reader_;

  // Callback method from the pipeline for initialization.
  InitializeCB init_cb_;

  // Read parameters received from the Read() method call. Must be accessed
  // under |lock_|.
  class ReadOperation;
  scoped_ptr<ReadOperation> read_op_;

  // Protects |stop_signal_received_| and |read_op_|.
  base::Lock lock_;

  // Whether we've been told to stop via Abort() or Stop().
  bool stop_signal_received_;

  // This variable is true when the user has requested the video to play at
  // least once.
  bool media_has_played_;

  // Are we currently paused.
  bool paused_;

  // As we follow redirects, we set this variable to false if redirects
  // go between different origins.
  bool single_origin_;

  // Close the connection when we have enough data.
  bool cancel_on_defer_;

  // This variable holds the value of the preload attribute for the video
  // element.
  Preload preload_;

  // Bitrate of the content, 0 if unknown.
  int bitrate_;

  // Current playback rate.
  double playback_rate_;

  scoped_refptr<MediaLog> media_log_;

  // Host object to report buffered byte range changes to.
  BufferedDataSourceHost* host_;

  DownloadingCB downloading_cb_;

  // The original URL of the first response. If the request is redirected to
  // another URL it is the URL after redirected. If the response is generated in
  // a Service Worker this URL is empty. MultibufferDataSource checks the
  // original URL of each successive response. If the origin URL of it is
  // different from the original URL of the first response, it is treated
  // as an error.
  GURL response_original_url_;

  // Disallow rebinding WeakReference ownership to a different thread by keeping
  // a persistent reference. This avoids problems with the thread-safety of
  // reaching into this class from multiple threads to attain a WeakPtr.
  base::WeakPtr<MultibufferDataSource> weak_ptr_;
  base::WeakPtrFactory<MultibufferDataSource> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(MultibufferDataSource);
};

}  // namespace media

#endif  // MEDIA_BLINK_MULTIBUFFER_DATA_SOURCE_H_
