#include "command_runner.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <httplib.h>

#include "control_client.h"

namespace vmon {
namespace agent {
namespace {

const size_t kMaxOutputBytes = 1024 * 1024;
const uint32_t kExecTimeoutMs = 5000;

std::string shell_escape_url(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0xF]);
    }
  }
  return out;
}

bool read_capped_file(const std::string& path, std::string* output,
                      std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "无法读取 " + path;
    return false;
  }
  std::ostringstream ss;
  char buffer[8192];
  size_t total = 0;
  while (in && total < kMaxOutputBytes) {
    in.read(buffer, sizeof(buffer));
    const std::streamsize n = in.gcount();
    if (n <= 0) break;
    ss.write(buffer, n);
    total += static_cast<size_t>(n);
  }
  *output = ss.str();
  return true;
}

// 通过 fork/exec 执行固定 argv；不使用 shell，输出合并 stdout/stderr。
bool exec_capped(const std::vector<std::string>& argv, uint32_t timeout_ms,
                 std::string* output, std::string* error) {
  if (argv.empty()) {
    if (error) *error = "空命令";
    return false;
  }
  int pipe_fd[2];
  if (::pipe(pipe_fd) != 0) {
    if (error) *error = "pipe 创建失败";
    return false;
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(pipe_fd[0]);
    ::close(pipe_fd[1]);
    if (error) *error = "fork 失败";
    return false;
  }
  if (pid == 0) {
    ::dup2(pipe_fd[1], STDOUT_FILENO);
    ::dup2(pipe_fd[1], STDERR_FILENO);
    ::close(pipe_fd[0]);
    ::close(pipe_fd[1]);
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const std::string& item : argv) {
      args.push_back(const_cast<char*>(item.c_str()));
    }
    args.push_back(nullptr);
    ::execvp(args[0], args.data());
    _exit(127);
  }

  ::close(pipe_fd[1]);
  std::string buffer;
  const uint64_t deadline =
      now_mono_ns() + static_cast<uint64_t>(timeout_ms) * 1000000ull;
  bool timed_out = false;
  while (buffer.size() < kMaxOutputBytes) {
    const uint64_t now = now_mono_ns();
    if (now >= deadline) {
      timed_out = true;
      break;
    }
    pollfd pfd;
    pfd.fd = pipe_fd[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int ready =
        ::poll(&pfd, 1, static_cast<int>((deadline - now) / 1000000ull + 1));
    if (ready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (ready == 0) continue;
    char chunk[4096];
    const ssize_t n = ::read(pipe_fd[0], chunk, sizeof(chunk));
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (n == 0) break;
    buffer.append(chunk, static_cast<size_t>(n));
  }
  ::close(pipe_fd[0]);

  if (timed_out) ::kill(pid, SIGKILL);
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (buffer.size() > kMaxOutputBytes) buffer.resize(kMaxOutputBytes);
  *output = buffer;
  if (timed_out) {
    if (error) *error = "命令执行超时";
    return false;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    if (error) {
      *error = WIFEXITED(status)
                   ? "命令退出码 " + std::to_string(WEXITSTATUS(status))
                   : "命令异常终止";
    }
    return false;
  }
  return true;
}

bool valid_pid(int32_t pid) { return pid > 0; }

bool proc_snapshot(int32_t pid, const std::string& name, std::string* output,
                   std::string* error) {
  static const char* kAllowed[] = {"status", "maps", "limits", "io",
                                   "smaps_rollup", "stat", "wchan"};
  bool allowed = false;
  for (const char* item : kAllowed) {
    if (name == item) {
      allowed = true;
      break;
    }
  }
  if (!allowed) {
    if (error) *error = "不在白名单的 /proc 文件: " + name;
    return false;
  }
  if (!valid_pid(pid)) {
    if (error) *error = "pid 非法";
    return false;
  }
  return read_capped_file("/proc/" + std::to_string(pid) + "/" + name, output,
                          error);
}

bool list_fds(int32_t pid, std::string* output, std::string* error) {
  if (!valid_pid(pid)) {
    if (error) *error = "pid 非法";
    return false;
  }
  const std::string dir_path = "/proc/" + std::to_string(pid) + "/fd";
  DIR* dir = ::opendir(dir_path.c_str());
  if (dir == nullptr) {
    if (error) *error = "无法打开 " + dir_path;
    return false;
  }
  std::ostringstream ss;
  size_t count = 0;
  while (dirent* entry = ::readdir(dir)) {
    if (entry->d_name[0] == '.') continue;
    const std::string link_path = dir_path + "/" + entry->d_name;
    char target[1024];
    const ssize_t n = ::readlink(link_path.c_str(), target, sizeof(target) - 1);
    ss << entry->d_name << " -> ";
    if (n > 0) {
      target[n] = '\0';
      ss << target;
    } else {
      ss << "?";
    }
    ss << "\n";
    if (++count >= 2000) break;
  }
  ::closedir(dir);
  *output = ss.str();
  return true;
}

std::string target_pid_text(const nlohmann::json& args) {
  return std::to_string(args.value("pid", 0));
}

}  // namespace

CommandRunner::CommandRunner(const AgentConfig& config, std::string node,
                             std::string agent)
    : config_(config),
      node_(std::move(node)),
      agent_(std::move(agent)) {
  split_url(config_.server_url, &host_, &port_, &prefix_);
}

