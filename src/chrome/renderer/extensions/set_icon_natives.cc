// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/set_icon_natives.h"

#include <limits>

#include "base/memory/scoped_ptr.h"
#include "chrome/common/render_messages.h"
#include "chrome/renderer/extensions/request_sender.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"

namespace {

const char* kImageSizeKeys[] = { "19", "38" };
const char kInvalidDimensions[] = "ImageData has invalid dimensions.";
const char kInvalidData[] = "ImageData data length does not match dimensions.";
const char kNoMemory[] = "Chrome was unable to initialize icon.";

}  // namespace

namespace extensions {

SetIconNatives::SetIconNatives(Dispatcher* dispatcher,
                               RequestSender* request_sender,
                               ChromeV8Context* context)
    : ChromeV8Extension(dispatcher, context),
      request_sender_(request_sender) {
  RouteFunction(
      "SetIconCommon",
      base::Bind(&SetIconNatives::SetIconCommon, base::Unretained(this)));
}

bool SetIconNatives::ConvertImageDataToBitmapValue(
    const v8::Local<v8::Object> image_data,
    Value** bitmap_value) {
  v8::Local<v8::Object> data =
      image_data->Get(v8::String::New("data"))->ToObject();
  int width = image_data->Get(v8::String::New("width"))->Int32Value();
  int height = image_data->Get(v8::String::New("height"))->Int32Value();

  if (width <= 0 || height <= 0) {
    v8::ThrowException(
        v8::Exception::Error(v8::String::New(kInvalidDimensions)));
    return false;
  }

  // We need to be able to safely check |data_length| == 4 * width * height
  // without overflowing below.
  int max_width = (std::numeric_limits<int>::max() / 4) / height;
  if (width > max_width) {
    v8::ThrowException(
        v8::Exception::Error(v8::String::New(kInvalidDimensions)));
    return false;
  }

  int data_length = data->Get(v8::String::New("length"))->Int32Value();
  if (data_length != 4 * width * height) {
    v8::ThrowException(v8::Exception::Error(v8::String::New(kInvalidData)));
    return false;
  }

  SkBitmap bitmap;
  bitmap.setConfig(SkBitmap::kARGB_8888_Config, width, height);
  if (!bitmap.allocPixels()) {
    v8::ThrowException(v8::Exception::Error(v8::String::New(kNoMemory)));
    return false;
  }
  bitmap.eraseARGB(0, 0, 0, 0);

  uint32_t* pixels = bitmap.getAddr32(0, 0);
  for (int t = 0; t < width*height; t++) {
    // |data| is RGBA, pixels is ARGB.
    pixels[t] = SkPreMultiplyColor(
        ((data->Get(v8::Integer::New(4*t + 3))->Int32Value() & 0xFF) << 24) |
        ((data->Get(v8::Integer::New(4*t + 0))->Int32Value() & 0xFF) << 16) |
        ((data->Get(v8::Integer::New(4*t + 1))->Int32Value() & 0xFF) << 8) |
        ((data->Get(v8::Integer::New(4*t + 2))->Int32Value() & 0xFF) << 0));
  }

  // Construct the Value object.
  IPC::Message bitmap_pickle;
  IPC::WriteParam(&bitmap_pickle, bitmap);
  *bitmap_value = base::BinaryValue::CreateWithCopiedBuffer(
      static_cast<const char*>(bitmap_pickle.data()), bitmap_pickle.size());

  return true;
}

bool SetIconNatives::ConvertImageDataSetToBitmapValueSet(
    const v8::FunctionCallbackInfo<v8::Value>& args,
    base::DictionaryValue* bitmap_set_value) {
  v8::Local<v8::Object> extension_args = args[1]->ToObject();
  v8::Local<v8::Object> details =
      extension_args->Get(v8::String::New("0"))->ToObject();
  v8::Local<v8::Object> image_data_set =
      details->Get(v8::String::New("imageData"))->ToObject();

  DCHECK(bitmap_set_value);
  for (size_t i = 0; i < arraysize(kImageSizeKeys); i++) {
    if (!image_data_set->Has(v8::String::New(kImageSizeKeys[i])))
      continue;
    v8::Local<v8::Object> image_data =
        image_data_set->Get(v8::String::New(kImageSizeKeys[i]))->ToObject();
    Value* image_data_bitmap = NULL;
    if (!ConvertImageDataToBitmapValue(image_data, &image_data_bitmap))
      return false;
    bitmap_set_value->Set(kImageSizeKeys[i], image_data_bitmap);
  }
  return true;
}

void SetIconNatives::SetIconCommon(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  scoped_ptr<base::DictionaryValue> bitmap_set_value(
      new base::DictionaryValue());
  if (!ConvertImageDataSetToBitmapValueSet(args, bitmap_set_value.get()))
    return;

  v8::Local<v8::Object> extension_args = args[1]->ToObject();
  v8::Local<v8::Object> details =
      extension_args->Get(v8::String::New("0"))->ToObject();

  base::DictionaryValue* dict = new base::DictionaryValue();
  dict->Set("imageData", bitmap_set_value.release());

  if (details->Has(v8::String::New("tabId"))) {
    dict->SetInteger("tabId",
                     details->Get(v8::String::New("tabId"))->Int32Value());
  }

  ListValue list_value;
  list_value.Append(dict);

  std::string name = *v8::String::AsciiValue(args[0]);
  int request_id = args[2]->Int32Value();
  bool has_callback = args[3]->BooleanValue();
  bool for_io_thread = args[4]->BooleanValue();

  request_sender_->StartRequest(context(),
                                name,
                                request_id,
                                has_callback,
                                for_io_thread,
                                &list_value);
}

}  // namespace extensions
