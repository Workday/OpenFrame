// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ANDROID_WEBVIEW_PUBLIC_BROWSER_DRAW_SW_H_
#define ANDROID_WEBVIEW_PUBLIC_BROWSER_DRAW_SW_H_

#include <jni.h>
#include <stddef.h>

#ifndef __cplusplus
#error "Can't mix C and C++ when using jni.h"
#endif

class SkPicture;

static const int kAwPixelInfoVersion = 2;

// Values of the AwPixelInfo::config field.
enum AwPixelConfig {
  AwConfig_RGB_565 = 4,
  AwConfig_ARGB_4444 = 5,
  AwConfig_ARGB_8888 = 6,
};

// Holds the information required to implement the SW draw to system canvas.
struct AwPixelInfo {
  int version;          // The kAwPixelInfoVersion this struct was built with.
  int config;           // |pixel| format: a value from AwPixelConfig.
  int width;            // In pixels.
  int height;           // In pixels.
  int row_bytes;        // Number of bytes from start of one line to next.
  void* pixels;         // The pixels, all (height * row_bytes) of them.
  // The Matrix and Clip are relative to |pixels|, not the source canvas.
  float matrix[9];      // The matrix currently in effect on the canvas.
  int clip_rect_count;  // Number of rects in |clip_rects|.
  int* clip_rects;      // Clip area: 4 ints per rect in {x,y,w,h} format.
  // NOTE: If you add more members, bump kAwPixelInfoVersion.
};

// Function that can be called to fish out the underlying native pixel data
// from a Java canvas object, for optimized rendering path.
// Returns the pixel info on success, which must be freed via a call to
// AwReleasePixelsFunction, or NULL.
typedef AwPixelInfo* (AwAccessPixelsFunction)(JNIEnv* env, jobject canvas);

// Must be called to balance every *successful* call to AwAccessPixelsFunction
// (i.e. that returned true).
typedef void (AwReleasePixelsFunction)(AwPixelInfo* pixels);

// Called to create an Android Picture object encapsulating a native SkPicture.
typedef jobject (AwCreatePictureFunction)(JNIEnv* env, SkPicture* picture);

// Method that returns the current Skia function.
typedef void (SkiaVersionFunction)(int* major, int* minor, int* patch);

// Called to verify if the Skia versions are compatible.
typedef bool (AwIsSkiaVersionCompatibleFunction)(SkiaVersionFunction function);

// "vtable" for the functions declared in this file. An instance must be set via
// AwContents.setAwDrawSWFunctionTable
struct AwDrawSWFunctionTable {
  AwAccessPixelsFunction* access_pixels;
  AwReleasePixelsFunction* release_pixels;
  AwCreatePictureFunction* create_picture;
  AwIsSkiaVersionCompatibleFunction* is_skia_version_compatible;
};

#endif  // ANDROID_WEBVIEW_PUBLIC_BROWSER_DRAW_SW_H_
