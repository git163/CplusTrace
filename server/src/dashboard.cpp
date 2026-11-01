#include "dashboard.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include "store.h"
#include "util.h"

namespace vmon {
namespace server {
namespace {

DashboardWidget widget_from_json(const nlohmann::json& json) {
  DashboardWidget widget;
  widget.id = json.value("id", std::string());
  widget.type = json.value("type", std::string("metric"));
  widget.title = json.value("title", std::string());
  widget.metric = json.value("metric", std::string());
  widget.service = json.value("service", std::string());
  widget.text = json.value("text", std::string());
  widget.limit = json.value("limit", static_cast<size_t>(5));
  return widget;
}

}  // namespace

nlohmann::json DashboardStore::widget_to_json(const DashboardWidget& widget) {
  nlohmann::json json;
  json["id"] = widget.id;
  json["type"] = widget.type;
  json["title"] = widget.title;
  if (!widget.metric.empty()) json["metric"] = widget.metric;
  if (!widget.service.empty()) json["service"] = widget.service;
  if (!widget.text.empty()) json["text"] = widget.text;
  json["limit"] = widget.limit;
  return json;
}

nlohmann::json DashboardStore::dashboard_to_json(const Dashboard& dashboard) {
  nlohmann::json json;
  json["id"] = dashboard.id;
  json["name"] = dashboard.name;
  json["description"] = dashboard.description;
  nlohmann::json widgets = nlohmann::json::array();
  for (const DashboardWidget& widget : dashboard.widgets) {
    widgets.push_back(widget_to_json(widget));
  }
  json["widgets"] = widgets;
  json["created_ns"] = dashboard.created_ns;
  json["updated_ns"] = dashboard.updated_ns;
  json["created"] = iso_time(dashboard.created_ns);
  json["updated"] = iso_time(dashboard.updated_ns);
  return json;
}

nlohmann::json DashboardStore::snapshot_to_json(
    const DashboardSnapshot& snapshot) {
  return {{"id", snapshot.id},
          {"dashboard_id", snapshot.dashboard_id},
          {"name", snapshot.name},
          {"created_ns", snapshot.created_ns},
          {"created", iso_time(snapshot.created_ns)},
          {"data", snapshot.data}};
}

bool DashboardStore::load(const std::string& path, std::string* error) {
  std::ifstream in(path);
  if (!in) return true;
  try {
    nlohmann::json json;
    in >> json;
    std::lock_guard<std::mutex> lock(mutex_);
    dashboards_.clear();
    snapshots_.clear();
    if (json.contains("dashboards") && json["dashboards"].is_array()) {
      for (const auto& item : json["dashboards"]) {
        Dashboard dashboard;
        dashboard.id = item.value("id", std::string());
        dashboard.name = item.value("name", std::string());
        dashboard.description = item.value("description", std::string());
        dashboard.created_ns = item.value("created_ns", static_cast<uint64_t>(0));
        dashboard.updated_ns = item.value("updated_ns", static_cast<uint64_t>(0));
        if (item.contains("widgets") && item["widgets"].is_array()) {
          for (const auto& widget_json : item["widgets"]) {
            dashboard.widgets.push_back(widget_from_json(widget_json));
          }
        }
        if (!dashboard.id.empty()) dashboards_.push_back(dashboard);
      }
    }
    if (json.contains("snapshots") && json["snapshots"].is_array()) {
      for (const auto& item : json["snapshots"]) {
        DashboardSnapshot snapshot;
        snapshot.id = item.value("id", std::string());
        snapshot.dashboard_id = item.value("dashboard_id", std::string());
        snapshot.name = item.value("name", std::string());
        snapshot.created_ns = item.value("created_ns", static_cast<uint64_t>(0));
        snapshot.data = item.value("data", nlohmann::json::object());
        if (!snapshot.id.empty()) snapshots_.push_back(snapshot);
      }
    }
    return true;
  } catch (const std::exception& ex) {
    if (error) *error = std::string("仪表板配置解析失败: ") + ex.what();
    return false;
  }
}

bool DashboardStore::save(const std::string& path, std::string* error) const {
  nlohmann::json json;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    json["dashboards"] = nlohmann::json::array();
    for (const Dashboard& dashboard : dashboards_) {
      json["dashboards"].push_back(dashboard_to_json(dashboard));
    }
    json["snapshots"] = nlohmann::json::array();
    for (const DashboardSnapshot& snapshot : snapshots_) {
      json["snapshots"].push_back(snapshot_to_json(snapshot));
    }
  }
  const std::string tmp = path + ".tmp";
  std::ofstream out(tmp, std::ios::trunc);
  if (!out) {
    if (error) *error = "无法写入仪表板配置: " + tmp;
    return false;
  }
  out << json.dump(2) << "\n";
  out.close();
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    if (error) *error = "无法替换仪表板配置: " + path;
    return false;
  }
  return true;
}

