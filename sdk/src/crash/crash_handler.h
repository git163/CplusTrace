// 崩溃兜底：async-signal-safe 地写入最小崩溃上下文。
//
// 不在信号上下文里 malloc、抓栈、格式化或符号化；只把 init 阶段预先生成的
// 固定前缀 + 信号号 + 故障地址 write() 到预打开文件，然后 _exit。
#pragma once

#include <cstddef>
#include <signal.h>

#include <vmon/vmon.h>

namespace vmon {
namespace detail {

class CrashHandler {
 public:
  void install(const Config& config) noexcept;
  void uninstall() noexcept;

 private:
  static void handle(int signal, siginfo_t* info, void* context) noexcept;

  static char prefix_[1200];
  static char mid_[32];
  static char suffix_[1200];
  static char path_[512];
  static size_t prefix_len_;
  static size_t mid_len_;
  static size_t suffix_len_;
  static int fd_;
  static bool installed_;
  static const char* signal_name(int signal) noexcept;
};

}  // namespace detail
}  // namespace vmon
