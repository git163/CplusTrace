#include <gtest/gtest.h>

#include "core/ring_buffer.h"

namespace {

using vmon::detail::RingBuffer;

TEST(RingBuffer, PushPopFifo) {
  RingBuffer<int, 8> rb;
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(rb.push(i));
  }
  int value = -1;
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(rb.pop(value));
    EXPECT_EQ(i, value);
  }
  EXPECT_FALSE(rb.pop(value));
}

TEST(RingBuffer, OverflowDropsWithoutBlocking) {
  RingBuffer<int, 4> rb;  // 实际容量 3
  EXPECT_TRUE(rb.push(1));
  EXPECT_TRUE(rb.push(2));
  EXPECT_TRUE(rb.push(3));
  EXPECT_FALSE(rb.push(4));
  EXPECT_EQ(1u, rb.dropped());

  int value = 0;
  ASSERT_TRUE(rb.pop(value));
  EXPECT_EQ(1, value);
}

TEST(RingBuffer, WrapAroundKeepsOrder) {
  RingBuffer<int, 4> rb;  // 实际容量 3
  int value = 0;

  for (int round = 0; round < 10; ++round) {
    for (int i = 0; i < 3; ++i) {
      EXPECT_TRUE(rb.push(round * 10 + i));
    }
    for (int i = 0; i < 3; ++i) {
      ASSERT_TRUE(rb.pop(value));
      EXPECT_EQ(round * 10 + i, value);
    }
  }
  EXPECT_TRUE(rb.empty());
  EXPECT_EQ(0u, rb.dropped());
}

}  // namespace
