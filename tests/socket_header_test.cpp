#include <gtest/gtest.h>

#include <cstdint>

#include "adapters/ipc/socket_header.h"

namespace {

using vmon::adapters::decode_socket_header;
using vmon::adapters::encode_socket_header;
using vmon::adapters::kSocketTraceHeaderSize;

TEST(SocketHeader, RoundTripBigEndian) {
  vmon::SpanContext context;
  context.trace_hi = 0x0123456789abcdefull;
  context.trace_lo = 0xfedcba9876543210ull;
  context.span = 0x1111222233334444ull;
  context.valid = true;

  uint8_t header[kSocketTraceHeaderSize];
  encode_socket_header(context, header);
  // 网络字节序：高位在前。
  EXPECT_EQ(0x01, header[0]);
  EXPECT_EQ(0xef, header[7]);
  EXPECT_EQ(0x10, header[15]);
  EXPECT_EQ(0x44, header[23]);

  vmon::SpanContext decoded;
  ASSERT_TRUE(decode_socket_header(header, sizeof(header), &decoded));
  EXPECT_TRUE(decoded.valid);
  EXPECT_EQ(context.trace_hi, decoded.trace_hi);
  EXPECT_EQ(context.trace_lo, decoded.trace_lo);
  EXPECT_EQ(context.span, decoded.span);
}

TEST(SocketHeader, InvalidContextEncodesAllZero) {
  vmon::SpanContext context;
  uint8_t header[kSocketTraceHeaderSize];
  encode_socket_header(context, header);
  for (size_t i = 0; i < kSocketTraceHeaderSize; ++i) {
    EXPECT_EQ(0, header[i]);
  }

  vmon::SpanContext decoded;
  EXPECT_FALSE(decode_socket_header(header, sizeof(header), &decoded));
}

TEST(SocketHeader, RejectsShortBufferAndMissingSpan) {
  vmon::SpanContext context;
  context.trace_hi = 1;
  context.trace_lo = 2;
  context.span = 3;
  context.valid = true;
  uint8_t header[kSocketTraceHeaderSize];
  encode_socket_header(context, header);

  vmon::SpanContext decoded;
  EXPECT_FALSE(decode_socket_header(header, kSocketTraceHeaderSize - 1,
                                    &decoded));
  EXPECT_FALSE(decode_socket_header(nullptr, kSocketTraceHeaderSize, &decoded));
  EXPECT_FALSE(decode_socket_header(header, sizeof(header), nullptr));

  // span 为 0 的头也视为无效。
  uint8_t no_span[kSocketTraceHeaderSize] = {0};
  no_span[0] = 1;
  EXPECT_FALSE(decode_socket_header(no_span, sizeof(no_span), &decoded));
}

}  // namespace
