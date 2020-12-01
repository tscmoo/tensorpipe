/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>

#include <tensorpipe/channel/xth/context.h>
#include <tensorpipe/common/callback.h>
#include <tensorpipe/common/error.h>

namespace rpc_tensorpipe {
namespace channel {
namespace xth {

class Context::PrivateIface {
 public:
  virtual ClosingEmitter& getClosingEmitter() = 0;

  using copy_request_callback_fn = Function<void(const Error&)>;

  virtual void requestCopy(
      void* remotePtr,
      void* localPtr,
      size_t length,
      copy_request_callback_fn fn) = 0;

  virtual ~PrivateIface() = default;
};

} // namespace xth
} // namespace channel
} // namespace rpc_tensorpipe
