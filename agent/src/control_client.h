// 通过 Unix Socket 向业务进程内的 SDK 控制通道发送指令。
//
// 控制通道是「尽力而为」：连不上、超时、返回非法 JSON 都只报告错误，
// 不影响 Agent 主循环与其他进程。
#pragma once

#include <cstdint>
#include <string>

namespace vmon {
namespace agent {

// 控制套接字路径：<control_dir>/<pid>.sock（与 SDK 约定一致）。
std::string control_socket_path(const std::string& control_dir, int32_t pid);

// 发送一条 JSON 请求（自动补换行），读取一行/直到 EOF 的响应。
bool send_control_command(const std::string& socket_path,
                          const std::string& request_json,
                          uint32_t timeout_ms, std::string* response,
                          std::string* error);

}  // namespace agent
}  // namespace vmon
