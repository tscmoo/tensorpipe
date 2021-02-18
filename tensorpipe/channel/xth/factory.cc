/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <tensorpipe/channel/xth/factory.h>

#include <tensorpipe/channel/context_boilerplate.h>
#include <tensorpipe/channel/xth/channel_impl.h>
#include <tensorpipe/channel/xth/context_impl.h>

namespace tensorpipe_moorpc {
namespace channel {
namespace xth {

std::shared_ptr<CpuContext> create() {
  return std::make_shared<
      ContextBoilerplate<CpuBuffer, ContextImpl, ChannelImpl>>(
      ContextImpl::create());
}

} // namespace xth
} // namespace channel
} // namespace tensorpipe_moorpc
