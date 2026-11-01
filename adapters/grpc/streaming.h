// gRPC streaming / 异步调用的通用 Span 辅助器。
//
// 这一层不包含 gRPC 头文件，避免绑定具体 gRPC 版本；使用方在自己的
// ClientStreaming/ServerStreaming/BidiStreaming interceptor 中回调：
//
//   vmon::adapters::StreamingSpan span;
//   span.begin(parent, "OrderService/StreamOrders");
//   span.message(n);          // 可选：记录消息大小
//   span.error("E_STREAM");
//   span.end();
//
// 打开 -DVMON_ENABLE_GRPC=ON 时仅定义 VMON_HAVE_GRPC，真正的 interceptor
// 仍需按目标工程 gRPC 版本接入（旧版 gRPC 可能没有 C++ interceptor）。
#pragma once

#include <cstddef>
#include <utility>

#include <vmon/vmon.h>

namespace vmon {
namespace adapters {

class StreamingSpan {
 public:
  StreamingSpan() = default;
  ~StreamingSpan() { end(); }

  StreamingSpan(const StreamingSpan&) = delete;
  StreamingSpan& operator=(const StreamingSpan&) = delete;

  StreamingSpan(StreamingSpan&& other) noexcept
      : span_(std::move(other.span_)),
        context_(other.context_),
        active_(other.active_) {
    other.active_ = false;
  }
  StreamingSpan& operator=(StreamingSpan&& other) noexcept {
    if (this != &other) {
      end();
      span_ = std::move(other.span_);
      context_ = other.context_;
      active_ = other.active_;
      other.active_ = false;
    }
    return *this;
  }

  // parent 无效时新建 trace，保证流式调用也能被观测。
  void begin(const SpanContext& parent, const char* name) {
    span_ = parent.valid
                ? vmon::create_span_with_parent(parent, name, __FILE__,
                                                __LINE__)
                : vmon::Span::create(name, __FILE__, __LINE__);
    context_ = vmon::current_context();
    active_ = true;
  }

  void message(size_t bytes) {
    if (!active_) return;
    VMON_DEBUG("grpc stream message bytes=%zu", bytes);
  }

  void error(const char* error_code) {
    if (active_) span_.set_error(error_code);
  }

  void end() {
    if (!active_) return;
    span_.end();
    active_ = false;
  }

  bool active() const { return active_; }
  const SpanContext& context() const { return context_; }

 private:
  vmon::Span span_;
  SpanContext context_;
  bool active_ = false;
};

}  // namespace adapters
}  // namespace vmon
