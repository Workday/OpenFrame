// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/blink/multibuffer_data_source.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "media/base/media_log.h"
#include "media/blink/multibuffer_reader.h"
#include "net/base/net_errors.h"

using blink::WebFrame;

namespace {

// Minimum preload buffer.
const int64 kMinBufferPreload = 2 << 20;  // 2 Mb
// Maxmimum preload buffer.
const int64 kMaxBufferPreload = 20 << 20;  // 20 Mb

// Preload this much extra, then stop preloading until we fall below the
// kTargetSecondsBufferedAhead.
const int64 kPreloadHighExtra = 1 << 20;  // 1 Mb

// Total size of the pinned region in the cache.
const int64 kMaxBufferSize = 25 << 20;  // 25 Mb

// If bitrate is not known, use this.
const int64 kDefaultBitrate = 200 * 8 << 10;  // 200 Kbps.

// Maximum bitrate for buffer calculations.
const int64 kMaxBitrate = 20 * 8 << 20;  // 20 Mbps.

// Maximum playback rate for buffer calculations.
const double kMaxPlaybackRate = 25.0;

// Preload this many seconds of data by default.
const int64 kTargetSecondsBufferedAhead = 10;

// Keep this many seconds of data for going back by default.
const int64 kTargetSecondsBufferedBehind = 2;

}  // namespace

namespace media {

template <typename T>
T clamp(T value, T min, T max) {
  return std::max(std::min(value, max), min);
}

class MultibufferDataSource::ReadOperation {
 public:
  ReadOperation(int64 position,
                int size,
                uint8* data,
                const DataSource::ReadCB& callback);
  ~ReadOperation();

  // Runs |callback_| with the given |result|, deleting the operation
  // afterwards.
  static void Run(scoped_ptr<ReadOperation> read_op, int result);

  int64 position() { return position_; }
  int size() { return size_; }
  uint8* data() { return data_; }

 private:
  const int64 position_;
  const int size_;
  uint8* data_;
  DataSource::ReadCB callback_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(ReadOperation);
};

MultibufferDataSource::ReadOperation::ReadOperation(
    int64 position,
    int size,
    uint8* data,
    const DataSource::ReadCB& callback)
    : position_(position), size_(size), data_(data), callback_(callback) {
  DCHECK(!callback_.is_null());
}

MultibufferDataSource::ReadOperation::~ReadOperation() {
  DCHECK(callback_.is_null());
}

// static
void MultibufferDataSource::ReadOperation::Run(
    scoped_ptr<ReadOperation> read_op,
    int result) {
  base::ResetAndReturn(&read_op->callback_).Run(result);
}

MultibufferDataSource::MultibufferDataSource(
    const GURL& url,
    UrlData::CORSMode cors_mode,
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner,
    linked_ptr<UrlIndex> url_index,
    WebFrame* frame,
    MediaLog* media_log,
    BufferedDataSourceHost* host,
    const DownloadingCB& downloading_cb)
    : cors_mode_(cors_mode),
      total_bytes_(kPositionNotSpecified),
      streaming_(false),
      loading_(false),
      render_task_runner_(task_runner),
      url_index_(url_index),
      frame_(frame),
      stop_signal_received_(false),
      media_has_played_(false),
      single_origin_(true),
      cancel_on_defer_(false),
      preload_(AUTO),
      bitrate_(0),
      playback_rate_(0.0),
      media_log_(media_log),
      host_(host),
      downloading_cb_(downloading_cb),
      weak_factory_(this) {
  weak_ptr_ = weak_factory_.GetWeakPtr();
  DCHECK(host_);
  DCHECK(!downloading_cb_.is_null());
  DCHECK(render_task_runner_->BelongsToCurrentThread());
  url_data_ = url_index_->GetByUrl(url, cors_mode_);
  url_data_->Use();
  DCHECK(url_data_);
  url_data_->OnRedirect(
      base::Bind(&MultibufferDataSource::OnRedirect, weak_ptr_));
}

MultibufferDataSource::~MultibufferDataSource() {
  DCHECK(render_task_runner_->BelongsToCurrentThread());
}

bool MultibufferDataSource::media_has_played() const {
  return media_has_played_;
}

bool MultibufferDataSource::assume_fully_buffered() {
  return !url_data_->url().SchemeIsHTTPOrHTTPS();
}

void MultibufferDataSource::CreateResourceLoader(int64 first_byte_position,
                                                 int64 last_byte_position) {
  DCHECK(render_task_runner_->BelongsToCurrentThread());

  reader_.reset(new MultiBufferReader(
      url_data_->multibuffer(), first_byte_position, last_byte_position,
      base::Bind(&MultibufferDataSource::ProgressCallback, weak_ptr_)));
  UpdateBufferSizes();
}

void MultibufferDataSource::Initialize(const InitializeCB& init_cb) {
  DCHECK(render_task_runner_->BelongsToCurrentThread());
  DCHECK(!init_cb.is_null());
  DCHECK(!reader_.get());

  init_cb_ = init_cb;

  CreateResourceLoader(0, kPositionNotSpecified);

  // We're not allowed to call Wait() if data is already available.
  if (reader_->Available()) {
    render_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&MultibufferDataSource::StartCallback, weak_ptr_));
  } else {
    reader_->Wait(1,
                  base::Bind(&MultibufferDataSource::StartCallback, weak_ptr_));
  }
  UpdateLoadingState();
}

