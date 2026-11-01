// 链路还原：Span 树、关键路径、首个错误、服务拓扑。
#pragma once

#include <string>
#include <vector>

#include "model.h"

namespace vmon {
namespace server {

TraceSummary summarize_trace(const TraceData& trace);

// 组装 Span 树。父 Span 不在本 trace 内时按根处理；对环做保护。
void build_trace_tree(const TraceData& trace, std::vector<TraceNode>* roots);

// 关键路径（按子树耗时最长的链路）。
std::vector<SpanData> critical_path(const TraceData& trace);

TraceAnalysis analyze_trace(const TraceData& trace);

// 从若干 trace 中提取服务级调用边。跨服务父子关系为强边；
// attrs.confidence == "weak" 的客户端 Span 单独标记，避免污染调用链结论。
std::vector<ServiceEdge> build_topology(const std::vector<TraceData>& traces);

}  // namespace server
}  // namespace vmon
