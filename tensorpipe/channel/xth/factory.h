/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>

#include <tensorpipe/channel/cpu_context.h>

namespace tensorpipe_moorpc {
namespace channel {
namespace xth {

std::shared_ptr<CpuContext> create();

} // namespace xth
} // namespace channel
} // namespace tensorpipe_moorpc
