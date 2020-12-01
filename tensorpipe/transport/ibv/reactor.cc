/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <tensorpipe/transport/ibv/reactor.h>

#include <tensorpipe/common/system.h>
#include <tensorpipe/transport/ibv/constants.h>

namespace rpc_tensorpipe {
namespace transport {
namespace ibv {

Reactor::Reactor() {
  Error error;
  std::tie(error, ibvLib_) = IbvLib::create();
  // FIXME Instead of throwing away the error and setting a bool, we should have
  // a way to set the reactor in an error state, and use that for viability.
  if (error) {
    TP_VLOG(9) << "Transport context " << id_
               << " couldn't open libibverbs: " << error.what();
    return;
  }
  foundIbvLib_ = true;
  IbvDeviceList deviceList(getIbvLib());
  if (deviceList.size() == 0) {
    return;
  }
  ctx_ = createIbvContext(getIbvLib(), deviceList[0]);
  pd_ = createIbvProtectionDomain(getIbvLib(), ctx_);
  cq_ = createIbvCompletionQueue(
      getIbvLib(),
      ctx_,
      kCompletionQueueSize,
      /*cq_context=*/nullptr,
      /*channel=*/nullptr,
      /*comp_vector=*/0);

  IbvLib::device_attr devattr;
  getIbvLib().query_device(ctx_.get(), &devattr);

  printf("max wr is %d %d\n", devattr.max_qp_wr, devattr.max_srq_wr);
  printf("max cqe is %d\n", devattr.max_cqe);

  //std::abort();

  IbvLib::srq_init_attr srqInitAttr;
  std::memset(&srqInitAttr, 0, sizeof(srqInitAttr));
  srqInitAttr.attr.max_wr = kNumPendingRecvReqs;
  srqInitAttr.attr.max_sge = 1;
  srq_ = createIbvSharedReceiveQueue(getIbvLib(), pd_, srqInitAttr);

  addr_ = makeIbvAddress(getIbvLib(), ctx_, kPortNum, kGlobalIdentifierIndex);

  postRecvRequestsOnSRQ_(kNumPendingRecvReqs);

  startThread("TP_IBV_reactor");
}

bool Reactor::isViable() const {
  return foundIbvLib_ && const_cast<IbvContext&>(ctx_).get() != nullptr;
}

void Reactor::postRecvRequestsOnSRQ_(int num) {
  while (num > 0) {
    IbvLib::recv_wr* badRecvWr = nullptr;
    std::array<IbvLib::recv_wr, kNumPolledWorkCompletions> wrs;
    std::memset(wrs.data(), 0, sizeof(wrs));
    for (int i = 0; i < std::min(num, kNumPolledWorkCompletions) - 1; i++) {
      wrs[i].next = &wrs[i + 1];
    }
    TP_VLOG(9) << "post_srq_recv on SRQ " << srq_.get()->handle;
    int rv = getIbvLib().post_srq_recv(srq_.get(), wrs.data(), &badRecvWr);
    TP_THROW_SYSTEM_IF(rv != 0, errno);
    TP_THROW_ASSERT_IF(badRecvWr != nullptr);
    num -= std::min(num, kNumPolledWorkCompletions);
  }
}

void Reactor::setId(std::string id) {
  id_ = std::move(id);
}

void Reactor::close() {
  if (!closed_.exchange(true)) {
    stopBusyPolling();
  }
}

void Reactor::join() {
  close();

  if (!joined_.exchange(true)) {
    joinThread();
  }
}

Reactor::~Reactor() {
  join();
}

bool Reactor::pollOnce() {
  std::array<IbvLib::wc, kNumPolledWorkCompletions> wcs;
  auto rv = getIbvLib().poll_cq(cq_.get(), wcs.size(), wcs.data());

  static std::chrono::steady_clock::time_point lp;
  auto now = std::chrono::steady_clock::now();
  if (now - lp >= std::chrono::milliseconds(100)) {
    printf("definitely still polling\n");
    lp = now;
  }

  if (rv == 0) {
    return false;
  }
  TP_THROW_SYSTEM_IF(rv < 0, errno);

  int numRecvs = 0;
  int numWrites = 0;
  int numAcks = 0;
  for (int wcIdx = 0; wcIdx < rv; wcIdx++) {
    IbvLib::wc& wc = wcs[wcIdx];

    TP_VLOG(9) << "Transport context " << id_
               << " got work completion for request " << wc.wr_id << " for QP "
               << wc.qp_num << " with status "
               << getIbvLib().wc_status_str(wc.status) << " and opcode "
               << ibvWorkCompletionOpcodeToStr(wc.opcode)
               << " (byte length: " << wc.byte_len
               << ", immediate data: " << wc.imm_data << ")";

    auto iter = queuePairEventHandler_.find(wc.qp_num);
    TP_THROW_ASSERT_IF(iter == queuePairEventHandler_.end())
        << "Got work completion for unknown queue pair " << wc.qp_num;

    switch (wc.opcode) {
      case IbvLib::WC_RECV_RDMA_WITH_IMM:
        TP_THROW_ASSERT_IF(!(wc.wc_flags & IbvLib::WC_WITH_IMM));
        numRecvs++;
        break;
      case IbvLib::WC_RECV:
        TP_THROW_ASSERT_IF(!(wc.wc_flags & IbvLib::WC_WITH_IMM));
        numRecvs++;
        break;
      case IbvLib::WC_RDMA_WRITE:
        numWrites++;
        break;
      case IbvLib::WC_SEND:
        numAcks++;
        break;
      default:
        TP_THROW_ASSERT() << "Unknown opcode: " << wc.opcode;
    }

    if (wc.status != IbvLib::WC_SUCCESS) {
      iter->second->onError(wc.status, wc.wr_id);
      continue;
    }

    switch (wc.opcode) {
      case IbvLib::WC_RECV_RDMA_WITH_IMM:
        TP_THROW_ASSERT_IF(!(wc.wc_flags & IbvLib::WC_WITH_IMM));
        iter->second->onRemoteProducedData(wc.imm_data);
        break;
      case IbvLib::WC_RECV:
        TP_THROW_ASSERT_IF(!(wc.wc_flags & IbvLib::WC_WITH_IMM));
        iter->second->onRemoteConsumedData(wc.imm_data);
        break;
      case IbvLib::WC_RDMA_WRITE:
        iter->second->onWriteCompleted();
        break;
      case IbvLib::WC_SEND:
        iter->second->onAckCompleted();
        break;
      default:
        TP_THROW_ASSERT() << "Unknown opcode: " << wc.opcode;
    }
  }

  postRecvRequestsOnSRQ_(numRecvs);

  numAvailableWrites_ += numWrites;
  while (!pendingQpWrites_.empty() && numAvailableWrites_ > 0) {
    postWrite(
        std::get<0>(pendingQpWrites_.front()),
        std::get<1>(pendingQpWrites_.front()));
    pendingQpWrites_.pop_front();
  }

  numAvailableAcks_ += numAcks;
  while (!pendingQpAcks_.empty() && numAvailableAcks_ > 0) {
    postAck(
        std::get<0>(pendingQpAcks_.front()),
        std::get<1>(pendingQpAcks_.front()));
    pendingQpAcks_.pop_front();
  }

  return true;
}

bool Reactor::readyToClose() {
  return queuePairEventHandler_.size() == 0;
}

void Reactor::registerQp(
    uint32_t qpn,
    std::shared_ptr<IbvEventHandler> eventHandler) {
  queuePairEventHandler_.emplace(qpn, std::move(eventHandler));
}

void Reactor::unregisterQp(uint32_t qpn) {
  queuePairEventHandler_.erase(qpn);
}

void Reactor::postWrite(IbvQueuePair& qp, IbvLib::send_wr& wr) {
  if (numAvailableWrites_ > 0) {
    IbvLib::send_wr* badWr = nullptr;
    TP_VLOG(9) << "Transport context " << id_ << " posting RDMA write for QP "
               << qp->qp_num;
    TP_CHECK_IBV_INT(getIbvLib().post_send(qp.get(), &wr, &badWr));
    if (badWr) {
      perror("post_send");
    }
    TP_THROW_ASSERT_IF(badWr != nullptr);
    numAvailableWrites_--;
  } else {
    TP_VLOG(9) << "Transport context " << id_
               << " queueing up RDMA write for QP " << qp->qp_num;
    pendingQpWrites_.emplace_back(qp, wr);
  }
}

void Reactor::postAck(IbvQueuePair& qp, IbvLib::send_wr& wr) {
  if (numAvailableAcks_ > 0) {
    IbvLib::send_wr* badWr = nullptr;
    TP_VLOG(9) << "Transport context " << id_ << " posting send for QP "
               << qp->qp_num;
    TP_CHECK_IBV_INT(getIbvLib().post_send(qp.get(), &wr, &badWr));
    if (badWr) {
      perror("post_send");
    }
    TP_THROW_ASSERT_IF(badWr != nullptr);
    numAvailableAcks_--;
  } else {
    TP_VLOG(9) << "Transport context " << id_ << " queueing send for QP "
               << qp->qp_num;
    pendingQpAcks_.emplace_back(qp, wr);
  }
}

} // namespace ibv
} // namespace transport
} // namespace tensorpipe
