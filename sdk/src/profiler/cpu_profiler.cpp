#include "profiler/cpu_profiler.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "core/clock.h"

namespace vmon {
namespace detail {
namespace {

const size_t kSlotCount = 4096;
const int kMaxFrames = 32;

struct SampleSlot {
  std::atomic<uint64_t> seq{0};
  uint64_t wall_ns = 0;
  uint32_t count = 0;
  void* frames[kMaxFrames] = {nullptr};
};

SampleSlot g_slots[kSlotCount];
std::atomic<uint64_t> g_head{0};
std::atomic<bool> g_active{false};
struct sigaction g_old_action;
bool g_installed = false;

void on_sigprof(int, siginfo_t*, void*) {
  if (!g_active.load(std::memory_order_relaxed)) return;
  const uint64_t index = g_head.fetch_add(1, std::memory_order_relaxed);
  SampleSlot& slot = g_slots[index % kSlotCount];
  const uint64_t seq = slot.seq.load(std::memory_order_relaxed);
  slot.seq.store(seq + 1, std::memory_order_relaxed);
  slot.wall_ns = wall_ns();
  slot.count = static_cast<uint32_t>(
      ::backtrace(slot.frames, kMaxFrames));
  slot.seq.store(seq + 2, std::memory_order_release);
}

std::string resolve(void* address) {
  Dl_info info;
  if (::dladdr(address, &info) != 0 && info.dli_sname != nullptr) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s", info.dli_sname);
    return buffer;
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "0x%llx",
                static_cast<unsigned long long>(
                    reinterpret_cast<uintptr_t>(address)));
  return buffer;
}

}  // namespace

CpuProfiler& CpuProfiler::instance() noexcept {
  static CpuProfiler profiler;
  return profiler;
}

CpuProfiler::~CpuProfiler() { stop(); }

bool CpuProfiler::running() const noexcept { return running_.load(); }

void CpuProfiler::start(const Config& config, uint32_t pid) noexcept {
  if (running_.exchange(true)) return;
  config_ = config;
  pid_ = pid;
  g_head.store(0, std::memory_order_relaxed);

  // 预热 backtrace，避免第一次在信号上下文里触发动态加载/分配。
  void* warm[4];
  (void)::backtrace(warm, 4);

  struct sigaction action;
  std::memset(&action, 0, sizeof(action));
  action.sa_sigaction = on_sigprof;
  action.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&action.sa_mask);
  g_installed = ::sigaction(SIGPROF, &action, &g_old_action) == 0;
  if (!g_installed) {
    running_.store(false);
    return;
  }
  g_active.store(true, std::memory_order_release);

  const uint32_t hz = config_.cpu_sample_hz == 0 ? 99 : config_.cpu_sample_hz;
  const uint32_t usec = 1000000u / hz;
  itimerval timer;
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = static_cast<suseconds_t>(usec);
  timer.it_value = timer.it_interval;
  ::setitimer(ITIMER_PROF, &timer, nullptr);

  thread_ = std::thread(&CpuProfiler::run, this);
}

void CpuProfiler::stop() noexcept {
  if (!running_.exchange(false)) return;
  g_active.store(false, std::memory_order_release);
  itimerval timer;
  std::memset(&timer, 0, sizeof(timer));
  ::setitimer(ITIMER_PROF, &timer, nullptr);
  if (thread_.joinable()) thread_.join();
  if (g_installed) {
    ::sigaction(SIGPROF, &g_old_action, nullptr);
    g_installed = false;
  }
}

void CpuProfiler::run() noexcept {
  const uint32_t seconds = config_.cpu_profile_flush_seconds == 0
                               ? 10
                               : config_.cpu_profile_flush_seconds;
  uint64_t last_index = g_head.load(std::memory_order_relaxed);
  while (running_.load()) {
    for (int i = 0; i < static_cast<int>(seconds * 10) && running_.load();
         ++i) {
      struct timespec sleep_time;
      sleep_time.tv_sec = 0;
      sleep_time.tv_nsec = 100000000;
      ::nanosleep(&sleep_time, nullptr);
    }
    const uint64_t head = g_head.load(std::memory_order_acquire);
    if (head == last_index) continue;
    if (head - last_index > kSlotCount) last_index = head - kSlotCount;

    std::unordered_map<std::string, uint64_t> counts;
    for (uint64_t index = last_index; index < head; ++index) {
      SampleSlot& slot = g_slots[index % kSlotCount];
      const uint64_t seq1 = slot.seq.load(std::memory_order_acquire);
      if ((seq1 & 1u) != 0) continue;  // 写入中
      const uint32_t count = slot.count;
      void* frames[kMaxFrames];
      if (count > kMaxFrames) continue;
      std::memcpy(frames, slot.frames, sizeof(void*) * count);
      const uint64_t seq2 = slot.seq.load(std::memory_order_acquire);
      if (seq1 != seq2) continue;

      std::string folded;
      for (int frame = static_cast<int>(count) - 1; frame >= 0; --frame) {
        if (!folded.empty()) folded.push_back(';');
        folded += resolve(frames[frame]);
      }
      if (folded.empty()) folded = "unknown";
      ++counts[folded];
    }
    last_index = head;

    if (counts.empty()) continue;
    std::string text;
    uint64_t total = 0;
    for (const auto& kv : counts) {
      text += kv.first + " " + std::to_string(kv.second) + "\n";
      total += kv.second;
    }

    nlohmann::json event;
    event["type"] = "profile";
    event["time_ns"] = wall_ns();
    event["service"] = config_.service_name;
    event["instance"] = config_.instance_id;
    event["version"] = config_.service_version;
    event["pid"] = pid_;
    event["kind"] = "cpu";
    event["attrs"] = {{"folded", text}, {"samples", std::to_string(total)},
                      {"source", "sigprof"}};
    send(event.dump());
  }
}

bool CpuProfiler::send(const std::string& body) noexcept {
  if (config_.agent_endpoint.empty()) return false;
  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return false;
  sockaddr_un address;
  std::memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  if (config_.agent_endpoint.size() >= sizeof(address.sun_path)) {
    ::close(fd);
    return false;
  }
  std::strncpy(address.sun_path, config_.agent_endpoint.c_str(),
               sizeof(address.sun_path) - 1);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
      0) {
    ::close(fd);
    return false;
  }
  const std::string line = body + "\n";
  size_t written = 0;
  while (written < line.size()) {
    const ssize_t n = ::write(fd, line.data() + written, line.size() - written);
    if (n <= 0) break;
    written += static_cast<size_t>(n);
  }
  ::close(fd);
  return written == line.size();
}

}  // namespace detail
}  // namespace vmon
