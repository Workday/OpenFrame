// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cast/sender/vp8_encoder.h"

#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "media/base/video_frame.h"
#include "media/cast/cast_defines.h"
#include "media/cast/constants.h"
#include "third_party/libvpx_new/source/libvpx/vpx/vp8cx.h"

namespace media {
namespace cast {

namespace {

// After a pause in the video stream, what is the maximum duration amount to
// pass to the encoder for the next frame (in terms of 1/max_fps sized periods)?
// This essentially controls the encoded size of the first frame that follows a
// pause in the video stream.
const int kRestartFramePeriods = 3;

}  // namespace

Vp8Encoder::Vp8Encoder(const VideoSenderConfig& video_config)
    : cast_config_(video_config),
      key_frame_requested_(true),
      bitrate_kbit_(cast_config_.start_bitrate / 1000),
      last_encoded_frame_id_(kFirstFrameId - 1),
      has_seen_zero_length_encoded_frame_(false) {
  config_.g_timebase.den = 0;  // Not initialized.

  thread_checker_.DetachFromThread();
}

Vp8Encoder::~Vp8Encoder() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (is_initialized())
    vpx_codec_destroy(&encoder_);
}

void Vp8Encoder::Initialize() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!is_initialized());
  // The encoder will be created/configured when the first frame encode is
  // requested.
}

void Vp8Encoder::ConfigureForNewFrameSize(const gfx::Size& frame_size) {
  if (is_initialized()) {
    // Workaround for VP8 bug: If the new size is strictly less-than-or-equal to
    // the old size, in terms of area, the existing encoder instance can
    // continue.  Otherwise, completely tear-down and re-create a new encoder to
    // avoid a shutdown crash.
    if (frame_size.GetArea() <= gfx::Size(config_.g_w, config_.g_h).GetArea()) {
      DVLOG(1) << "Continuing to use existing encoder at smaller frame size: "
               << gfx::Size(config_.g_w, config_.g_h).ToString() << " --> "
               << frame_size.ToString();
      config_.g_w = frame_size.width();
      config_.g_h = frame_size.height();
      if (vpx_codec_enc_config_set(&encoder_, &config_) == VPX_CODEC_OK)
        return;
      DVLOG(1) << "libvpx rejected the attempt to use a smaller frame size in "
                  "the current instance.";
    }

    DVLOG(1) << "Destroying/Re-Creating encoder for larger frame size: "
             << gfx::Size(config_.g_w, config_.g_h).ToString() << " --> "
             << frame_size.ToString();
    vpx_codec_destroy(&encoder_);
  } else {
    DVLOG(1) << "Creating encoder for the first frame; size: "
             << frame_size.ToString();
  }

  // Populate encoder configuration with default values.
  CHECK_EQ(vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &config_, 0),
           VPX_CODEC_OK);

  config_.g_threads = cast_config_.number_of_encode_threads;
  config_.g_w = frame_size.width();
  config_.g_h = frame_size.height();
  // Set the timebase to match that of base::TimeDelta.
  config_.g_timebase.num = 1;
  config_.g_timebase.den = base::Time::kMicrosecondsPerSecond;

  // |g_pass| and |g_lag_in_frames| must be "one pass" and zero, respectively,
  // in order for VP8 to support changing frame sizes during encoding:
  config_.g_pass = VPX_RC_ONE_PASS;
  config_.g_lag_in_frames = 0;  // Immediate data output for each frame.

  // Rate control settings.
  config_.rc_dropframe_thresh = 0;  // The encoder may not drop any frames.
  config_.rc_resize_allowed = 0;  // TODO(miu): Why not?  Investigate this.
  config_.rc_end_usage = VPX_CBR;
  config_.rc_target_bitrate = bitrate_kbit_;
  config_.rc_min_quantizer = cast_config_.min_qp;
  config_.rc_max_quantizer = cast_config_.max_qp;
  // TODO(miu): Revisit these now that the encoder is being successfully
  // micro-managed.
  config_.rc_undershoot_pct = 100;
  config_.rc_overshoot_pct = 15;
  // TODO(miu): Document why these rc_buf_*_sz values were chosen and/or
  // research for better values.  Should they be computed from the target
  // playout delay?
  config_.rc_buf_initial_sz = 500;
  config_.rc_buf_optimal_sz = 600;
  config_.rc_buf_sz = 1000;

  config_.kf_mode = VPX_KF_DISABLED;

  vpx_codec_flags_t flags = 0;
  CHECK_EQ(vpx_codec_enc_init(&encoder_, vpx_codec_vp8_cx(), &config_, flags),
           VPX_CODEC_OK);

  // Raise the threshold for considering macroblocks as static.  The default is
  // zero, so this setting makes the encoder less sensitive to motion.  This
  // lowers the probability of needing to utilize more CPU to search for motion
  // vectors.
  CHECK_EQ(vpx_codec_control(&encoder_, VP8E_SET_STATIC_THRESHOLD, 1),
           VPX_CODEC_OK);

  // Improve quality by enabling sets of codec features that utilize more CPU.
  // The default is zero, with increasingly more CPU to be used as the value is
  // more negative.
  // TODO(miu): Document why this value was chosen and expected behaviors.
  // Should this be dynamic w.r.t. hardware performance?
  CHECK_EQ(vpx_codec_control(&encoder_, VP8E_SET_CPUUSED, -6), VPX_CODEC_OK);
}

