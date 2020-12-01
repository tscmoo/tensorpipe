/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>
#include <memory>

#include <tensorpipe/common/callback.h>
#include <tensorpipe/transport/uv/context.h>

namespace rpc_tensorpipe {
namespace transport {
namespace uv {

class TCPHandle;

class Context::PrivateIface : public DeferredExecutor {
 public:
  virtual ClosingEmitter& getClosingEmitter() = 0;

  virtual std::shared_ptr<TCPHandle> createHandle() = 0;

  virtual ~PrivateIface() = default;
};

} // namespace uv
} // namespace transport
} // namespace rpc_tensorpipe
