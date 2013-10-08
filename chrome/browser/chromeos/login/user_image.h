// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_USER_IMAGE_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_USER_IMAGE_H_

#include <vector>

#include "ui/gfx/image/image_skia.h"
#include "url/gurl.h"

namespace chromeos {

// Wrapper class storing a still image and it's raw representation.  Could be
// used for storing profile images (including animated profile images) and user
// wallpapers.
class UserImage {
 public:
  // TODO(ivankr): replace with RefCountedMemory to prevent copying.
  typedef std::vector<unsigned char> RawImage;

  // Creates a new instance from a given still frame and tries to encode raw
  // representation for it.
  // TODO(ivankr): remove eventually.
  static UserImage CreateAndEncode(const gfx::ImageSkia& image);

  // Create instance with an empty still frame and no raw data.
  UserImage();

  // Creates a new instance from a given still frame without any raw data.
  explicit UserImage(const gfx::ImageSkia& image);

  // Creates a new instance from a given still frame and raw representation.
  // |raw_image| can be animated, in which case animated_image() will return the
  // original |raw_image| and raw_image() will return the encoded representation
  // of |image|.
  UserImage(const gfx::ImageSkia& image, const RawImage& raw_image);

  virtual ~UserImage();

  const gfx::ImageSkia& image() const { return image_; }

  // Optional raw representation of the still image.
  bool has_raw_image() const { return has_raw_image_; }
  const RawImage& raw_image() const { return raw_image_; }

  // Discards the stored raw image, freeing used memory.
  void DiscardRawImage();

  // Optional raw representation of the animated image.
  bool has_animated_image() const { return has_animated_image_; }
  const RawImage& animated_image() const { return animated_image_; }

  // URL from which this image was originally downloaded, if any.
  void set_url(const GURL& url) { url_ = url; }
  GURL url() const { return url_; }

  // Whether |raw_image| contains data in format that is considered safe to
  // decode in sensitive environment (on Login screen).
  bool is_safe_format() const { return is_safe_format_; }
  void MarkAsSafe();

 private:
  gfx::ImageSkia image_;
  bool has_raw_image_;
  RawImage raw_image_;
  bool has_animated_image_;
  RawImage animated_image_;
  GURL url_;
  bool is_safe_format_;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_USER_IMAGE_H_
