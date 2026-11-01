// 数据洞察：轻量仪表板与快照。
//
// 目标不是拖拽式 BI，而是把「常用指标 + 服务错误率 + 日志 Top 模板」固定成
// 团队共用视图，并能保存某一时刻的快照用于复盘/分享。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vmon {
namespace server {

class Store;

struct DashboardWidget {
  std::string id;
  std::string type = "metric";  // metric / traces / logs / text
  std::string title;
  std::string metric;   // type=metric
  std::string service;  // type=traces（可选）
  std::string text;     // type=text
  size_t limit = 5;
};

struct Dashboard {
  std::string id;
  std::string name;
  std::string description;
  std::vector<DashboardWidget> widgets;
  uint64_t created_ns = 0;
  uint64_t updated_ns = 0;
};

struct DashboardSnapshot {
  std::string id;
  std::string dashboard_id;
  std::string name;
  uint64_t created_ns = 0;
  nlohmann::json data;
};

class DashboardStore {
 public:
  bool load(const std::string& path, std::string* error);
  bool save(const std::string& path, std::string* error) const;

  std::vector<Dashboard> list() const;
  bool get(const std::string& id, Dashboard* out) const;
  bool upsert_json(const nlohmann::json& json, uint64_t now_ns,
                   Dashboard* out, std::string* error);
  bool remove(const std::string& id);

  // 计算仪表板当前数据（只读 Store）。
  nlohmann::json data(const Dashboard& dashboard, const Store& store,
                      uint64_t now_ns) const;

  bool create_snapshot(const std::string& dashboard_id, const Store& store,
                       uint64_t now_ns, DashboardSnapshot* out,
                       std::string* error);
  std::vector<DashboardSnapshot> snapshots() const;
  bool get_snapshot(const std::string& id, DashboardSnapshot* out) const;

  static nlohmann::json widget_to_json(const DashboardWidget& widget);
  static nlohmann::json dashboard_to_json(const Dashboard& dashboard);
  static nlohmann::json snapshot_to_json(const DashboardSnapshot& snapshot);

 private:
  mutable std::mutex mutex_;
  std::vector<Dashboard> dashboards_;
  std::vector<DashboardSnapshot> snapshots_;
};

}  // namespace server
}  // namespace vmon
