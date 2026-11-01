// 中心服务内存存储。
//
// 设计原则：
//   - 查询优先：trace / request / service / fingerprint 建索引；
//   - 有界内存：所有容器都有上限，超限淘汰最旧数据；
//   - 日志治理：模板聚类、指纹、采样、去重、噪声屏蔽在写入侧完成；
//   - 线程安全：HTTP 线程池与规则线程共享，统一加锁。
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "catalog.h"
#include "json_event.h"
#include "log_cluster.h"
#include "model.h"

namespace vmon {
namespace server {

struct StoreConfig {
  size_t max_traces = 20000;
  size_t max_log_records = 200000;
  size_t per_key_logs = 5000;
  size_t max_metrics = 100000;
  size_t max_events = 5000;
  uint64_t retention_ns = 6ull * 3600 * 1000000000ull;
  uint32_t info_sample_permyriad = 10000;  // 10000 = 全量
  uint64_t dedup_window_ns = 30ull * 1000000000ull;
  uint32_t dedup_max_per_window = 50;  // 0 表示不限制
  std::vector<std::string> noise_patterns;
};

struct LogQuery {
  std::string trace;
  std::string request_id;
  std::string task_id;
  std::string service;
  std::string node;
  std::string level;
  std::string fingerprint;
  std::string text;
  uint64_t since_ns = 0;
  uint64_t until_ns = 0;
  size_t limit = 500;
  bool errors_only = false;
};

struct TraceQuery {
  std::string trace_id;
  std::string request_id;
  std::string task_id;
  std::string service;
  // 统一搜索：匹配 trace_id / request_id / task_id / 服务名，子串、忽略大小写。
  std::string text;
  bool errors_only = false;
  bool slow_only = false;
  uint64_t min_duration_ns = 0;
  uint64_t since_ns = 0;
  size_t limit = 100;
};

struct EventQuery {
  std::string status;
  std::string severity;
  std::string service;
  std::string type;
  size_t limit = 100;
};

struct StoreStats {
  uint64_t events_ingested = 0;
  uint64_t events_rejected = 0;
  uint64_t spans_ingested = 0;
  uint64_t logs_ingested = 0;
  uint64_t logs_sampled = 0;
  uint64_t logs_deduped = 0;
  uint64_t logs_noise = 0;
  uint64_t metric_points = 0;
  size_t trace_count = 0;
  size_t log_count = 0;
  size_t cluster_count = 0;
  size_t event_count = 0;
};

class Store {
 public:
  explicit Store(const StoreConfig& config = StoreConfig());
  ~Store();

  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  // 写入一条已解析事件；node_hint / agent 用于补齐 Agent 侧信息。
  void ingest(const common::Event& event, const std::string& node_hint,
              const std::string& agent);

  // 解析 JSONL 单行并写入；返回是否成功。
  bool ingest_line(const std::string& line, const std::string& node_hint,
                   const std::string& agent);

  bool get_trace(const std::string& trace_id, TraceData* out) const;
  std::vector<TraceSummary> query_traces(const TraceQuery& query) const;
  std::vector<TraceData> recent_traces(uint64_t since_ns, size_t limit) const;

  std::vector<LogData> query_logs(const LogQuery& query) const;
  std::vector<LogClusterInfo> log_clusters(size_t limit) const;

  std::vector<MetricPoint> query_metrics(const std::string& name,
                                         uint64_t since_ns, size_t limit) const;
  std::vector<MetricPoint> recent_metrics(uint64_t since_ns, size_t limit) const;

  void add_event(const EventRecord& event);
  // 事件新增后的回调（Webhook 等）。回调在锁外执行，不能阻塞太久。
  typedef std::function<void(const EventRecord&)> EventSink;
  void set_event_sink(EventSink sink);
  std::vector<EventRecord> list_events(size_t limit) const;
  std::vector<EventRecord> list_events(const EventQuery& query) const;
  bool acknowledge_event(const std::string& id);
  bool resolve_event(const std::string& id);

  // 请求级关联：按 request_id 找到相关 trace id。
  std::vector<std::string> traces_for_request(const std::string& request_id,
                                              size_t limit) const;

  StoreStats stats() const;
  const StoreConfig& config() const { return config_; }
  Catalog& catalog() { return catalog_; }
  const Catalog& catalog() const { return catalog_; }

  // 周期性清理（HTTP 服务后台线程调用）。
  void prune();

  // 缓存原始 JSON 事件，便于证据包导出（有界）。
  std::vector<common::Event> raw_events(uint64_t since_ns, size_t limit) const;

 private:
  struct TraceState {
    TraceData data;
    uint64_t last_touch_ns = 0;
  };

  void ingest_log(const common::Event& event, const std::string& node_hint);
  void ingest_span(const common::Event& event, const std::string& node_hint);
  void ingest_metric(const common::Event& event);
  void index_log_locked(const std::shared_ptr<LogData>& log);
  void maybe_prune_locked(uint64_t now);
  void prune_logs_locked(uint64_t cutoff);
  void prune_traces_locked(uint64_t cutoff);

  mutable std::mutex mutex_;
  StoreConfig config_;

  std::unordered_map<std::string, TraceState> traces_;
  std::deque<std::string> trace_order_;
  std::unordered_map<std::string, std::deque<std::string>> traces_by_request_;

  std::deque<std::shared_ptr<LogData>> logs_;
  std::unordered_map<std::string, std::deque<std::shared_ptr<LogData>>>
      logs_by_trace_;
  std::unordered_map<std::string, std::deque<std::shared_ptr<LogData>>>
      logs_by_request_;
  std::unordered_map<std::string, std::deque<std::shared_ptr<LogData>>>
      logs_by_service_;
  std::unordered_map<std::string, std::deque<std::shared_ptr<LogData>>>
      logs_by_fingerprint_;

  struct DedupState {
    uint64_t window_start_ns = 0;
    uint32_t count = 0;
  };
  std::unordered_map<std::string, DedupState> dedup_;

  struct MetricSeries {
    std::deque<MetricPoint> points;
  };
  std::unordered_map<std::string, MetricSeries> metrics_;

  std::deque<EventRecord> events_;
  std::deque<common::Event> raw_;
  LogClusterer clusterer_;
  Catalog catalog_;

  StoreStats stats_;
  EventSink event_sink_;
  uint64_t last_prune_ns_ = 0;
  uint64_t started_ns_ = 0;
};

}  // namespace server
}  // namespace vmon
