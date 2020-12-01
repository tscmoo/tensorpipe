/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

namespace rpc_tensorpipe {

std::tuple<std::string, std::string> splitSchemeOfURL(const std::string&);

}
