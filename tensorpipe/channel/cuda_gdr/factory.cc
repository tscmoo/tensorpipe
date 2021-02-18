/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <tensorpipe/channel/cuda_gdr/factory.h>

#include <tensorpipe/channel/context_boilerplate.h>
#include <tensorpipe/channel/cuda_gdr/channel_impl.h>
#include <tensorpipe/channel/cuda_gdr/context_impl.h>

namespace tensorpipe_moorpc {
namespace channel {
namespace cuda_gdr {

std::shared_ptr<CudaContext> create(
    optional<std::vector<std::string>> gpuIdxToNicName) {
  return std::make_shared<
      ContextBoilerplate<CudaBuffer, ContextImpl, ChannelImpl>>(
      ContextImpl::create(std::move(gpuIdxToNicName)));
}

} // namespace cuda_gdr
} // namespace channel
} // namespace tensorpipe_moorpc
