// Agent 侧指标事件构造。
//
// Agent 不直接理解具体存储，只把采集到的数值转成统一的 type=metric 事件，
// 与日志、链路走同一条 WAL/上传通道。这样中心服务无需为指标增加额外协议，
// 断网续传、鉴权与批量上传也能直接复用。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "json_event.h"
#include "process_scan.h"

namespace vmon {
namespace agent {

// 主机级指标：负载、内存、进程数、uptime。
// 统一使用 SI 单位（字节/秒），避免下游再猜 kB 还是 MB。
std::vector<common::Event> host_metric_events(const HostInfo& info,
                                              size_t process_count,
                                              const std::string& node,
                                              const std::string& agent);

// 单进程指标：RSS 与线程数。默认关闭，避免 200+ 进程 × 高频扫描产生高基数。
common::Event process_metric_event(const ProcessInfo& info,
                                   const std::string& node,
                                   const std::string& agent);

}  // namespace agent
}  // namespace vmon
