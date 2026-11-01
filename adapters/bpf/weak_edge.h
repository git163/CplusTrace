// eBPF / 无侵入弱边推断接口（Linux 专用，默认关闭）。
//
// 目标：不改业务代码时，从连接事件推断 caller→callee，作为拓扑图的弱边，
// 不进入 Span 树结论。真正实现依赖 libbpf + 内核 BTF/uprobe，需在目标
// Linux 节点验证；macOS 开发环境只保留接口，避免把平台绑定写进主流程。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vmon {
namespace adapters {

struct WeakEdge {
  std::string caller_service;
  std::string callee_service;
  std::string method;
  uint64_t count = 0;
  uint32_t confidence = 60;  // 0–100，弱边默认低于强边
};

class WeakEdgeCollector {
 public:
  // 当前平台/构建是否具备采集能力；不满足时返回 false，调用方直接跳过。
  virtual bool available() const { return false; }

  // 拉取一批新推断的边；实现需自行限流与去重。
  virtual std::vector<WeakEdge> poll() { return std::vector<WeakEdge>(); }

  virtual ~WeakEdgeCollector() = default;
};

}  // namespace adapters
}  // namespace vmon