std::vector<Dashboard> DashboardStore::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dashboards_;
}

bool DashboardStore::get(const std::string& id, Dashboard* out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const Dashboard& dashboard : dashboards_) {
    if (dashboard.id == id) {
      if (out) *out = dashboard;
      return true;
    }
  }
  return false;
}

bool DashboardStore::upsert_json(const nlohmann::json& json, uint64_t now,
                                 Dashboard* out, std::string* error) {
  if (!json.is_object()) {
    if (error) *error = "仪表板必须是 JSON 对象";
    return false;
  }
  Dashboard dashboard;
  dashboard.id = json.value("id", std::string());
  dashboard.name = json.value("name", std::string());
  dashboard.description = json.value("description", std::string());
  dashboard.created_ns = now;
  dashboard.updated_ns = now;
  if (dashboard.name.empty()) {
    if (error) *error = "仪表板需要 name";
    return false;
  }
  if (dashboard.id.empty()) {
    dashboard.id = "dash-" + hex_hash(dashboard.name + std::to_string(now))
                                .substr(0, 10);
  }
  if (json.contains("widgets") && json["widgets"].is_array()) {
    for (const auto& widget_json : json["widgets"]) {
      DashboardWidget widget = widget_from_json(widget_json);
      if (widget.id.empty()) {
        widget.id = "w-" + std::to_string(dashboard.widgets.size() + 1);
      }
      if (widget.title.empty()) widget.title = widget.metric;
      if (widget.type != "metric" && widget.type != "traces" &&
          widget.type != "logs" && widget.type != "text") {
        if (error) *error = "未知 widget 类型: " + widget.type;
        return false;
      }
      dashboard.widgets.push_back(widget);
    }
  }
  if (dashboard.widgets.empty()) {
    if (error) *error = "仪表板至少需要一个 widget";
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  for (Dashboard& existing : dashboards_) {
    if (existing.id == dashboard.id) {
      dashboard.created_ns = existing.created_ns;
      existing = dashboard;
      if (out) *out = existing;
      return true;
    }
  }
  dashboards_.push_back(dashboard);
  if (out) *out = dashboard;
  return true;
}

bool DashboardStore::remove(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = dashboards_.begin(); it != dashboards_.end(); ++it) {
    if (it->id == id) {
      dashboards_.erase(it);
      return true;
    }
  }
  return false;
}

