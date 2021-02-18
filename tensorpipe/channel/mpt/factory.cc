/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <tensorpipe/channel/mpt/factory.h>

#include <tensorpipe/channel/context_boilerplate.h>
#include <tensorpipe/channel/mpt/channel_impl.h>
#include <tensorpipe/channel/mpt/context_impl.h>

namespace tensorpipe_moorpc {
namespace channel {
namespace mpt {

std::shared_ptr<CpuContext> create(
    std::vector<std::shared_ptr<transport::Context>> contexts,
    std::vector<std::shared_ptr<transport::Listener>> listeners) {
  auto impl = ContextImpl::create(std::move(contexts), std::move(listeners));
  // FIXME Make this part of the generic boilerplate.
  impl->init();
  return std::make_shared<
      ContextBoilerplate<CpuBuffer, ContextImpl, ChannelImpl>>(std::move(impl));
}

} // namespace mpt
} // namespace channel
} // namespace tensorpipe_moorpc
