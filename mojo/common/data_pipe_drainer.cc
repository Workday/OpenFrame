// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/common/data_pipe_drainer.h"

#include "base/bind.h"

namespace mojo {
namespace common {

DataPipeDrainer::DataPipeDrainer(Client* client,
                                 mojo::ScopedDataPipeConsumerHandle source)
    : client_(client), source_(source.Pass()), weak_factory_(this) {
  DCHECK(client_);
  ReadData();
}

DataPipeDrainer::~DataPipeDrainer() {}

void DataPipeDrainer::ReadData() {
  const void* buffer = nullptr;
  uint32_t num_bytes = 0;
  MojoResult rv = BeginReadDataRaw(source_.get(), &buffer, &num_bytes,
                                   MOJO_READ_DATA_FLAG_NONE);
  if (rv == MOJO_RESULT_OK) {
    client_->OnDataAvailable(buffer, num_bytes);
    EndReadDataRaw(source_.get(), num_bytes);
    WaitForData();
  } else if (rv == MOJO_RESULT_SHOULD_WAIT) {
    WaitForData();
  } else if (rv == MOJO_RESULT_FAILED_PRECONDITION) {
    client_->OnDataComplete();
  } else {
    DCHECK(false) << "Unhandled MojoResult: " << rv;
  }
}

void DataPipeDrainer::WaitForData() {
  handle_watcher_.Start(
      source_.get(), MOJO_HANDLE_SIGNAL_READABLE, MOJO_DEADLINE_INDEFINITE,
      base::Bind(&DataPipeDrainer::WaitComplete, weak_factory_.GetWeakPtr()));
}

void DataPipeDrainer::WaitComplete(MojoResult result) {
  ReadData();
}

}  // namespace common
}  // namespace mojo