nlohmann::json DashboardStore::data(const Dashboard& dashboard,
                                    const Store& store, uint64_t now_ns) const {
  nlohmann::json widgets = nlohmann::json::array();
  for (const DashboardWidget& widget : dashboard.widgets) {
    nlohmann::json item = widget_to_json(widget);
    if (widget.type == "metric") {
      const uint64_t since = now_ns > 3600000000000ull
                                 ? now_ns - 3600000000000ull
                                 : 0;
      const std::vector<MetricPoint> points =
          store.query_metrics(widget.metric, since, 300);
      nlohmann::json values = nlohmann::json::array();
      double min = 0.0;
      double max = 0.0;
      bool first = true;
      for (const MetricPoint& point : points) {
        values.push_back(point.value);
        if (first) {
          min = max = point.value;
          first = false;
        } else {
          min = std::min(min, point.value);
          max = std::max(max, point.value);
        }
      }
      item["values"] = values;
      item["count"] = points.size();
      item["last"] = points.empty() ? 0.0 : points.back().value;
      item["min"] = min;
      item["max"] = max;
    } else if (widget.type == "traces") {
      TraceQuery query;
      query.service = widget.service;
      query.since_ns = now_ns > 300000000000ull ? now_ns - 300000000000ull : 0;
      query.limit = 500;
      const std::vector<TraceSummary> traces = store.query_traces(query);
      std::vector<uint64_t> durations;
      size_t errors = 0;
      nlohmann::json recent = nlohmann::json::array();
      for (const TraceSummary& trace : traces) {
        durations.push_back(trace.duration_ns);
        if (trace.has_error) ++errors;
      }
      std::sort(durations.begin(), durations.end());
      double p95 = 0.0;
      if (!durations.empty()) {
        size_t index = static_cast<size_t>(durations.size() * 0.95);
        if (index >= durations.size()) index = durations.size() - 1;
        p95 = static_cast<double>(durations[index]) / 1000000.0;
      }
      for (size_t i = 0; i < traces.size() && i < widget.limit; ++i) {
        recent.push_back({{"trace_id", traces[i].trace_id},
                          {"request_id", traces[i].request_id},
                          {"service", traces[i].root_service},
                          {"name", traces[i].root_name},
                          {"duration_ms", static_cast<double>(traces[i].duration_ns) / 1000000.0},
                          {"has_error", traces[i].has_error}});
      }
      item["count"] = traces.size();
      item["errors"] = errors;
      item["error_rate"] = traces.empty()
                               ? 0.0
                               : static_cast<double>(errors) * 100.0 /
                                     static_cast<double>(traces.size());
      item["p95_ms"] = p95;
      item["recent"] = recent;
    } else if (widget.type == "logs") {
      nlohmann::json clusters = nlohmann::json::array();
      for (const LogClusterInfo& cluster : store.log_clusters(widget.limit)) {
        clusters.push_back({{"fingerprint", cluster.id},
                            {"template", cluster.tmpl},
                            {"level", cluster.level},
                            {"service", cluster.service},
                            {"count", cluster.count}});
      }
      item["clusters"] = clusters;
    } else if (widget.type == "text") {
      item["text"] = widget.text;
    }
    widgets.push_back(item);
  }
  return {{"id", dashboard.id},
          {"name", dashboard.name},
          {"description", dashboard.description},
          {"created_ns", now_ns},
          {"widgets", widgets}};
}

bool DashboardStore::create_snapshot(const std::string& dashboard_id,
                                     const Store& store, uint64_t now_ns,
                                     DashboardSnapshot* out,
                                     std::string* error) {
  Dashboard dashboard;
  if (!get(dashboard_id, &dashboard)) {
    if (error) *error = "仪表板不存在: " + dashboard_id;
    return false;
  }
  DashboardSnapshot snapshot;
  snapshot.id = "snap-" + hex_hash(dashboard_id + std::to_string(now_ns))
                            .substr(0, 12);
  snapshot.dashboard_id = dashboard_id;
  snapshot.name = dashboard.name;
  snapshot.created_ns = now_ns;
  snapshot.data = data(dashboard, store, now_ns);

  std::lock_guard<std::mutex> lock(mutex_);
  snapshots_.push_back(snapshot);
  while (snapshots_.size() > 100) snapshots_.erase(snapshots_.begin());
  if (out) *out = snapshot;
  return true;
}

std::vector<DashboardSnapshot> DashboardStore::snapshots() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshots_;
}

bool DashboardStore::get_snapshot(const std::string& id,
                                  DashboardSnapshot* out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const DashboardSnapshot& snapshot : snapshots_) {
    if (snapshot.id == id) {
      if (out) *out = snapshot;
      return true;
    }
  }
  return false;
}

}  // namespace server
}  // namespace vmon
