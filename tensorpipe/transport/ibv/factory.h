/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>

#include <tensorpipe/transport/context.h>

namespace tensorpipe_moorpc {
namespace transport {
namespace ibv {

std::shared_ptr<Context> create();

} // namespace ibv
} // namespace transport
} // namespace tensorpipe_moorpc