CommandRunner::~CommandRunner() { stop(); }

void CommandRunner::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread(&CommandRunner::run, this);
}

void CommandRunner::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void CommandRunner::run() {
  const uint32_t interval =
      config_.command_interval_ms == 0 ? 1000 : config_.command_interval_ms;
  while (running_.load()) {
    poll_once();
    const uint64_t deadline = now_mono_ns() +
                              static_cast<uint64_t>(interval) * 1000000ull;
    while (running_.load() && now_mono_ns() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
}

void CommandRunner::poll_once() {
  httplib::Client client(host_, port_);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(3, 0);
  client.set_write_timeout(3, 0);

  httplib::Headers headers;
  if (!config_.token.empty()) headers.emplace("X-Vmon-Token", config_.token);
  const std::string path = prefix_ + "/v1/commands?node=" +
                           shell_escape_url(node_) + "&agent=" +
                           shell_escape_url(agent_) + "&limit=20";
  auto res = client.Get(path, headers);
  if (!res || res->status != 200) return;

  nlohmann::json body;
  try {
    body = nlohmann::json::parse(res->body);
  } catch (const std::exception&) {
    return;
  }
  if (!body.contains("commands") || !body["commands"].is_array()) return;

  for (const auto& command : body["commands"]) {
    const std::string id = command.value("id", std::string());
    if (id.empty()) continue;
    std::string result;
    std::string error;
    const bool ok = execute(command, &result, &error);
    post_result(id, ok ? "done" : "failed", result, error);
  }
}

bool CommandRunner::execute(const nlohmann::json& command, std::string* result,
                            std::string* error) {
  if (result != nullptr) result->clear();
  if (error != nullptr) error->clear();

  const std::string type = command.value("type", std::string());
  const nlohmann::json args =
      command.contains("args") && command["args"].is_object()
          ? command["args"]
          : nlohmann::json::object();
  const int32_t pid = command.value("pid", 0);

  auto control = [&](const nlohmann::json& request) -> bool {
    if (!valid_pid(pid)) {
      if (error) *error = "命令缺少有效 pid";
      return false;
    }
    const std::string socket_path =
        control_socket_path(config_.control_dir, pid);
    std::string response;
    if (!send_control_command(socket_path, request.dump(), 3000, &response,
                              error)) {
      return false;
    }
    if (response.empty()) {
      if (error) *error = "控制通道返回空响应";
      return false;
    }
    if (result != nullptr) *result = response;
    return true;
  };

  if (type == "ping") {
    return control({{"cmd", "ping"}});
  }
  if (type == "get_stats") {
    return control({{"cmd", "get_stats"}});
  }
  if (type == "dump_stacks") {
    return control({{"cmd", "dump_stacks"}});
  }
  if (type == "set_log_level") {
    const std::string level = args.value("level", std::string("info"));
    const uint64_t ttl_ms = args.value("ttl_ms", static_cast<uint64_t>(0));
    return control({{"cmd", "set_level"}, {"level", level}, {"ttl_ms", ttl_ms}});
  }
  if (type == "diagnose") {
    const std::string op = args.value("op", std::string());
    std::string output;
    bool ok = false;
    if (op == "fds") {
      ok = list_fds(pid, &output, error);
    } else if (op == "ss") {
      ok = exec_capped({"ss", "-tanp"}, kExecTimeoutMs, &output, error);
    } else if (op == "lsof") {
      if (!valid_pid(pid)) {
        if (error) *error = "pid 非法";
        return false;
      }
      ok = exec_capped({"lsof", "-p", target_pid_text(args)},
                       kExecTimeoutMs, &output, error);
    } else if (op == "gdb_backtrace") {
      if (!valid_pid(pid)) {
        if (error) *error = "pid 非法";
        return false;
      }
      ok = exec_capped({"gdb", "-p", target_pid_text(args), "-batch", "-ex",
                        "thread apply all bt"},
                       kExecTimeoutMs, &output, error);
    } else if (op == "dmesg") {
      ok = exec_capped({"dmesg", "--ctime"}, kExecTimeoutMs, &output, error);
    } else if (!op.empty()) {
      ok = proc_snapshot(pid, op, &output, error);
    } else {
      if (error) *error = "缺少 diagnose op";
      return false;
    }
    if (result != nullptr) {
      nlohmann::json j;
      j["op"] = op;
      j["pid"] = pid;
      j["output"] = output;
      *result = j.dump();
    }
    return ok;
  }

  if (error) *error = "不支持的命令类型: " + type;
  return false;
}

bool CommandRunner::post_result(const std::string& id, const std::string& status,
                                const std::string& result,
                                const std::string& error) {
  httplib::Client client(host_, port_);
  client.set_connection_timeout(2, 0);
  client.set_read_timeout(3, 0);
  client.set_write_timeout(3, 0);

  httplib::Headers headers;
  if (!config_.token.empty()) headers.emplace("X-Vmon-Token", config_.token);

  nlohmann::json body;
  body["status"] = status;
  body["result"] = result;
  body["error"] = error;
  const std::string path =
      prefix_ + "/v1/commands/" + shell_escape_url(id) + "/result";
  auto res = client.Post(path, headers, body.dump(), "application/json");
  return res && res->status >= 200 && res->status < 300;
}

}  // namespace agent
}  // namespace vmon
