// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLIMP_NET_INPUT_MESSAGE_GENERATOR_H_
#define BLIMP_NET_INPUT_MESSAGE_GENERATOR_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "blimp/net/blimp_net_export.h"
#include "net/base/completion_callback.h"

namespace blink {
class WebInputEvent;
}

namespace blimp {

class BlimpMessage;
class BlimpMessageProcessor;

// Handles creating serialized InputMessage protos from a stream of
// WebInputEvents.  This class may be stateful to optimize the size of the
// serialized transmission data.  See InputMessageProcessor for the deserialize
// code.
class BLIMP_NET_EXPORT InputMessageGenerator {
 public:
  InputMessageGenerator();
  ~InputMessageGenerator();

  // Builds a BlimpMessage from |event| that has the basic input event fields
  // populated.  This might make use of state sent from previous
  // BlimpMessage::INPUT messages.  It is up to the caller to populate the
  // non-input fields and to send the BlimpMessage.
  scoped_ptr<BlimpMessage> GenerateMessage(const blink::WebInputEvent& event);

 private:
  DISALLOW_COPY_AND_ASSIGN(InputMessageGenerator);
};

}  // namespace blimp

#endif  // BLIMP_NET_INPUT_MESSAGE_GENERATOR_H_
