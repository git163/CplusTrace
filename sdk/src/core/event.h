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

// 血缘/状态事件（独立车道，避免撑大热路径 Event）。
enum class LineageType : uint16_t {
  kField = 1,  // 字段值指纹
  kState = 2,  // 设备状态
};

// 固定 POD：只存静态字符串指针 + 固定缓冲，不分配。
struct LineageEvent {
  uint64_t  mono_ns      = 0;
  uint64_t  wall_ns      = 0;
  uint64_t  seq          = 0;
  TraceId   trace;
  uint64_t  span         = 0;
  uint64_t  parent_span  = 0;
  uint32_t  thread_id    = 0;
  uint64_t  iteration_id = 0;
  uint64_t  value_hash   = 0;
  uint32_t  value_len    = 0;
  uint16_t  type         = 0;   // LineageType
  uint8_t   dir          = 0;   // 0 in / 1 out / 2 local
  uint8_t   kind         = 0;   // 0 str / 1 i64 / 2 f64 / 3 bool / 4 bytes / 5 opaque
  const char* run_id     = nullptr;  // 静态或长生命周期
  const char* object     = nullptr;
  const char* field      = nullptr;
  const char* device     = nullptr;  // state
  const char* state      = nullptr;  // state
  char        value[64]  = {0};      // 截断后的值（仅用于可读展示）
};

}  // namespace detail
}  // namespace vmon