void MultibufferDataSource::OnRedirect(
    const scoped_refptr<UrlData>& destination) {
  if (!destination) {
    // A failure occured.
    if (!init_cb_.is_null()) {
      render_task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&MultibufferDataSource::StartCallback, weak_ptr_));
    } else {
      {
        base::AutoLock auto_lock(lock_);
        StopInternal_Locked();
      }
      StopLoader();
    }
    return;
  }
  if (url_data_->url().GetOrigin() != destination->url().GetOrigin()) {
    single_origin_ = false;
  }
  reader_.reset(nullptr);
  url_data_ = destination;

  if (url_data_) {
    url_data_->OnRedirect(
        base::Bind(&MultibufferDataSource::OnRedirect, weak_ptr_));

    if (!init_cb_.is_null()) {
      CreateResourceLoader(0, kPositionNotSpecified);
      if (reader_->Available()) {
        render_task_runner_->PostTask(
            FROM_HERE,
            base::Bind(&MultibufferDataSource::StartCallback, weak_ptr_));
      } else {
        reader_->Wait(
            1, base::Bind(&MultibufferDataSource::StartCallback, weak_ptr_));
      }
    } else if (read_op_) {
      CreateResourceLoader(read_op_->position(), kPositionNotSpecified);
      if (reader_->Available()) {
        render_task_runner_->PostTask(
            FROM_HERE, base::Bind(&MultibufferDataSource::ReadTask, weak_ptr_));
      } else {
        reader_->Wait(1,
                      base::Bind(&MultibufferDataSource::ReadTask, weak_ptr_));
      }
    }
  }
}

void MultibufferDataSource::SetPreload(Preload preload) {
  DCHECK(render_task_runner_->BelongsToCurrentThread());
  preload_ = preload;
  UpdateBufferSizes();
}

bool MultibufferDataSource::HasSingleOrigin() {
  DCHECK(render_task_runner_->BelongsToCurrentThread());
  DCHECK(init_cb_.is_null() && reader_.get())
      << "Initialize() must complete before calling HasSingleOrigin()";
  return single_origin_;
}

bool MultibufferDataSource::DidPassCORSAccessCheck() const {
  if (cors_mode_ == UrlData::CORS_UNSPECIFIED)
    return false;
  // If init_cb is set, we initialization is not finished yet.
  if (!init_cb_.is_null())
    return false;
  // Loader will be false if there was a failure.
  if (!reader_)
    return false;
  return true;
}

void MultibufferDataSource::Abort() {
  DCHECK(render_task_runner_->BelongsToCurrentThread());
  {
    base::AutoLock auto_lock(lock_);
    StopInternal_Locked();
  }
  StopLoader();
  frame_ = NULL;
}

void MultibufferDataSource::MediaPlaybackRateChanged(double playback_rate) {
  DCHECK(render_task_runner_->BelongsToCurrentThread());
  DCHECK(reader_.get());

  if (playback_rate < 0.0)
    return;

  playback_rate_ = playback_rate;
  cancel_on_defer_ = false;
  UpdateBufferSizes();
}

void MultibufferDataSource::MediaIsPlaying() {
  DCHECK(render_task_runner_->BelongsToCurrentThread());
  media_has_played_ = true;
  cancel_on_defer_ = false;
  paused_ = false;
  // Once we start playing, we need preloading.
  preload_ = AUTO;
  UpdateBufferSizes();
}

void MultibufferDataSource::MediaIsPaused() {
  DCHECK(render_task_runner_->BelongsToCurrentThread());
  paused_ = true;
  UpdateBufferSizes();
}

