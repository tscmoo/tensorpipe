/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <tensorpipe/channel/cuda_ipc/factory.h>

#include <tensorpipe/channel/context_boilerplate.h>
#include <tensorpipe/channel/cuda_ipc/channel_impl.h>
#include <tensorpipe/channel/cuda_ipc/context_impl.h>

namespace tensorpipe_moorpc {
namespace channel {
namespace cuda_ipc {

std::shared_ptr<CudaContext> create() {
  return std::make_shared<
      ContextBoilerplate<CudaBuffer, ContextImpl, ChannelImpl>>(
      ContextImpl::create());
}

} // namespace cuda_ipc
} // namespace channel
} // namespace tensorpipe_moorpc
