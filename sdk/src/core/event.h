// 进程内事件（POD，可直接 memcpy/批量序列化）。
//
// 热路径只记录固定字段与静态字符串指针（__FILE__、__func__、字符串字面量），
// 不做任何动态分配。动态内容（业务字段）在后续版本通过 intern / 批量处理引入。
#pragma once

#include <cstdint>

#include "core/ids.h"

namespace vmon {
namespace detail {

enum class EventType : uint16_t {
  kSpanBegin = 1,
  kSpanEnd   = 2,
  kHeartbeat = 3,
  kLog       = 4,
};

struct Event {
  uint64_t  mono_ns      = 0;   // 进程内时序（CLOCK_MONOTONIC）
  uint64_t  wall_ns      = 0;   // 跨节点对齐（CLOCK_REALTIME）
  uint64_t  seq          = 0;   // 线程内事件序号
  TraceId   trace;              // 所属 trace
  uint64_t  span         = 0;   // 当前 span
  uint64_t  parent_span  = 0;   // 父 span，0 表示根
  uint32_t  thread_id    = 0;
  uint16_t  type         = 0;
  uint16_t  level        = 0;
  const char* name       = nullptr;  // 静态字符串，不拥有
  const char* file       = nullptr;  // 静态字符串，不拥有
  const char* request_id = nullptr;  // 线程本地指针，调用方保证请求期间有效
  const char* task_id    = nullptr;
  const char* error_code = nullptr;  // span 出错时的静态错误码，nullptr 表示正常
  int32_t   line         = 0;
  uint16_t  msg_len      = 0;        // msg 实际长度（不含结尾 0）
  uint16_t  reserved     = 0;
  char      msg[128]     = {0};      // 截断后的日志正文，固定缓冲区不分配
};

}  // namespace detail
}  // namespace vmon
