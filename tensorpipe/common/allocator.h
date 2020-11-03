
#pragma once

#include <cstddef>
#include <cstdlib>
#include <type_traits>
#include <atomic>

struct TestAllocatorTag {};

template<typename T, TestAllocatorTag* tag = nullptr, bool constructObjects = false>
class TestAllocator {
public:
  using value_type = T;

  TestAllocator() = default;
  template<typename U>
  constexpr TestAllocator(const TestAllocator<U>&) noexcept {}

  template<typename U>
  struct rebind {typedef TestAllocator<U, tag> other;};

  T* allocate(size_t n) {
    if (n == 1) {
      return allocateImpl<false>(1);
    } else {
      return allocateImpl<true>(n);
    }
  }
  void deallocate(T* ptr, size_t n) {
    if (n == 1) {
      deallocateImpl<false>(ptr, 1);
    } else {
      deallocateImpl<true>(ptr, n);
    }
  }
private:

  struct alignas(std::max_align_t) Storage {
    Storage* next = nullptr;
    size_t allocated = 0;
    T* object() {
      return (T*)(void*)(this + 1);
    }
    static Storage* from(T* ptr) {
      return (Storage*)(void*)ptr - 1;
    }
  };

  struct TLSList {
    Storage* list = nullptr;
    size_t size = 0;

    bool empty() const {
      return list == nullptr;
    }
    Storage* extract() {
      Storage* r = list;
      list = list->next;
      --size;
      return r;
    }
    void insert(T* ptr) {
      Storage* s = Storage::from(ptr);
      s->next = list;
      list = s;
    }
  };

  struct GlobalList {
    std::atomic<Storage*> list = nullptr;
  };

  template<bool multi>
  TLSList& freeList() {
    thread_local TLSList list;
    return list;
  }

  template<bool multi>
  T* allocateImpl(size_t n) {
    Storage* s;
    TLSList& tls = freeList<multi>();
    bool construct = false;
    if (tls.empty()) {
      s = (Storage*)std::malloc(sizeof(Storage) + sizeof(T) * n);
      s->allocated = n;
      construct = constructObjects;
    } else {
      s = tls.extract();
      if (multi && s->allocated < n) {
        if (constructObjects) {
          s->object()->~T();
        }
        std::free(s);
        Storage* s = (Storage*)std::malloc(sizeof(Storage) + sizeof(T) * n);
        s->allocated = n;
        construct = constructObjects;
      }
    }
    if (constructObjects && construct) {
      try {
        return new (s->object()) T();
      } catch (...) {
        std::free(s);
        throw;
      }
    }
    return s->object();
  }
  template<bool multi>
  void deallocateImpl(T* ptr, size_t n) {
    TLSList& tls = freeList<multi>();
    if (true || tls.size < 1024) {
      tls.insert(ptr);
    } else {
      std::abort();
    }
  }
};


template <typename T, typename U>
inline bool operator == (const TestAllocator<T>&, const TestAllocator<U>&) {
    return true;
}

template <typename T, typename U>
inline bool operator != (const TestAllocator<T>& a, const TestAllocator<U>& b) {
    return !(a == b);
}

