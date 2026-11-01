// 自定义 Socket / IPC 协议的 trace 头编解码。
//
// 无法使用 gRPC metadata 的链路，在消息头预留固定 24 字节：
//   trace_hi (8) | trace_lo (8) | span (8)，均为大端（网络字节序）。
// 全 0 表示“无 trace 上下文”，接收方应视为低置信度/弱关联。
//
// 本头文件只负责编码格式，不改变任何 Socket 读写逻辑；业务在发送前
// 写入 24 字节头，接收后解码并调用 create_span_with_parent 恢复上下文。
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <vmon/vmon.h>

namespace vmon {
namespace adapters {

static const size_t kSocketTraceHeaderSize = 24;

inline void put_u64_be(uint8_t* out, uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out[i] = static_cast<uint8_t>((value >> ((7 - i) * 8)) & 0xFFu);
  }
}

inline uint64_t get_u64_be(const uint8_t* in) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | static_cast<uint64_t>(in[i]);
  }
  return value;
}

// 编码当前上下文；无效上下文输出全 0，不抛异常、不分配。
inline void encode_socket_header(const SpanContext& context,
                                 uint8_t out[kSocketTraceHeaderSize]) {
  std::memset(out, 0, kSocketTraceHeaderSize);
  if (!context.valid || (context.trace_hi == 0 && context.trace_lo == 0) ||
      context.span == 0) {
    return;
  }
  put_u64_be(out, context.trace_hi);
  put_u64_be(out + 8, context.trace_lo);
  put_u64_be(out + 16, context.span);
}

// 解码 24 字节头；长度不足、全 0 或 span 为 0 时返回 false。
inline bool decode_socket_header(const uint8_t* data, size_t size,
                                 SpanContext* context) {
  if (context == nullptr || data == nullptr ||
      size < kSocketTraceHeaderSize) {
    return false;
  }
  const uint64_t hi = get_u64_be(data);
  const uint64_t lo = get_u64_be(data + 8);
  const uint64_t span = get_u64_be(data + 16);
  if ((hi == 0 && lo == 0) || span == 0) return false;
  context->trace_hi = hi;
  context->trace_lo = lo;
  context->span = span;
  context->valid = true;
  return true;
}

}  // namespace adapters
}  // namespace vmon
