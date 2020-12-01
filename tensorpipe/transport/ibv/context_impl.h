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
#include <tensorpipe/common/epoll_loop.h>
#include <tensorpipe/transport/ibv/context.h>

namespace rpc_tensorpipe {
namespace transport {
namespace ibv {

class Reactor;

class Context::PrivateIface : public DeferredExecutor {
 public:
  virtual ClosingEmitter& getClosingEmitter() = 0;

  virtual void registerDescriptor(
      int fd,
      int events,
      std::shared_ptr<EpollLoop::EventHandler> h) = 0;

  virtual void unregisterDescriptor(int fd) = 0;

  virtual Reactor& getReactor() = 0;

  virtual ~PrivateIface() = default;
};

} // namespace ibv
} // namespace transport
} // namespace rpc_tensorpipe
