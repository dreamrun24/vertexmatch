#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace vm {

template <typename T>
class SpscRing {
 public:
  explicit SpscRing(std::size_t capacity_pow2)
      : mask_(capacity_pow2 - 1), buffer_(capacity_pow2) {
    if (capacity_pow2 == 0 || (capacity_pow2 & (capacity_pow2 - 1)) != 0) {
      throw std::invalid_argument("SpscRing capacity must be a power of two");
    }
  }

  bool push(const T& value) noexcept {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto next = (head + 1) & mask_;
    if (next == tail_.load(std::memory_order_acquire)) return false;
    buffer_[head] = value;
    head_.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T& out) noexcept {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    out = buffer_[tail];
    tail_.store((tail + 1) & mask_, std::memory_order_release);
    return true;
  }

 private:
  alignas(64) std::atomic<std::size_t> head_{0};
  char pad0_[64 - sizeof(std::atomic<std::size_t>)]{};
  alignas(64) std::atomic<std::size_t> tail_{0};
  char pad1_[64 - sizeof(std::atomic<std::size_t>)]{};
  const std::size_t mask_;
  std::vector<T> buffer_;
};

}  // namespace vm