/////////////////////////////////////////////////////////////////////////////
// DataSource implementation.
void MultibufferDataSource::Stop() {
  {
    base::AutoLock auto_lock(lock_);
    StopInternal_Locked();
  }

  render_task_runner_->PostTask(FROM_HERE,
                                base::Bind(&MultibufferDataSource::StopLoader,
                                           weak_factory_.GetWeakPtr()));
}

void MultibufferDataSource::SetBitrate(int bitrate) {
  render_task_runner_->PostTask(
      FROM_HERE, base::Bind(&MultibufferDataSource::SetBitrateTask,
                            weak_factory_.GetWeakPtr(), bitrate));
}

void MultibufferDataSource::OnBufferingHaveEnough() {
  DCHECK(render_task_runner_->BelongsToCurrentThread());
  if (reader_ && preload_ == METADATA && !media_has_played_ && !IsStreaming()) {
    cancel_on_defer_ = true;
    if (!loading_)
      reader_.reset(nullptr);
  }
}

int64_t MultibufferDataSource::GetMemoryUsage() const {
  // TODO(hubbe): Make more accurate when url_data_ is shared.
  return url_data_->CachedSize()
         << url_data_->multibuffer()->block_size_shift();
}

void MultibufferDataSource::Read(int64 position,
                                 int size,
                                 uint8* data,
                                 const DataSource::ReadCB& read_cb) {
  DVLOG(1) << "Read: " << position << " offset, " << size << " bytes";
  // Reading is not allowed until after initialization.
  DCHECK(init_cb_.is_null());
  DCHECK(!read_cb.is_null());

  {
    base::AutoLock auto_lock(lock_);
    DCHECK(!read_op_);

    if (stop_signal_received_) {
      read_cb.Run(kReadError);
      return;
    }

    read_op_.reset(new ReadOperation(position, size, data, read_cb));
  }

  render_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&MultibufferDataSource::ReadTask, weak_factory_.GetWeakPtr()));
}

bool MultibufferDataSource::GetSize(int64* size_out) {
  *size_out = url_data_->length();
  return *size_out != kPositionNotSpecified;
}

bool MultibufferDataSource::IsStreaming() {
  return streaming_;
}

/////////////////////////////////////////////////////////////////////////////
// This method is the place where actual read happens,
void MultibufferDataSource::ReadTask() {
  DCHECK(render_task_runner_->BelongsToCurrentThread());

  base::AutoLock auto_lock(lock_);
  int bytes_read = 0;
  if (stop_signal_received_)
    return;
  DCHECK(read_op_);
  DCHECK(read_op_->size());

  if (!reader_) {
    CreateResourceLoader(read_op_->position(), kPositionNotSpecified);
  } else {
    reader_->Seek(read_op_->position());
  }

  int64_t available = reader_->Available();
  if (available < 0) {
    // A failure has occured.
    ReadOperation::Run(read_op_.Pass(), kReadError);
    return;
  }
  if (available) {
    bytes_read =
        static_cast<int>(std::min<int64_t>(available, read_op_->size()));
    bytes_read = reader_->TryRead(read_op_->data(), bytes_read);
    ReadOperation::Run(read_op_.Pass(), bytes_read);
  } else {
    reader_->Wait(1, base::Bind(&MultibufferDataSource::ReadTask,
                                weak_factory_.GetWeakPtr()));
    UpdateLoadingState();
  }
}

void MultibufferDataSource::StopInternal_Locked() {
  lock_.AssertAcquired();
  if (stop_signal_received_)
    return;

  stop_signal_received_ = true;

  // Initialize() isn't part of the DataSource interface so don't call it in
  // response to Stop().
  init_cb_.Reset();

  if (read_op_)
    ReadOperation::Run(read_op_.Pass(), kReadError);
}

void MultibufferDataSource::StopLoader() {
  DCHECK(render_task_runner_->BelongsToCurrentThread());
  reader_.reset(nullptr);
}

void MultibufferDataSource::SetBitrateTask(int bitrate) {
  DCHECK(render_task_runner_->BelongsToCurrentThread());
  DCHECK(reader_.get());

  bitrate_ = bitrate;
  UpdateBufferSizes();
}

