/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <tensorpipe/benchmark/transport_registry.h>

#include <tensorpipe/tensorpipe.h>

TP_DEFINE_SHARED_REGISTRY(
    TensorpipeTransportRegistry,
    tensorpipe_moorpc::transport::Context);

// IBV

#if TENSORPIPE_HAS_IBV_TRANSPORT
std::shared_ptr<tensorpipe_moorpc::transport::Context> makeIbvContext() {
  return tensorpipe_moorpc::transport::ibv::create();
}

TP_REGISTER_CREATOR(TensorpipeTransportRegistry, ibv, makeIbvContext);
#endif // TENSORPIPE_HAS_IBV_TRANSPORT

// SHM

#if TENSORPIPE_HAS_SHM_TRANSPORT
std::shared_ptr<tensorpipe_moorpc::transport::Context> makeShmContext() {
  return tensorpipe_moorpc::transport::shm::create();
}

TP_REGISTER_CREATOR(TensorpipeTransportRegistry, shm, makeShmContext);
#endif // TENSORPIPE_HAS_SHM_TRANSPORT

// UV

std::shared_ptr<tensorpipe_moorpc::transport::Context> makeUvContext() {
  return tensorpipe_moorpc::transport::uv::create();
}

TP_REGISTER_CREATOR(TensorpipeTransportRegistry, uv, makeUvContext);