void Vp8Encoder::Encode(const scoped_refptr<media::VideoFrame>& video_frame,
                        const base::TimeTicks& reference_time,
                        SenderEncodedFrame* encoded_frame) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(encoded_frame);

  // Note: This is used to compute the |deadline_utilization| and so it uses the
  // real-world clock instead of the CastEnvironment clock, the latter of which
  // might be simulated.
  const base::TimeTicks start_time = base::TimeTicks::Now();

  // Initialize on-demand.  Later, if the video frame size has changed, update
  // the encoder configuration.
  const gfx::Size frame_size = video_frame->visible_rect().size();
  if (!is_initialized() || gfx::Size(config_.g_w, config_.g_h) != frame_size)
    ConfigureForNewFrameSize(frame_size);

  // Wrapper for vpx_codec_encode() to access the YUV data in the |video_frame|.
  // Only the VISIBLE rectangle within |video_frame| is exposed to the codec.
  vpx_image_t vpx_image;
  vpx_image_t* const result = vpx_img_wrap(
      &vpx_image,
      VPX_IMG_FMT_I420,
      frame_size.width(),
      frame_size.height(),
      1,
      video_frame->data(VideoFrame::kYPlane));
  DCHECK_EQ(result, &vpx_image);
  vpx_image.planes[VPX_PLANE_Y] =
      video_frame->visible_data(VideoFrame::kYPlane);
  vpx_image.planes[VPX_PLANE_U] =
      video_frame->visible_data(VideoFrame::kUPlane);
  vpx_image.planes[VPX_PLANE_V] =
      video_frame->visible_data(VideoFrame::kVPlane);
  vpx_image.stride[VPX_PLANE_Y] = video_frame->stride(VideoFrame::kYPlane);
  vpx_image.stride[VPX_PLANE_U] = video_frame->stride(VideoFrame::kUPlane);
  vpx_image.stride[VPX_PLANE_V] = video_frame->stride(VideoFrame::kVPlane);

  // The frame duration given to the VP8 codec affects a number of important
  // behaviors, including: per-frame bandwidth, CPU time spent encoding,
  // temporal quality trade-offs, and key/golden/alt-ref frame generation
  // intervals.  Bound the prediction to account for the fact that the frame
  // rate can be highly variable, including long pauses in the video stream.
  const base::TimeDelta minimum_frame_duration =
      base::TimeDelta::FromSecondsD(1.0 / cast_config_.max_frame_rate);
  const base::TimeDelta maximum_frame_duration =
      base::TimeDelta::FromSecondsD(static_cast<double>(kRestartFramePeriods) /
                                        cast_config_.max_frame_rate);
  base::TimeDelta predicted_frame_duration;
  if (!video_frame->metadata()->GetTimeDelta(
          media::VideoFrameMetadata::FRAME_DURATION,
          &predicted_frame_duration) ||
      predicted_frame_duration <= base::TimeDelta()) {
    // The source of the video frame did not provide the frame duration.  Use
    // the actual amount of time between the current and previous frame as a
    // prediction for the next frame's duration.
    predicted_frame_duration = video_frame->timestamp() - last_frame_timestamp_;
  }
  predicted_frame_duration =
      std::max(minimum_frame_duration,
               std::min(maximum_frame_duration, predicted_frame_duration));
  last_frame_timestamp_ = video_frame->timestamp();

  // Encode the frame.  The presentation time stamp argument here is fixed to
  // zero to force the encoder to base its single-frame bandwidth calculations
  // entirely on |predicted_frame_duration| and the target bitrate setting being
  // micro-managed via calls to UpdateRates().
  CHECK_EQ(vpx_codec_encode(&encoder_, &vpx_image, 0,
                            predicted_frame_duration.InMicroseconds(),
                            key_frame_requested_ ? VPX_EFLAG_FORCE_KF : 0,
                            VPX_DL_REALTIME),
           VPX_CODEC_OK)
      << "BUG: Invalid arguments passed to vpx_codec_encode().";

  // Pull data from the encoder, populating a new EncodedFrame.
  encoded_frame->frame_id = ++last_encoded_frame_id_;
  const vpx_codec_cx_pkt_t* pkt = NULL;
  vpx_codec_iter_t iter = NULL;
  while ((pkt = vpx_codec_get_cx_data(&encoder_, &iter)) != NULL) {
    if (pkt->kind != VPX_CODEC_CX_FRAME_PKT)
      continue;
    if (pkt->data.frame.flags & VPX_FRAME_IS_KEY) {
      // TODO(hubbe): Replace "dependency" with a "bool is_key_frame".
      encoded_frame->dependency = EncodedFrame::KEY;
      encoded_frame->referenced_frame_id = encoded_frame->frame_id;
    } else {
      encoded_frame->dependency = EncodedFrame::DEPENDENT;
      // Frame dependencies could theoretically be relaxed by looking for the
      // VPX_FRAME_IS_DROPPABLE flag, but in recent testing (Oct 2014), this
      // flag never seems to be set.
      encoded_frame->referenced_frame_id = last_encoded_frame_id_ - 1;
    }
    encoded_frame->rtp_timestamp =
        TimeDeltaToRtpDelta(video_frame->timestamp(), kVideoFrequency);
    encoded_frame->reference_time = reference_time;
    encoded_frame->data.assign(
        static_cast<const uint8*>(pkt->data.frame.buf),
        static_cast<const uint8*>(pkt->data.frame.buf) + pkt->data.frame.sz);
    break;  // Done, since all data is provided in one CX_FRAME_PKT packet.
  }
  DCHECK(!encoded_frame->data.empty())
      << "BUG: Encoder must provide data since lagged encoding is disabled.";

  // TODO(miu): Determine when/why encoding can produce zero-length data,
  // which causes crypto crashes.  http://crbug.com/519022
  if (!has_seen_zero_length_encoded_frame_ && encoded_frame->data.empty()) {
    has_seen_zero_length_encoded_frame_ = true;

    const char kZeroEncodeDetails[] = "zero-encode-details";
    const std::string details = base::StringPrintf(
        "SV/%c,id=%" PRIu32 ",rtp=%" PRIu32 ",br=%d,kfr=%c",
        encoded_frame->dependency == EncodedFrame::KEY ? 'K' : 'D',
        encoded_frame->frame_id, encoded_frame->rtp_timestamp,
        static_cast<int>(config_.rc_target_bitrate),
        key_frame_requested_ ? 'Y' : 'N');
    base::debug::SetCrashKeyValue(kZeroEncodeDetails, details);
    // Please forward crash reports to http://crbug.com/519022:
    base::debug::DumpWithoutCrashing();
    base::debug::ClearCrashKey(kZeroEncodeDetails);
  }

  // Compute deadline utilization as the real-world time elapsed divided by the
  // frame duration.
  const base::TimeDelta processing_time = base::TimeTicks::Now() - start_time;
  encoded_frame->deadline_utilization =
      processing_time.InSecondsF() / predicted_frame_duration.InSecondsF();

  // Compute lossy utilization.  The VP8 encoder took an estimated guess at what
  // quantizer value would produce an encoded frame size as close to the target
  // as possible.  Now that the frame has been encoded and the number of bytes
  // is known, the perfect quantizer value (i.e., the one that should have been
  // used) can be determined.  This perfect quantizer is then normalized and
  // used as the lossy utilization.
  const double actual_bitrate =
      encoded_frame->data.size() * 8.0 / predicted_frame_duration.InSecondsF();
  const double target_bitrate = 1000.0 * config_.rc_target_bitrate;
  DCHECK_GT(target_bitrate, 0.0);
  const double bitrate_utilization = actual_bitrate / target_bitrate;
  int quantizer = -1;
  CHECK_EQ(vpx_codec_control(&encoder_, VP8E_GET_LAST_QUANTIZER_64, &quantizer),
           VPX_CODEC_OK);
  const double perfect_quantizer = bitrate_utilization * std::max(0, quantizer);
  // Side note: If it was possible for the encoder to encode within the target
  // number of bytes, the |perfect_quantizer| will be in the range [0.0,63.0].
  // If it was never possible, the value will be greater than 63.0.
  encoded_frame->lossy_utilization = perfect_quantizer / 63.0;

  DVLOG(2) << "VP8 encoded frame_id " << encoded_frame->frame_id
           << ", sized: " << encoded_frame->data.size()
           << ", deadline_utilization: " << encoded_frame->deadline_utilization
           << ", lossy_utilization: " << encoded_frame->lossy_utilization
           << " (quantizer chosen by the encoder was " << quantizer << ')';

  if (encoded_frame->dependency == EncodedFrame::KEY) {
    key_frame_requested_ = false;
  }
}

void Vp8Encoder::UpdateRates(uint32 new_bitrate) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!is_initialized())
    return;

  uint32 new_bitrate_kbit = new_bitrate / 1000;
  if (config_.rc_target_bitrate == new_bitrate_kbit)
    return;

  config_.rc_target_bitrate = bitrate_kbit_ = new_bitrate_kbit;

  // Update encoder context.
  if (vpx_codec_enc_config_set(&encoder_, &config_)) {
    NOTREACHED() << "Invalid return value";
  }

  VLOG(1) << "VP8 new rc_target_bitrate: " << new_bitrate_kbit << " kbps";
}

void Vp8Encoder::GenerateKeyFrame() {
  DCHECK(thread_checker_.CalledOnValidThread());
  key_frame_requested_ = true;
}

}  // namespace cast
}  // namespace media
