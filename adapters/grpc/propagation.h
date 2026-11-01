// W3C Trace Context 编解码 + gRPC metadata 约定。
//
// 这一层刻意不依赖 gRPC 头文件：可以在没有 gRPC 的环境里做单元测试，
// 真正的 interceptor 只在 VMON_HAVE_GRPC 宏打开时编译进目标工程。
#pragma once

#include <cstdint>
#include <string>

namespace vmon {
namespace adapters {

struct Traceparent {
  uint64_t trace_hi = 0;
  uint64_t trace_lo = 0;
  uint64_t span = 0;
  bool     sampled = true;
  bool     valid = false;
};

// 生成 "00-<trace_id>-<span_id>-<flags>"。
std::string make_traceparent(uint64_t trace_hi, uint64_t trace_lo,
                             uint64_t span, bool sampled);

// 解析 traceparent；格式非法时返回 false，调用方应新建 trace 而不是拒绝请求。
bool parse_traceparent(const std::string& header, Traceparent* out);

// 业务关联头（跨进程必带）；为空时不注入。
extern const char* const kHeaderTraceparent;
extern const char* const kHeaderRequestId;
extern const char* const kHeaderTaskId;

}  // namespace adapters
}  // namespace vmon
