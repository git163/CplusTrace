#include "trace_builder.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace vmon {
namespace server {
namespace {

void build_children(const TraceData& trace, const std::string& span_id,
                    std::unordered_set<std::string>* ancestors, int depth,
                    TraceNode* out) {
  if (depth > 128 || ancestors->count(span_id) != 0) return;
  ancestors->insert(span_id);

  std::vector<const SpanData*> children;
  for (const auto& kv : trace.spans) {
    const SpanData& span = kv.second;
    if (span.parent == span_id) children.push_back(&span);
  }
  std::sort(children.begin(), children.end(),
            [](const SpanData* a, const SpanData* b) {
              return a->start_ns < b->start_ns;
            });
  for (const SpanData* child : children) {
    TraceNode node;
    node.span = *child;
    node.duration_ns = child->duration_ns();
    build_children(trace, child->span, ancestors, depth + 1, &node);
    out->children.push_back(std::move(node));
  }
  ancestors->erase(span_id);
}

void collect_nodes(const TraceNode& node, int depth, uint32_t* max_depth,
                   std::vector<const TraceNode*>* flat) {
  if (depth > static_cast<int>(*max_depth)) *max_depth = static_cast<uint32_t>(depth);
  flat->push_back(&node);
  for (const auto& child : node.children) {
    collect_nodes(child, depth + 1, max_depth, flat);
  }
}

}  // namespace

TraceSummary summarize_trace(const TraceData& trace) {
  TraceSummary summary;
  summary.trace_id = trace.trace_id;
  summary.request_id = trace.request_id;
  summary.task_id = trace.task_id;
  summary.root_service = trace.root_service;
  summary.root_name = trace.root_name;
  summary.start_ns = trace.first_ns;
  summary.span_count = static_cast<uint32_t>(trace.spans.size());

  uint64_t end_ns = trace.first_ns;
  std::set<std::string> services;
  for (const auto& kv : trace.spans) {
    const SpanData& span = kv.second;
    if (!span.service.empty()) services.insert(span.service);
    if (span.end_ns > end_ns) end_ns = span.end_ns;
    if (span.is_error()) {
      summary.has_error = true;
      auto previous = trace.spans.find(summary.first_error_span);
      if (summary.first_error_span.empty() || previous == trace.spans.end() ||
          span.start_ns < previous->second.start_ns) {
        summary.first_error_span = span.span;
        summary.error_code = span.error_code;
      }
    }
  }
  summary.service_count = static_cast<uint32_t>(services.size());
  summary.duration_ns = end_ns > trace.first_ns ? end_ns - trace.first_ns : 0;

  std::vector<TraceNode> roots;
  build_trace_tree(trace, &roots);
  uint32_t depth = 0;
  std::vector<const TraceNode*> flat;
  for (const auto& root : roots) collect_nodes(root, 1, &depth, &flat);
  summary.depth = depth;
  return summary;
}

void build_trace_tree(const TraceData& trace, std::vector<TraceNode>* roots) {
  if (roots == nullptr) return;
  roots->clear();
  std::unordered_set<std::string> ids;
  ids.reserve(trace.spans.size() * 2);
  for (const auto& kv : trace.spans) ids.insert(kv.first);

  std::vector<const SpanData*> root_spans;
  for (const auto& kv : trace.spans) {
    const SpanData& span = kv.second;
    if (span.parent.empty() || ids.count(span.parent) == 0) {
      root_spans.push_back(&span);
    }
  }
  std::sort(root_spans.begin(), root_spans.end(),
            [](const SpanData* a, const SpanData* b) {
              return a->start_ns < b->start_ns;
            });

  for (const SpanData* span : root_spans) {
    TraceNode root;
    root.span = *span;
    root.duration_ns = span->duration_ns();
    std::unordered_set<std::string> ancestors;
    build_children(trace, span->span, &ancestors, 1, &root);
    roots->push_back(std::move(root));
  }
}

std::vector<SpanData> critical_path(const TraceData& trace) {
  std::vector<TraceNode> roots;
  build_trace_tree(trace, &roots);
  if (roots.empty()) return {};

  // 在树中找「子树累计耗时」最大的路径。
  std::unordered_map<const TraceNode*, uint64_t> totals;
  std::vector<const TraceNode*> order;

  // 后序收集，避免递归 lambda（C++14 可用 lambda，但显式栈更直观）。
  struct Frame {
    const TraceNode* node;
    size_t child_index;
  };
  for (const auto& root : roots) {
    std::vector<Frame> stack;
    stack.push_back(Frame{&root, 0});
    while (!stack.empty()) {
      Frame& frame = stack.back();
      if (frame.child_index < frame.node->children.size()) {
        const TraceNode* child = &frame.node->children[frame.child_index];
        ++frame.child_index;
        stack.push_back(Frame{child, 0});
      } else {
        uint64_t total = frame.node->duration_ns;
        for (const auto& child : frame.node->children) {
          total += totals[&child];
        }
        totals[frame.node] = total;
        order.push_back(frame.node);
        stack.pop_back();
      }
    }
  }

  const TraceNode* best = nullptr;
  uint64_t best_total = 0;
  for (const auto& root : roots) {
    const uint64_t total = totals[&root];
    if (total > best_total) {
      best = &root;
      best_total = total;
    }
  }

  std::vector<SpanData> path;
  while (best != nullptr) {
    path.push_back(best->span);
    const TraceNode* next = nullptr;
    uint64_t next_total = 0;
    for (const auto& child : best->children) {
      const uint64_t total = totals[&child];
      if (total > next_total) {
        next_total = total;
        next = &child;
      }
    }
    best = next;
  }
  return path;
}

TraceAnalysis analyze_trace(const TraceData& trace) {
  TraceAnalysis analysis;
  analysis.summary = summarize_trace(trace);
  build_trace_tree(trace, &analysis.roots);
  analysis.critical_path = critical_path(trace);

  for (const auto& kv : trace.spans) {
    const SpanData& span = kv.second;
    if (span.is_error() &&
        (!analysis.has_error || span.start_ns < analysis.first_error.start_ns)) {
      analysis.has_error = true;
      analysis.first_error = span;
    }
    if (span.duration_ns() > analysis.slowest_span.duration_ns()) {
      analysis.slowest_span = span;
    }
  }
  return analysis;
}

std::vector<ServiceEdge> build_topology(const std::vector<TraceData>& traces) {
  std::map<std::string, ServiceEdge> edges;
  for (const TraceData& trace : traces) {
    for (const auto& kv : trace.spans) {
      const SpanData& span = kv.second;
      auto parent_it = trace.spans.find(span.parent);
      if (parent_it == trace.spans.end()) continue;
      const SpanData& parent = parent_it->second;
      if (parent.service.empty() || span.service.empty()) continue;
      if (parent.service == span.service) continue;

      const bool weak = span.attrs.count("confidence") != 0 &&
                        span.attrs.at("confidence") == "weak";
      const std::string key =
          parent.service + "\x1f" + span.service + "\x1f" + span.name + "\x1f" +
          (weak ? "weak" : "strong");
      ServiceEdge& edge = edges[key];
      edge.caller = parent.service;
      edge.callee = span.service;
      edge.method = span.name;
      edge.confidence = weak ? "weak" : "strong";
      ++edge.count;
      if (span.is_error()) ++edge.error_count;
      edge.total_duration_ns += span.duration_ns();
      if (span.duration_ns() > edge.max_duration_ns) {
        edge.max_duration_ns = span.duration_ns();
      }
    }
  }

  std::vector<ServiceEdge> out;
  out.reserve(edges.size());
  for (const auto& kv : edges) out.push_back(kv.second);
  std::sort(out.begin(), out.end(),
            [](const ServiceEdge& a, const ServiceEdge& b) {
              return a.count > b.count;
            });
  return out;
}

}  // namespace server
}  // namespace vmon
