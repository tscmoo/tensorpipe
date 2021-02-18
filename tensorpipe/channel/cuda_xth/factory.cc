/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <tensorpipe/channel/cuda_xth/factory.h>

#include <tensorpipe/channel/context_boilerplate.h>
#include <tensorpipe/channel/cuda_xth/channel_impl.h>
#include <tensorpipe/channel/cuda_xth/context_impl.h>

namespace tensorpipe_moorpc {
namespace channel {
namespace cuda_xth {

std::shared_ptr<CudaContext> create() {
  return std::make_shared<
      ContextBoilerplate<CudaBuffer, ContextImpl, ChannelImpl>>(
      ContextImpl::create());
}

} // namespace cuda_xth
} // namespace channel
} // namespace tensorpipe_moorpc
