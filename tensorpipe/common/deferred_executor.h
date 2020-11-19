/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <atomic>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <tensorpipe/common/defs.h>
#include <tensorpipe/common/system.h>
#include <tensorpipe/common/function.h>

namespace tensorpipe {

//using SpinMutex = std::mutex;
class alignas(64) SpinMutex {
  int lock_ = 0;
public:
  void lock() {
    long tmp, tmp2;
    asm volatile (""
    "3:movl $1, %%edx\n"
    "xor %%rax, %%rax\n"
    "xacquire lock cmpxchgl %%edx, (%2)\n"
    "jz 2f\n"
    "1:\n"
    "pause\n"
    "movl (%2), %%eax\n"
    "testl %%eax, %%eax\n"
    "jnz 1b\n"
    "jmp 3b\n"
    "2:\n"
    : "+a"(tmp), "+d"(tmp2)
    : "r"(&lock_)
    : "memory", "cc");
  }
  void unlock() {
    asm volatile ("xrelease movl $0, (%0)"::"r"(&lock_):"memory");
  }
};

// Dealing with thread-safety using per-object mutexes is prone to deadlocks
// because of reentrant calls (both "upward", when invoking a callback that
// calls back into a method of the object, and "downward", when passing a
// callback to an operation of another object that calls it inline) and lock
// inversions (object A calling a method of object B and attempting to acquire
// its lock, with the reverse happening at the same time). Using a "loop" model,
// where operations aren't called inlined and piled up on the stack but instead
// deferred to a later iteration of the loop, solves many of these issues. This
// abstract interface defines the essential methods we need such event loops to
// provide.
class DeferredExecutor {
 public:
  using TTask = Function<void()>;

  virtual void deferToLoop(TTask fn) = 0;

  virtual bool inLoop() = 0;

  // Prefer using deferToLoop over runInLoop when you don't need to wait for the
  // result.
  template <typename F>
  void runInLoop(F&& fn) {
    // When called from the event loop thread itself (e.g., from a callback),
    // deferring would cause a deadlock because the given callable can only be
    // run when the loop is allowed to proceed. On the other hand, it means it
    // is thread-safe to run it immediately. The danger here however is that it
    // can lead to an inconsistent order between operations run from the event
    // loop, from outside of it, and deferred.
    if (inLoop()) {
      fn();
    } else {
      std::promise<void> promise;
      auto future = promise.get_future();
      // Marked as mutable because the fn might hold some state (e.g., the
      // closure of a lambda) which it might want to modify.
      deferToLoop([&promise, fn{std::forward<F>(fn)}]() mutable {
        try {
          fn();
          promise.set_value();
        } catch (...) {
          promise.set_exception(std::current_exception());
        }
      });
      future.get();
    }
  }

  virtual ~DeferredExecutor() = default;
};

// Transports typically have their own thread they can use as deferred executors
// but many objects (like pipes) don't naturally own threads and introducing
// them would also mean introducing latency costs due to context switching.
// In order to give these objects a loop they can use to defer their operations
// to, we can have them temporarily hijack the calling thread and repurpose it
// to run an ephemeral loop on which to run the original task and all the ones
// that a task running on the loop chooses to defer to a later iteration of the
// loop, recursively. Once all these tasks have been completed, the makeshift
// loop is dismantled and control of the thread is returned to the caller.
// FIXME Rename this to OnDemandDeferredExecutor?
class OnDemandDeferredExecutor : public DeferredExecutor {
 public:
  bool inLoop() override {
    // If the current thread is already holding the lock (i.e., it's already in
    // this function somewhere higher up in the stack) then this check won't
    // race and we will detect it correctly. If this is not the case, then this
    // check may race with another thread, but that's nothing to worry about
    // because in either case the outcome will be negative.
    return currentLoop_ == std::this_thread::get_id();
  }

  void deferToLoop(TTask fn) override {
    std::unique_lock l(mutex);
    if (queue.empty() && currentLoop_ == std::thread::id()) {
      currentLoop_ = std::this_thread::get_id(); // exception safety?
      l.unlock();
      fn();
      l.lock();
      if (!queue.empty()) {
        unloadQueue(l);
      }
      currentLoop_ = std::thread::id();
    } else {
      queue.push_back(fn.release());
      if (currentLoop_ == std::thread::id()) {
        currentLoop_ = std::this_thread::get_id();
        unloadQueue(l);
        currentLoop_ = std::thread::id();
      }
    }
  }

protected:

  std::vector<FunctionPointer> queue;
  SpinMutex mutex;
  void enqueue(FunctionPointer ptr) {
    std::lock_guard l(mutex);
    queue.push_back(ptr);
  }

  size_t unloadQueue(std::unique_lock<SpinMutex>& l) {
    thread_local std::vector<FunctionPointer> localQueue;
    auto& lq = localQueue;
    size_t r = 0;
    do {
      std::swap(queue, lq);
      l.unlock();
      for (FunctionPointer f : lq) {
        Function<void()>{f}();
      }
      r += lq.size();
      lq.clear();
      l.lock();
    } while (!queue.empty());
    return r;
  }

  void loop() {
    std::unique_lock l(mutex);
    if (currentLoop_ != std::thread::id()) {
      return;
    }
    currentLoop_ = std::this_thread::get_id();
    unloadQueue(l);
    currentLoop_ = std::thread::id();
  }

  size_t runDeferredFunctions() {
    std::unique_lock l(mutex);
    if (queue.empty()) {
      return 0;
    }
    return unloadQueue(l);
  }

  std::atomic<std::thread::id> currentLoop_;
};

class EventLoopDeferredExecutor : public virtual OnDemandDeferredExecutor {
 public:
  void deferToLoop(TTask fn) override {
    enqueue(fn.release());
    wakeupEventLoopToDeferFunction();

    if (!isThreadConsumingDeferredFunctions_) {
      loop();
    }
  };

 protected:
  // This is the actual long-running event loop, which is implemented by
  // subclasses and called inside the thread owned by this parent class.
  virtual void eventLoop() = 0;

  // This function is called by the parent class when a function is deferred to
  // it, and must be implemented by subclasses, which are required to have their
  // event loop call runDeferredFunctionsFromEventLoop as soon as possible. This
  // function is guaranteed to be called once per function deferral (in case
  // subclasses want to keep count).
  virtual void wakeupEventLoopToDeferFunction() = 0;

  // Called by subclasses to have the parent class start the thread. We cannot
  // implicitly call this in the parent class's constructor because it could
  // lead to a race condition between the event loop (run by the thread) and the
  // subclass's constructor (which is executed after the parent class's one).
  // Hence this method should be invoked at the end of the subclass constructor.
  void startThread(std::string threadName) {
    thread_ = std::thread(
        &EventLoopDeferredExecutor::loop_, this, std::move(threadName));
  }

  // This is basically the reverse operation of the above, and is needed for the
  // same (reversed) reason. Note that this only waits for the thread to finish:
  // the subclass must have its own way of telling its event loop to stop and
  // return control.
  void joinThread() {
    thread_.join();
  }

  // Must be called by the subclass after it was woken up. Even if multiple
  // functions were deferred, this method only needs to be called once. However,
  // care must be taken to avoid races between this call and new wakeups. This
  // method also returns the number of functions it executed, in case the
  // subclass is keeping count.
  size_t runDeferredFunctionsFromEventLoop() {
    return runDeferredFunctions();
  }

 private:

  void loop_(std::string threadName) {
    setThreadName(std::move(threadName));

    currentLoop_ = std::this_thread::get_id();
    eventLoop();
    currentLoop_ = std::thread::id();

    isThreadConsumingDeferredFunctions_ = false;
    loop();
  }

  std::thread thread_;

  // Whether the thread is still taking care of running the deferred functions
  //
  // This is part of what can only be described as a hack. Sometimes, even when
  // using the API as intended, objects try to defer tasks to the loop after
  // that loop has been closed and joined. Since those tasks may be lambdas that
  // captured shared_ptrs to the objects in their closures, this may lead to a
  // reference cycle and thus a leak. Our hack is to have this flag to record
  // when we can no longer defer tasks to the loop and in that case we just run
  // those tasks inline. In order to keep ensuring the single-threadedness
  // assumption of our model (which is what we rely on to be safe from race
  // conditions) we use an on-demand loop.
  std::atomic<bool> isThreadConsumingDeferredFunctions_{true};
};

} // namespace tensorpipe