/////////////////////////////////////////////////////////////////////////////
// BufferedResourceLoader callback methods.
void MultibufferDataSource::StartCallback() {
  DCHECK(render_task_runner_->BelongsToCurrentThread());

  if (init_cb_.is_null()) {
    reader_.reset();
    return;
  }

  // All responses must be successful. Resources that are assumed to be fully
  // buffered must have a known content length.
  bool success = reader_ && reader_->Available() > 0 && url_data_ &&
                 (!assume_fully_buffered() ||
                  url_data_->length() != kPositionNotSpecified);

  if (success) {
    total_bytes_ = url_data_->length();
    streaming_ =
        !assume_fully_buffered() && (total_bytes_ == kPositionNotSpecified ||
                                     !url_data_->range_supported());

    media_log_->SetDoubleProperty("total_bytes",
                                  static_cast<double>(total_bytes_));
    media_log_->SetBooleanProperty("streaming", streaming_);
  } else {
    reader_.reset(nullptr);
  }

  // TODO(scherkus): we shouldn't have to lock to signal host(), see
  // http://crbug.com/113712 for details.
  base::AutoLock auto_lock(lock_);
  if (stop_signal_received_)
    return;

  if (success) {
    if (total_bytes_ != kPositionNotSpecified) {
      host_->SetTotalBytes(total_bytes_);
      if (assume_fully_buffered())
        host_->AddBufferedByteRange(0, total_bytes_);
    }

    // Progress callback might be called after the start callback,
    // make sure that we update single_origin_ now.
    media_log_->SetBooleanProperty("single_origin", single_origin_);
    media_log_->SetBooleanProperty("passed_cors_access_check",
                                   DidPassCORSAccessCheck());
    media_log_->SetBooleanProperty("range_header_supported",
                                   url_data_->range_supported());
  }

  UpdateLoadingState();
  render_task_runner_->PostTask(
      FROM_HERE, base::Bind(base::ResetAndReturn(&init_cb_), success));
}

void MultibufferDataSource::ProgressCallback(int64 begin, int64 end) {
  DCHECK(render_task_runner_->BelongsToCurrentThread());

  if (assume_fully_buffered())
    return;

  if (end > begin) {
    // TODO(scherkus): we shouldn't have to lock to signal host(), see
    // http://crbug.com/113712 for details.
    base::AutoLock auto_lock(lock_);
    if (stop_signal_received_)
      return;

    host_->AddBufferedByteRange(begin, end);
  }

  UpdateLoadingState();
}

void MultibufferDataSource::UpdateLoadingState() {
  // Update loading state.
  if ((!!reader_ && reader_->IsLoading()) != loading_) {
    loading_ = !loading_;

    if (!loading_ && cancel_on_defer_) {
      reader_.reset(nullptr);
    }

    // Callback could kill us, be sure to call it last.
    downloading_cb_.Run(loading_);
  }
}

void MultibufferDataSource::UpdateBufferSizes() {
  if (!reader_)
    return;

  if (!assume_fully_buffered()) {
    // If the playback has started and we're paused, then try to load as much as
    // possible, assuming that the file is cacheable. (If not, why bother?)
    if (media_has_played_ && paused_ && url_data_ &&
        url_data_->range_supported() && url_data_->cacheable()) {
      reader_->SetPreload(1LL << 40, 1LL << 40);  // 1 Tb
      return;
    }
  }

  // Use a default bit rate if unknown and clamp to prevent overflow.
  int64 bitrate = clamp<int64>(bitrate_, 0, kMaxBitrate);
  if (bitrate == 0)
    bitrate = kDefaultBitrate;

  // Only scale the buffer window for playback rates greater than 1.0 in
  // magnitude and clamp to prevent overflow.
  double playback_rate = playback_rate_;

  playback_rate = std::max(playback_rate, 1.0);
  playback_rate = std::min(playback_rate, kMaxPlaybackRate);

  int64 bytes_per_second = (bitrate / 8.0) * playback_rate;

  int64 preload = clamp(kTargetSecondsBufferedAhead * bytes_per_second,
                        kMinBufferPreload, kMaxBufferPreload);
  int64 back_buffer = clamp(kTargetSecondsBufferedBehind * bytes_per_second,
                            kMinBufferPreload, kMaxBufferPreload);
  int64 pin_forwards = kMaxBufferSize - back_buffer;
  DCHECK_LE(preload_ + kPreloadHighExtra, pin_forwards);
  reader_->SetMaxBuffer(back_buffer, pin_forwards);

  if (preload_ == METADATA) {
    reader_->SetPreload(0, 0);
  } else {
    reader_->SetPreload(preload + kPreloadHighExtra, preload);
  }
}

}  // namespace media
