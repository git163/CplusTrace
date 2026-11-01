// 每线程 SPSC 无锁环形队列。
//
// - 单生产者（业务线程）/ 单消费者（SDK 后台 flush 线程）；
// - 容量必须是 2 的幂，实际可存放 Capacity-1 条；
// - push 永不阻塞：满则丢弃并累加 dropped 计数（可观测，不反压业务）；
// - head/tail 分处不同 cacheline，避免伪共享；
// - 只接受可平凡拷贝的类型。
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace vmon {
namespace detail {

template <typename T, std::size_t Capacity>
class RingBuffer {
  static_assert(Capacity >= 2, "容量至少为 2");
  static_assert((Capacity & (Capacity - 1)) == 0, "容量必须是 2 的幂");
  static_assert(std::is_trivially_copyable<T>::value, "只支持可平凡拷贝的类型");

 public:
  RingBuffer() noexcept = default;

  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;

  // 生产者调用。满时返回 false 并计数。
  bool push(const T& value) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = (head + 1) & kMask;
    if (next == tail_.load(std::memory_order_acquire)) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    buffer_[head] = value;
    head_.store(next, std::memory_order_release);
    return true;
  }

  // 消费者调用。空时返回 false。
  bool pop(T& out) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;
    }
    out = buffer_[tail];
    tail_.store((tail + 1) & kMask, std::memory_order_release);
    return true;
  }

  bool empty() const noexcept {
    return tail_.load(std::memory_order_acquire) ==
           head_.load(std::memory_order_acquire);
  }

  uint64_t dropped() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
  }

  static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

 private:
  static constexpr std::size_t kMask = Capacity - 1;

  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
  alignas(64) std::atomic<uint64_t> dropped_{0};
  T buffer_[Capacity]{};
};

}  // namespace detail
}  // namespace vmon
