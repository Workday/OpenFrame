// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_MEDIA_VP9_DECODER_H_
#define CONTENT_COMMON_GPU_MEDIA_VP9_DECODER_H_

#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "content/common/gpu/media/accelerated_video_decoder.h"
#include "content/common/gpu/media/vp9_picture.h"
#include "media/filters/vp9_parser.h"

namespace content {

// This class implements an AcceleratedVideoDecoder for VP9 decoding.
// Clients of this class are expected to pass raw VP9 stream and are expected
// to provide an implementation of VP9Accelerator for offloading final steps
// of the decoding process.
//
// This class must be created, called and destroyed on a single thread, and
// does nothing internally on any other thread.
class CONTENT_EXPORT VP9Decoder : public AcceleratedVideoDecoder {
 public:
  class CONTENT_EXPORT VP9Accelerator {
   public:
    VP9Accelerator();
    virtual ~VP9Accelerator();

    // Create a new VP9Picture that the decoder client can use for initial
    // stages of the decoding process and pass back to this accelerator for
    // final, accelerated stages of it, or for reference when decoding other
    // pictures.
    //
    // When a picture is no longer needed by the decoder, it will just drop
    // its reference to it, and it may do so at any time.
    //
    // Note that this may return nullptr if the accelerator is not able to
    // provide any new pictures at the given time. The decoder must handle this
    // case and treat it as normal, returning kRanOutOfSurfaces from Decode().
    virtual scoped_refptr<VP9Picture> CreateVP9Picture() = 0;

    // Submit decode for |pic| to be run in accelerator, taking as arguments
    // information contained in it, as well as current segmentation and loop
    // filter state in |seg| and |lf|, respectively, and using pictures in
    // |ref_pictures| for reference.
    //
    // Note that returning from this method does not mean that the decode
    // process is finished, but the caller may drop its references to |pic|
    // and |ref_pictures| immediately, and the data in |seg| and |lf| does not
    // need to remain valid after this method returns.
    //
    // Return true when successful, false otherwise.
    virtual bool SubmitDecode(
        const scoped_refptr<VP9Picture>& pic,
        const media::Vp9Segmentation& seg,
        const media::Vp9LoopFilter& lf,
        const std::vector<scoped_refptr<VP9Picture>>& ref_pictures) = 0;

    // Schedule output (display) of |pic|.
    //
    // Note that returning from this method does not mean that |pic| has already
    // been outputted (displayed), but guarantees that all pictures will be
    // outputted in the same order as this method was called for them, and that
    // they are decoded before outputting (assuming SubmitDecode() has been
    // called for them beforehand). Decoder may drop its references to |pic|
    // immediately after calling this method.
    //
    // Return true when successful, false otherwise.
    virtual bool OutputPicture(const scoped_refptr<VP9Picture>& pic) = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(VP9Accelerator);
  };

  VP9Decoder(VP9Accelerator* accelerator);
  ~VP9Decoder() override;

  // content::AcceleratedVideoDecoder implementation.
  void SetStream(const uint8_t* ptr, size_t size) override;
  bool Flush() override WARN_UNUSED_RESULT;
  void Reset() override;
  DecodeResult Decode() override WARN_UNUSED_RESULT;
  gfx::Size GetPicSize() const override;
  size_t GetRequiredNumOfPictures() const override;

 private:
  // Update ref_frames_ based on the information in current frame header.
  void RefreshReferenceFrames(const scoped_refptr<VP9Picture>& pic);

  // Decode and possibly output |pic| (if the picture is to be shown).
  // Return true on success, false otherwise.
  bool DecodeAndOutputPicture(scoped_refptr<VP9Picture> pic);

  // Called on error, when decoding cannot continue. Sets state_ to kError and
  // releases current state.
  void SetError();

  enum State {
    kNeedStreamMetadata,  // After initialization, need a keyframe.
    kDecoding,            // Ready to decode from any point.
    kAfterReset,          // After Reset(), need a resume point.
    kError,               // Error in decode, can't continue.
  };

  // Current decoder state.
  State state_;

  // Current frame header to be used in decoding the next picture.
  scoped_ptr<media::Vp9FrameHeader> curr_frame_hdr_;

  // Reference frames currently in use.
  std::vector<scoped_refptr<VP9Picture>> ref_frames_;

  // Current coded resolution.
  gfx::Size pic_size_;

  media::Vp9Parser parser_;

  // VP9Accelerator instance owned by the client.
  VP9Accelerator* accelerator_;

  DISALLOW_COPY_AND_ASSIGN(VP9Decoder);
};

}  // namespace content

#endif  // CONTENT_COMMON_GPU_MEDIA_VP9_DECODER_H_
