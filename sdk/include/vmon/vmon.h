// vmon 探针 SDK 公共门面
//
// 设计约束（见 docs/production-safety.md）：
//   - 所有 API 不抛异常、不阻塞、不改变业务控制流；
//   - SDK 未初始化/初始化失败/被禁用时，所有调用退化为空操作；
//   - 热路径不做动态分配、不加锁、不写文件、不发网络。
#pragma once

#include <cstdint>
#include <string>

namespace vmon {

// 初始化配置。只在 init() 时读取，热路径不访问。
struct Config {
  std::string service_name;              // 必填；为空视为配置错误
  std::string instance_id;               // 可选；默认取主机名 + pid
  std::string service_version;           // 可选；用于关联 build_id
  std::string agent_endpoint;            // Agent UDS，默认 /tmp/vmon-agent.sock
  std::string control_dir;               // 控制套接字目录，默认 /tmp/vmon-ctrl
  uint32_t    sample_permyriad = 10000;  // 头部采样率（万分之一），默认全采
  uint32_t    flush_interval_ms = 100;   // 后台 flush 周期
  bool        enable_transport = true;   // 后台发送线程；单元测试可关闭
  bool        enable_control = true;     // 控制通道；单元测试可关闭
  // 崩溃兜底：默认关闭。开启后会接管 SIGSEGV/SIGABRT 等信号，写入一份
  // 最小崩溃 JSON（信号、pid、故障地址、二进制路径）后退出；生产环境建议
  // 优先用 core_pattern helper，避免与平台自带 core 收集冲突。
  bool        enable_crash_handler = false;
  std::string crash_report_dir = "/tmp/vmon-crash";
  bool        enabled = true;            // 配置级总开关

  // 常驻 CPU 采样（诊断版）。需要编译时打开 -DVMON_ENABLE_CPU_PROFILER=ON；
  // 默认关闭，避免常驻采样影响生产。采样结果以 type=profile 上报。
  bool     enable_cpu_profiler = false;
  uint32_t cpu_sample_hz = 99;
  uint32_t cpu_profile_flush_seconds = 10;
};

// 日志级别语义与原生日志一致；SDK 只提供分级与结构化字段。
enum class Level : uint8_t {
  kTrace = 0,
  kDebug = 1,
  kInfo  = 2,
  kWarn  = 3,
  kError = 4,
  kFatal = 5,
};

// 结构化日志。fmt 为 printf 风格，正文截断后入队，不分配内存。
void log(Level level, const char* file, int line, const char* fmt, ...) noexcept;

bool should_log(Level level) noexcept;
void set_min_level(Level level) noexcept;
Level min_level() noexcept;

// 初始化 SDK。失败返回 false 并保持 disabled，业务可继续运行。
bool init(const Config& config) noexcept;

// 关闭 SDK。幂等；不阻塞业务退出（后续会加限时 flush）。
void shutdown() noexcept;

// SDK 是否处于启用状态。热路径只读一个原子变量。
bool enabled() noexcept;

// 业务线程心跳。用于区分“进程活着但卡死”和“进程不存在”。
void heartbeat() noexcept;

// 设置当前线程的请求关联 ID（业务入口解析请求后调用）。
// SDK 只保存指针，后台发送线程稍后才序列化；指针必须保持有效直到事件被
// 发送（默认 100ms），建议使用静态/长生命周期字符串或显式管理生命周期。
// 传 nullptr 清除，避免野指针。
void set_request_id(const char* request_id) noexcept;
void set_task_id(const char* task_id) noexcept;
const char* request_id() noexcept;
const char* task_id() noexcept;

class Span;

// 跨线程/跨进程传递的最小上下文。
struct SpanContext {
  uint64_t trace_hi = 0;
  uint64_t trace_lo = 0;
  uint64_t span     = 0;
  bool     valid    = false;
};

// 从 W3C traceparent 头恢复父上下文（HTTP / gRPC 服务端入口使用）。
// 解析失败返回 false；调用方应新建 trace，而不是拒绝业务请求。
bool context_from_traceparent(const char* header, SpanContext* out) noexcept;

// 取当前线程 span 栈顶上下文；无 span 时 valid=false。
SpanContext current_context() noexcept;

// 以外部传入的父上下文创建 span（服务端接收请求、异步任务恢复）。
Span create_span_with_parent(const SpanContext& parent, const char* name,
                             const char* file, int line) noexcept;

// 进程内函数级 span。RAII；未启用时为零成本空对象。
//
//   void HandleRequest() {
//     VMON_SPAN("HandleRequest");
//     ...
//   }
class Span {
 public:
  Span() noexcept = default;
  ~Span();

  Span(const Span&) = delete;
  Span& operator=(const Span&) = delete;

  Span(Span&& other) noexcept;
  Span& operator=(Span&& other) noexcept;

  static Span create(const char* name, const char* file, int line) noexcept;

  // 内部使用：以显式父上下文创建 span（供 create_span_with_parent 调用）。
  static Span create_impl(const SpanContext* parent, const char* name,
                          const char* file, int line) noexcept;

  bool active() const noexcept { return active_; }

  // 标记当前 span 失败。error_code 必须是静态字符串（__FILE__ 同级生命周期），
  // 热路径只保存指针，不做拷贝。重复调用以最后一次为准。
  void set_error(const char* error_code) noexcept;

  // 提前结束；析构时重复调用是安全的。
  void end() noexcept;

 private:
  bool    active_ = false;
  bool    suppressed_ = false;  // 头部采样未命中时的抑制标记
  int32_t slot_   = -1;   // 线程本地 span 栈中的槽位
};

// SDK 自监控：当前线程丢弃的事件数（队列满）。
uint64_t dropped_events() noexcept;

}  // namespace vmon

#define VMON_CONCAT_INNER(a, b) a##b
#define VMON_CONCAT(a, b) VMON_CONCAT_INNER(a, b)

// 生成唯一变量名，允许同一作用域出现多个 VMON_SPAN。
#define VMON_SPAN(name)                                                     \
  auto VMON_CONCAT(vmon_span_, __LINE__) =                                  \
      ::vmon::Span::create((name), __FILE__, __LINE__)

// 结构化日志宏：SDK 未启用时完全退化为空操作。
#define VMON_LOG(level, ...)                                                \
  do {                                                                      \
    if (::vmon::should_log(level)) {                                        \
      ::vmon::log(level, __FILE__, __LINE__, __VA_ARGS__);                  \
    }                                                                       \
  } while (0)

#define VMON_TRACE(...) VMON_LOG(::vmon::Level::kTrace, __VA_ARGS__)
#define VMON_DEBUG(...) VMON_LOG(::vmon::Level::kDebug, __VA_ARGS__)
#define VMON_INFO(...)  VMON_LOG(::vmon::Level::kInfo, __VA_ARGS__)
#define VMON_WARN(...)  VMON_LOG(::vmon::Level::kWarn, __VA_ARGS__)
#define VMON_ERROR(...) VMON_LOG(::vmon::Level::kError, __VA_ARGS__)
#define VMON_FATAL(...) VMON_LOG(::vmon::Level::kFatal, __VA_ARGS__)
