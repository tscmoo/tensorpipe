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
    return currentLoop_.load(std::memory_order_relaxed) == std::this_thread::get_id();
  }

  void deferToLoop(TTask fn) override {
    enqueue(fn.release());
    loop();
  }

  OnDemandDeferredExecutor() {
    head_.nextAtomic = nullptr;
  }

protected:

  struct Queue {
    struct Item {
      std::atomic<Item*> next{nullptr};
      std::array<FunctionPointer, 16> queue;
      std::atomic<size_t> rn{0};
    };
    Queue* next = nullptr;
    OnDemandDeferredExecutor* lastExecutor = nullptr;
    Item* lastItem = nullptr;
    OnDemandDeferredExecutor* secondLastExecutor = nullptr;
    Item* secondLastItem = nullptr;

    std::unordered_map<OnDemandDeferredExecutor*, Item> items;
  };

  Queue& getThreadQueue() {
    thread_local Queue queue;
    return queue;
  }

  std::atomic<Queue::Item*> queue_ = nullptr;

  std::atomic<size_t> queuedFunctionCount_ = 0;

  void enqueue(FunctionPointer ptr) {
    //printf("queue function into %p\n", this);
//    Queue& tq = getThreadQueue();
//    Queue::Item* i = tq.lastItem;
//    if (tq.lastExecutor != this) {
//      std::swap(tq.lastExecutor, tq.secondLastExecutor);
//      std::swap(tq.lastItem, tq.secondLastItem);
//      i = tq.lastItem;
//      if (tq.lastExecutor != this) {
//        auto [it, inserted] = tq.items.try_emplace(this);
//        i = &it->second;
//        if (inserted) {
//          Queue::Item* q = queue_.load(std::memory_order_relaxed);
//          do {
//            i->next = q;
//          } while (!queue_.compare_exchange_weak(q, i, std::memory_order_relaxed));

//          //printf("Item %p inserted\n", i);
//        }
//        tq.lastExecutor = this;
//        tq.lastItem = i;
//      }
//    }
    FunctionPointer next = head_.nextAtomic.load(std::memory_order_relaxed);
    do {
      ptr->nextAtomic.store(next, std::memory_order_relaxed);
    } while (!head_.nextAtomic.compare_exchange_weak(next, ptr, std::memory_order_acquire));

//    size_t offset = i->rn.load(std::memory_order_relaxed);
//    if (offset < i->queue.size()) {
//      do {
//        i->queue[offset] = ptr;
//      } while (!i->rn.compare_exchange_weak(offset, offset + 1));
//      //printf("function %p queued at offset %d of item %p\n", ptr, offset, i);
//      //i->wn = offset + 1;
//    } else {
//      //printf("global function queued :(\n");
//      FunctionPointer next = head_.nextAtomic.load(std::memory_order_relaxed);
//      do {
//        ptr->nextAtomic.store(next, std::memory_order_relaxed);
//      } while (!head_.nextAtomic.compare_exchange_weak(next, ptr, std::memory_order_acquire));
//    }

//    queuedFunctionCount_.fetch_add(1, std::memory_order_relaxed);
  }

  void loop() {
    static_assert(std::atomic<std::thread::id>::is_always_lock_free);
    do {
      auto owner = currentLoop_.load(std::memory_order_relaxed);
      if (owner != std::thread::id()) {
        return;
      }
      if (!currentLoop_.compare_exchange_strong(owner, std::this_thread::get_id(), std::memory_order_relaxed)) {
        return;
      }
      runDeferredFunctions();
      currentLoop_.store(std::thread::id(), std::memory_order_release);
    //} while (queuedFunctionCount_.load(std::memory_order_relaxed));
    } while (head_.nextAtomic.load(std::memory_order_acquire));
  }

  size_t runDeferredFunctions() {
    //printf("run %p\n", this);
//    int r = 0;
//    Queue::Item* q = queue_.load(std::memory_order_relaxed);
//    while (q) {
//      size_t n = q->rn.load(std::memory_order_acquire);
//      if (n) {
//        //printf("got an n of %d\n", n);
//        size_t i = 0;
//        do {
//          for (; i != n; ++i) {
//            //printf("execute function %p, number %d of item %p\n", q->queue[i], i, q);
//            Function<void()>{q->queue[i]}();
//            ++r;
//          }
//          //printf("done executing %d\n", i);
//        } while (!q->rn.compare_exchange_weak(n, 0));
//      }
//      q = q->next.load(std::memory_order_relaxed);
//    }
    FunctionPointer ptr = head_.nextAtomic.load(std::memory_order_relaxed);
    if (!ptr) {
      return 0;
    }
    ptr = head_.nextAtomic.exchange(nullptr, std::memory_order_acquire);
    return unrollEventLoopStack(ptr);
  }

  size_t unrollEventLoopStack(FunctionPointer ptr, int depth = 0) {
    std::array<FunctionPointer, 16> stack;
    size_t n = 0;
    do {
      stack[n] = ptr;
      ++n;
      ptr = ptr->nextAtomic.load(std::memory_order_relaxed);
    } while (ptr && n != stack.size());
    size_t index = n;
    if (ptr) {
      n += depth != 16 ? unrollEventLoopStack(ptr, depth + 1) : unrollEventLoopDynamic(ptr);
    }
    while (index) {
      --index;
      Function<void()>{stack[index]}();
    }
    return n;
  }

  size_t unrollEventLoopDynamic(FunctionPointer ptr) {
    std::vector<FunctionPointer> vec;
    do {
      vec.push_back(ptr);
      ptr = ptr->nextAtomic.load(std::memory_order_relaxed);
    } while (ptr);
    for (size_t i = vec.size(); i;) {
      --i;
      Function<void()>{vec[i]}();
    }
    return vec.size();
  }

  std::atomic<std::thread::id> currentLoop_;

  std::remove_pointer_t<FunctionPointer> head_;
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

  EventLoopDeferredExecutor() {
    head_.nextAtomic = nullptr;
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
