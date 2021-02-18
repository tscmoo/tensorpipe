/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <tensorpipe/benchmark/channel_registry.h>

#include <tensorpipe/tensorpipe.h>

TP_DEFINE_SHARED_REGISTRY(
    TensorpipeChannelRegistry,
    tensorpipe_moorpc::channel::CpuContext);

// BASIC

std::shared_ptr<tensorpipe_moorpc::channel::CpuContext> makeBasicChannel() {
  return tensorpipe_moorpc::channel::basic::create();
}

TP_REGISTER_CREATOR(TensorpipeChannelRegistry, basic, makeBasicChannel);

// CMA

#if TENSORPIPE_HAS_CMA_CHANNEL
std::shared_ptr<tensorpipe_moorpc::channel::CpuContext> makeCmaChannel() {
  return tensorpipe_moorpc::channel::cma::create();
}

TP_REGISTER_CREATOR(TensorpipeChannelRegistry, cma, makeCmaChannel);
#endif // TENSORPIPE_HAS_CMA_CHANNEL

// MPT

std::shared_ptr<tensorpipe_moorpc::channel::CpuContext> makeMptChannel() {
  throw std::runtime_error("mtp channel requires arguments");
}

TP_REGISTER_CREATOR(TensorpipeChannelRegistry, mpt, makeMptChannel);

// XTH

std::shared_ptr<tensorpipe_moorpc::channel::CpuContext> makeXthChannel() {
  return tensorpipe_moorpc::channel::xth::create();
}

TP_REGISTER_CREATOR(TensorpipeChannelRegistry, xth, makeXthChannel);
