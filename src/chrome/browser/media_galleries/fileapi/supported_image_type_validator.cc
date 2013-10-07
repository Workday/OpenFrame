// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media_galleries/fileapi/supported_image_type_validator.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/files/scoped_platform_file_closer.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/stl_util.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/image_decoder.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace chrome {

namespace {

// Arbitrary limit to sanity check the file size.
const int kMaxImageFileSize = 50*1014*1024;

scoped_ptr<std::string> ReadOnFileThread(const base::FilePath& path) {
  base::ThreadRestrictions::AssertIOAllowed();
  scoped_ptr<std::string> result;

  base::PlatformFile file = base::CreatePlatformFile(
      path, base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_READ, NULL, NULL);
  if (file == base::kInvalidPlatformFileValue)
    return result.Pass();
  base::ScopedPlatformFileCloser file_closer(&file);

  base::PlatformFileInfo file_info;
  if (!base::GetPlatformFileInfo(file, &file_info) ||
      file_info.size > kMaxImageFileSize) {
    return result.Pass();
  }

  result.reset(new std::string);
  result->resize(file_info.size);
  if (base::ReadPlatformFile(file, 0, string_as_array(result.get()),
                             file_info.size) != file_info.size) {
    result.reset();
  }

  return result.Pass();
}

class ImageDecoderDelegateAdapter : public ImageDecoder::Delegate {
 public:
  ImageDecoderDelegateAdapter(
      scoped_ptr<std::string> data,
      const fileapi::CopyOrMoveFileValidator::ResultCallback& callback)
      : data_(data.Pass()),
        callback_(callback) {
    DCHECK(data_);
  }

  const std::string& data() {
    return *data_;
  }

  // ImageDecoder::Delegate methods.
  virtual void OnImageDecoded(const ImageDecoder* /*decoder*/,
                              const SkBitmap& /*decoded_image*/) OVERRIDE {
    callback_.Run(base::PLATFORM_FILE_OK);
    delete this;
  }

  virtual void OnDecodeImageFailed(const ImageDecoder* /*decoder*/) OVERRIDE {
    callback_.Run(base::PLATFORM_FILE_ERROR_SECURITY);
    delete this;
  }

 private:
  scoped_ptr<std::string> data_;
  fileapi::CopyOrMoveFileValidator::ResultCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(ImageDecoderDelegateAdapter);
};

}  // namespace

SupportedImageTypeValidator::~SupportedImageTypeValidator() {}

// static
bool SupportedImageTypeValidator::SupportsFileType(const base::FilePath& path) {
  base::FilePath::StringType extension = path.Extension();
  return extension == FILE_PATH_LITERAL(".bmp") ||
         extension == FILE_PATH_LITERAL(".gif") ||
         extension == FILE_PATH_LITERAL(".jfif") ||
         extension == FILE_PATH_LITERAL(".jpeg") ||
         extension == FILE_PATH_LITERAL(".jpg") ||
         extension == FILE_PATH_LITERAL(".pjp") ||
         extension == FILE_PATH_LITERAL(".pjpeg") ||
         extension == FILE_PATH_LITERAL(".png") ||
         extension == FILE_PATH_LITERAL(".webp");
}

void SupportedImageTypeValidator::StartPreWriteValidation(
    const ResultCallback& result_callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(callback_.is_null());
  callback_ = result_callback;

  BrowserThread::PostTaskAndReplyWithResult(
      BrowserThread::FILE,
      FROM_HERE,
      base::Bind(&ReadOnFileThread, path_),
      base::Bind(&SupportedImageTypeValidator::OnFileOpen,
                 weak_factory_.GetWeakPtr()));
}

SupportedImageTypeValidator::SupportedImageTypeValidator(
    const base::FilePath& path)
    : path_(path),
      weak_factory_(this) {
}

void SupportedImageTypeValidator::OnFileOpen(scoped_ptr<std::string> data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!data.get()) {
    callback_.Run(base::PLATFORM_FILE_ERROR_SECURITY);
    return;
  }

  // |adapter| will delete itself after a completion message is received.
  ImageDecoderDelegateAdapter* adapter =
      new ImageDecoderDelegateAdapter(data.Pass(), callback_);
  decoder_ = new ImageDecoder(adapter, adapter->data(),
                              ImageDecoder::DEFAULT_CODEC);
  decoder_->Start(content::BrowserThread::GetMessageLoopProxyForThread(
      BrowserThread::IO));
}

}  // namespace chrome
