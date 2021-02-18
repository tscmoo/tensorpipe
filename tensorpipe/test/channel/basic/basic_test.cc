/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <tensorpipe/channel/basic/factory.h>
#include <tensorpipe/test/channel/channel_test.h>

namespace {

class BasicChannelTestHelper : public ChannelTestHelper<tensorpipe_moorpc::CpuBuffer> {
 protected:
  std::shared_ptr<tensorpipe_moorpc::channel::CpuContext> makeContextInternal(
      std::string id) override {
    auto context = tensorpipe_moorpc::channel::basic::create();
    context->setId(std::move(id));
    return context;
  }
};

BasicChannelTestHelper helper;

} // namespace

INSTANTIATE_TEST_CASE_P(Basic, CpuChannelTestSuite, ::testing::Values(&helper));
