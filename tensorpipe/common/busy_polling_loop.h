/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <utility>

#include <tensorpipe/common/deferred_executor.h>
#include <tensorpipe/common/system.h>

#include <semaphore.h>

namespace tensorpipe {

class Semaphore {
  sem_t sem;

 public:
  Semaphore() {
    sem_init(&sem, 1, 0);
  }
  ~Semaphore() {
    sem_destroy(&sem);
  }
  void post() {
    sem_post(&sem);
  }
  void wait() {
    sem_wait(&sem);
  }
};

class BusyPollingLoop : public EventLoopDeferredExecutor {
 protected:
  virtual bool pollOnce() = 0;

  virtual bool readyToClose() = 0;

  Semaphore sem;

  void stopBusyPolling() {
    closed_ = true;
    //sem.post();
  }

  void eventLoop() override {
    while (!closed_ || !readyToClose()) {
      if (pollOnce()) {
        // continue
      } else if (deferredFunctionCount_ > 0) {
        deferredFunctionCount_ -= runDeferredFunctionsFromEventLoop();
      } else {
        //sem.wait();
      }
    }
  };

  void wakeupEventLoopToDeferFunction() override {
    ++deferredFunctionCount_;
    //sem.post();
  };

 private:
  std::atomic<bool> closed_{false};

  std::atomic<int64_t> deferredFunctionCount_{0};
};

} // namespace tensorpipe
