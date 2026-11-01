#include "crash/crash_handler.h"

#include <cerrno>
#include <ctime>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace vmon {
namespace detail {

char CrashHandler::prefix_[1200] = {0};
char CrashHandler::mid_[32] = {0};
char CrashHandler::suffix_[1200] = {0};
char CrashHandler::path_[512] = {0};
size_t CrashHandler::prefix_len_ = 0;
size_t CrashHandler::mid_len_ = 0;
size_t CrashHandler::suffix_len_ = 0;
int CrashHandler::fd_ = -1;
bool CrashHandler::installed_ = false;

namespace {

// 只保留 JSON 安全字符，避免配置里的引号破坏崩溃文件结构。
void append_escaped(char* out, size_t capacity, size_t* len,
                    const std::string& value) {
  for (char c : value) {
    if (*len + 2 >= capacity) break;
    if (c == '"' || c == '\\') {
      out[(*len)++] = '_';
    } else if (static_cast<unsigned char>(c) < 0x20) {
      out[(*len)++] = ' ';
    } else {
      out[(*len)++] = c;
    }
  }
  out[*len] = '\0';
}

void append_literal(char* out, size_t capacity, size_t* len,
                    const char* text) {
  if (text == nullptr) return;
  for (const char* p = text; *p != '\0' && *len + 1 < capacity; ++p) {
    out[(*len)++] = *p;
  }
  out[*len] = '\0';
}

// async-signal-safe：只做整数转字符，不调用 snprintf。
size_t append_uint(char* out, size_t capacity, size_t len, uint64_t value,
                   bool hex) {
  char digits[32];
  size_t count = 0;
  const unsigned base = hex ? 16 : 10;
  if (value == 0) {
    digits[count++] = '0';
  }
  while (value > 0 && count < sizeof(digits)) {
    const unsigned d = static_cast<unsigned>(value % base);
    digits[count++] = static_cast<char>(d < 10 ? '0' + d : 'a' + (d - 10));
    value /= base;
  }
  while (count > 0 && len + 1 < capacity) {
    out[len++] = digits[--count];
  }
  out[len] = '\0';
  return len;
}

}  // namespace

const char* CrashHandler::signal_name(int signal) noexcept {
  switch (signal) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGTRAP: return "SIGTRAP";
    default: return "SIGNAL";
  }
}

void CrashHandler::install(const Config& config) noexcept {
  if (installed_) return;
  const std::string dir = config.crash_report_dir.empty()
                              ? std::string("/tmp/vmon-crash")
                              : config.crash_report_dir;
  ::mkdir(dir.c_str(), 0700);

  size_t path_len = 0;
  append_escaped(path_, sizeof(path_), &path_len, dir);
  append_literal(path_, sizeof(path_), &path_len, "/crash-");
  path_len = append_uint(path_, sizeof(path_), path_len,
                         static_cast<uint64_t>(::getpid()), false);
  append_literal(path_, sizeof(path_), &path_len, ".json");

  // 预打开文件；普通退出时删除，崩溃时由信号处理器写入。
  // 0640：报告目录通常由业务用户与 Agent 共享用户组，Agent 需要可读。
  fd_ = ::open(path_, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0640);
  if (fd_ < 0) return;

  char binary[512];
  ssize_t n = ::readlink("/proc/self/exe", binary, sizeof(binary) - 1);
  std::string binary_path;
  if (n > 0) {
    binary[n] = '\0';
    binary_path = binary;
  }

  prefix_len_ = 0;
  mid_len_ = 0;
  suffix_len_ = 0;
  append_literal(prefix_, sizeof(prefix_), &prefix_len_,
                 "{\"type\":\"crash\",\"time_ns\":");
  append_literal(mid_, sizeof(mid_), &mid_len_, ",\"pid\":");
  append_literal(suffix_, sizeof(suffix_), &suffix_len_,
                 ",\"service\":\"");
  append_escaped(suffix_, sizeof(suffix_), &suffix_len_, config.service_name);
  append_literal(suffix_, sizeof(suffix_), &suffix_len_, "\",\"instance\":\"");
  append_escaped(suffix_, sizeof(suffix_), &suffix_len_, config.instance_id);
  append_literal(suffix_, sizeof(suffix_), &suffix_len_, "\",\"version\":\"");
  append_escaped(suffix_, sizeof(suffix_), &suffix_len_, config.service_version);
  append_literal(suffix_, sizeof(suffix_), &suffix_len_, "\",\"binary\":\"");
  append_escaped(suffix_, sizeof(suffix_), &suffix_len_, binary_path);
  append_literal(suffix_, sizeof(suffix_), &suffix_len_,
                 "\",\"reason\":\"signal-handler\",\"signal\":\"");

  struct sigaction action;
  std::memset(&action, 0, sizeof(action));
  action.sa_sigaction = &CrashHandler::handle;
  action.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&action.sa_mask);
  ::sigaction(SIGSEGV, &action, nullptr);
  ::sigaction(SIGABRT, &action, nullptr);
  ::sigaction(SIGBUS, &action, nullptr);
  ::sigaction(SIGFPE, &action, nullptr);
  ::sigaction(SIGILL, &action, nullptr);
  installed_ = true;
}

void CrashHandler::uninstall() noexcept {
  if (!installed_) return;
  installed_ = false;
  ::signal(SIGSEGV, SIG_DFL);
  ::signal(SIGABRT, SIG_DFL);
  ::signal(SIGBUS, SIG_DFL);
  ::signal(SIGFPE, SIG_DFL);
  ::signal(SIGILL, SIG_DFL);
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  // 正常退出：删除空的崩溃占位文件。
  if (path_[0] != '\0') {
    ::unlink(path_);
    path_[0] = '\0';
  }
}

void CrashHandler::handle(int signal, siginfo_t* info, void*) noexcept {
  if (fd_ < 0) {
    ::_exit(128 + signal);
  }
  char buffer[2600];
  size_t len = 0;
  append_literal(buffer, sizeof(buffer), &len, prefix_);
  timespec ts;
  ::clock_gettime(CLOCK_REALTIME, &ts);
  const uint64_t now = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                       static_cast<uint64_t>(ts.tv_nsec);
  len = append_uint(buffer, sizeof(buffer), len, now, false);
  append_literal(buffer, sizeof(buffer), &len, mid_);
  len = append_uint(buffer, sizeof(buffer), len,
                    static_cast<uint64_t>(::getpid()), false);
  append_literal(buffer, sizeof(buffer), &len, suffix_);
  append_literal(buffer, sizeof(buffer), &len, signal_name(signal));
  append_literal(buffer, sizeof(buffer), &len, "\",\"fault_address\":\"0x");
  if (info != nullptr && info->si_addr != nullptr) {
    len = append_uint(buffer, sizeof(buffer), len,
                      reinterpret_cast<uintptr_t>(info->si_addr), true);
  } else {
    append_literal(buffer, sizeof(buffer), &len, "0");
  }
  append_literal(buffer, sizeof(buffer), &len, "\"}\n");

  size_t offset = 0;
  while (offset < len) {
    const ssize_t n = ::write(fd_, buffer + offset, len - offset);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) continue;
      break;
    }
    offset += static_cast<size_t>(n);
  }
  ::_exit(128 + signal);
}

}  // namespace detail
}  // namespace vmon
