// 模型 → JSON 的统一序列化，供 HTTP API、诊断、证据包共用。
#pragma once

#include <nlohmann/json.hpp>

#include "catalog.h"
#include "commands.h"
#include "crash.h"
#include "log_cluster.h"
#include "model.h"
#include "profiler.h"
#include "rules.h"

namespace vmon {
namespace server {

nlohmann::json span_to_json(const SpanData& span);
nlohmann::json trace_node_to_json(const TraceNode& node);
nlohmann::json analysis_to_json(const TraceData& trace,
                                const TraceAnalysis& analysis);
nlohmann::json log_to_json(const LogData& log);
nlohmann::json cluster_to_json(const LogClusterInfo& cluster);
nlohmann::json event_to_json(const EventRecord& event);
nlohmann::json crash_to_json(const CrashRecord& crash);
nlohmann::json profile_to_json(const ProfileRecord& profile,
                               size_t top_limit = 20);
nlohmann::json command_to_json(const CommandRecord& command);

nlohmann::json catalog_node_to_json(const NodeRecord& node);
nlohmann::json instance_to_json(const InstanceRecord& instance);
nlohmann::json process_to_json(const ProcessRecord& process);

}  // namespace server
}  // namespace vmon
