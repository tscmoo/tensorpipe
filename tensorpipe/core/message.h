/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <tensorpipe/core/buffer.h>

#include <atomic>

inline std::atomic<int> messageMoveCount{0};

inline void(*addaddr)() = nullptr;

namespace tensorpipe {

// Messages consist of a primary buffer and zero or more separate
// buffers. The primary buffer is always a host-side memory region that
// contains a serialized version of the message we're dealing with. This
// serialized message, in turn, may have references to the separate
// buffers that accompany the primary buffer. These separate buffers may
// point to any type of memory, host-side or device-side.
//
class Message final {
 public:
  Message() = default;

  // Messages are movable.
  Message(Message&& n) {
    *this = std::move(n);
  }
  Message& operator=(Message&& n) {
    std::swap(metadata, n.metadata);
    std::swap(payloads, n.payloads);
    std::swap(tensors, n.tensors);
    //++messageMoveCount;
    //addaddr();
    return *this;
  };

  void clear() {
    metadata.clear();
    payloads.clear();
    tensors.clear();
  }

  // But they are not copyable.
  Message(const Message&) = delete;
  Message& operator=(const Message&) = delete;

  std::string metadata;

  struct Payload {
    void* data{nullptr};
    size_t length{0};

    // Users may include arbitrary metadata in the following fields.
    // This may contain allocation hints for the receiver, for example.
    std::string metadata;
  };

  // Holds the payloads that are transferred over the primary connection.
  std::vector<Payload> payloads;

  struct Tensor {
    tensorpipe::Buffer buffer;

    // Users may include arbitrary metadata in the following field.
    // This may contain allocation hints for the receiver, for example.
    std::string metadata;
  };

  // Holds the tensors that are offered to the side channels.
  std::vector<Tensor> tensors;
};

namespace impl {

struct LinkedMessage {
  LinkedMessage* next = nullptr;
  Message message;
};

struct MessageFreeList {
  LinkedMessage* ptr = nullptr;
  bool dead = false;
  ~MessageFreeList() {
    dead = true;
    while (ptr) {
      LinkedMessage* next = ptr->next;
      delete ptr;
      ptr = next;
    }
    ptr = nullptr;
  }
  LinkedMessage* allocate() {
    if (ptr != nullptr) {
      return std::exchange(ptr, ptr->next);
    }
    return new LinkedMessage();
  }
  void deallocate(LinkedMessage* obj) {
    if (!dead) {
      obj->next = ptr;
      ptr = obj;
    } else {
      delete obj;
    }
  }
};

inline thread_local MessageFreeList messageFreeList;

}

class MessageHandle {
  impl::LinkedMessage* message_ = nullptr;
public:
  MessageHandle() {
    message_ = impl::messageFreeList.allocate();
    message_->message.clear();
  }
  MessageHandle(Message&& message) {
    message_ = impl::messageFreeList.allocate();
    message_->message = std::move(message);
  }
  MessageHandle(const MessageHandle&) = delete;
  MessageHandle(MessageHandle&& n) noexcept {
    message_ = n.message_;
    n.message_ = nullptr;
  }
  ~MessageHandle() {
    if (message_) {
      impl::messageFreeList.deallocate(message_);
    }
  }

  MessageHandle& operator=(const MessageHandle&) = delete;
  MessageHandle& operator=(MessageHandle&& n) noexcept {
    std::swap(message_, n.message_);
    return *this;
  }

  MessageHandle& operator=(Message&& message) noexcept {
    message_->message = std::move(message);
//    message_->message.metadata = message.metadata;
//    message_->message.payloads = message.payloads;
//    message_->message.tensors = message.tensors;
    return *this;
  }

  Message* operator->() noexcept {
    return &message_->message;
  }
  const Message* operator->() const noexcept {
    return &message_->message;
  }
  Message& operator*() noexcept {
    return message_->message;
  }
  const Message& operator*() const noexcept {
    return message_->message;
  }
};

} // namespace tensorpipe
