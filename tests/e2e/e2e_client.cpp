// 端到端测试用业务进程：使用真实 SDK 产生 span / 日志 / 心跳，并支持
// 主动崩溃。run_e2e.py 通过 HTTP 查询中心服务，验证“业务进程 → UDS →
// Agent WAL → 中心服务”的完整链路。
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <cstdint>

#include <unistd.h>

#include <vmon/vmon.h>

namespace {

struct Options {
  std::string uds = "/tmp/vmon-agent.sock";
  std::string control_dir = "/tmp/vmon-ctrl";
  std::string crash_dir = "/tmp/vmon-crash";
  std::string service = "e2e-client";
  std::string request_id;
  std::string task_id;
  // 跨进程链路：子进程用父进程导出的 trace/span 构造自己的根 span。
  std::string trace_id;
  std::string parent_span_id;
  std::string root_name;
  std::string mode = "normal";
  int duration_ms = 15000;
  int hold_ms = 5000;
};

bool ParseArgs(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](std::string* out) {
      if (i + 1 >= argc) return false;
      *out = argv[++i];
      return true;
    };
    if (arg == "--uds") {
      if (!value(&options->uds)) return false;
    } else if (arg == "--control-dir") {
      if (!value(&options->control_dir)) return false;
    } else if (arg == "--crash-dir") {
      if (!value(&options->crash_dir)) return false;
    } else if (arg == "--service") {
      if (!value(&options->service)) return false;
    } else if (arg == "--request-id") {
      if (!value(&options->request_id)) return false;
    } else if (arg == "--task-id") {
      if (!value(&options->task_id)) return false;
    } else if (arg == "--trace-id") {
      if (!value(&options->trace_id)) return false;
    } else if (arg == "--parent-span-id") {
      if (!value(&options->parent_span_id)) return false;
    } else if (arg == "--root-name") {
      if (!value(&options->root_name)) return false;
    } else if (arg == "--mode") {
      if (!value(&options->mode)) return false;
    } else if (arg == "--duration-ms") {
      std::string text;
      if (!value(&text)) return false;
      options->duration_ms = std::atoi(text.c_str());
    } else if (arg == "--hold-ms") {
      std::string text;
      if (!value(&text)) return false;
      options->hold_ms = std::atoi(text.c_str());
    } else {
      std::fprintf(stderr, "未知参数: %s\n", arg.c_str());
      return false;
    }
  }
  const bool has_trace = !options->trace_id.empty();
  const bool has_parent = !options->parent_span_id.empty();
  return !options->request_id.empty() && options->duration_ms >= 0 &&
         has_trace == has_parent;
}

void SleepMs(int ms) {
  if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string Hex64(uint64_t value) {
  char buffer[17];
  std::snprintf(buffer, sizeof(buffer), "%016llx",
                static_cast<unsigned long long>(value));
  return std::string(buffer);
}

bool ParseHex64(const std::string& text, uint64_t* out) {
  if (text.empty() || text.size() > 16) return false;
  char* end = nullptr;
  const unsigned long long value =
      std::strtoull(text.c_str(), &end, 16);
  if (end == nullptr || *end != '\0') return false;
  *out = static_cast<uint64_t>(value);
  return true;
}

bool ParseContext(const Options& options, vmon::SpanContext* context) {
  if (options.trace_id.size() != 32 || options.parent_span_id.size() != 16) {
    std::fprintf(stderr, "trace_id 需要 32 位十六进制，span 需要 16 位\n");
    return false;
  }
  if (!ParseHex64(options.trace_id.substr(0, 16), &context->trace_hi) ||
      !ParseHex64(options.trace_id.substr(16, 16), &context->trace_lo) ||
      !ParseHex64(options.parent_span_id, &context->span)) {
    std::fprintf(stderr, "trace_id/span 不是合法十六进制\n");
    return false;
  }
  context->valid = true;
  return true;
}

// 根 span 由 main 持有：普通模式覆盖业务请求，parent 模式保持打开等待子进程。
vmon::Span CreateRoot(const Options& options) {
  // span 只保存名称指针，options 在 main 期间一直有效；不能指向局部临时串。
  const char* name =
      options.root_name.empty() ? "E2E/Handle" : options.root_name.c_str();
  if (!options.trace_id.empty()) {
    vmon::SpanContext parent;
    if (ParseContext(options, &parent)) {
      return vmon::create_span_with_parent(parent, name, __FILE__, __LINE__);
    }
  }
  return vmon::Span::create(name, __FILE__, __LINE__);
}

// 当前根 span 已入栈，这里只产生子 span 与日志。
void RunBusinessRequest(const std::string& request_id) {
  VMON_INFO("e2e request started request_id=%s", request_id.c_str());
  SleepMs(40);
  {
    VMON_SPAN("E2E/Child");
    VMON_DEBUG("e2e child debug detail");
    SleepMs(30);
  }
  {
    // 显式标记失败：服务端据此计算错误率、SLO 与首个错误 Span。
    vmon::Span cache = vmon::Span::create("E2E/Cache", __FILE__, __LINE__);
    cache.set_error("E_CACHE");
    VMON_WARN("e2e cache lookup miss");
    VMON_ERROR("e2e downstream failed code=E_CACHE");
    SleepMs(10);
  }
  VMON_INFO("e2e request handled request_id=%s", request_id.c_str());
}

void PrintReady(const std::string& service, const vmon::SpanContext& context) {
  std::printf("READY pid=%d service=%s trace_id=%s%s span_id=%s\n",
              static_cast<int>(::getpid()), service.c_str(),
              Hex64(context.trace_hi).c_str(), Hex64(context.trace_lo).c_str(),
              Hex64(context.span).c_str());
  std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!ParseArgs(argc, argv, &options)) {
    std::fprintf(stderr,
                 "用法: %s --request-id ID [--uds PATH] "
                 "[--mode normal|crash|parent] [--duration-ms N] "
                 "[--trace-id 32hex --parent-span-id 16hex]\n",
                 argv[0]);
    return 2;
  }

  const bool crash_mode = options.mode == "crash";
  vmon::Config config;
  config.service_name = crash_mode ? "e2e-crash" : options.service;
  config.instance_id = config.service_name + "-" + std::to_string(::getpid());
  config.service_version = "e2e-1";
  config.agent_endpoint = options.uds;
  config.control_dir = options.control_dir;
  config.flush_interval_ms = 50;
  config.enable_transport = true;
  config.enable_control = !crash_mode;
  config.enable_crash_handler = crash_mode;
  config.crash_report_dir = options.crash_dir;

  if (!vmon::init(config)) {
    std::fprintf(stderr, "SDK 初始化失败\n");
    return 3;
  }

  vmon::set_request_id(options.request_id.c_str());
  if (!options.task_id.empty()) vmon::set_task_id(options.task_id.c_str());

  const bool hold_mode = options.mode == "parent";
  {
    vmon::Span root = CreateRoot(options);
    PrintReady(config.service_name, vmon::current_context());

    if (crash_mode) {
      VMON_ERROR("e2e crash trigger request_id=%s", options.request_id.c_str());
      // 给发送线程时间把崩溃前的日志送出去，再触发信号处理器。
      SleepMs(600);
      std::raise(SIGSEGV);
      return 4;
    }

    if (hold_mode) {
      VMON_INFO("e2e parent holding for child request_id=%s",
                options.request_id.c_str());
      SleepMs(options.hold_ms);
      VMON_INFO("e2e parent closing request_id=%s", options.request_id.c_str());
    } else {
      RunBusinessRequest(options.request_id);
    }
    root.end();
  }

  if (hold_mode) {
    std::printf("DONE pid=%d\n", static_cast<int>(::getpid()));
    std::fflush(stdout);
    vmon::shutdown();
    return 0;
  }

  // 持续产生 debug 日志与心跳：Python 通过动态日志级别命令打开 debug 后，
  // 应能在中心看到 "e2e debug tick"；命令之前这些日志会被 SDK 过滤。
  const int interval_ms = 100;
  int ticks = 0;
  for (int elapsed = 0; elapsed < options.duration_ms; elapsed += interval_ms) {
    VMON_DEBUG("e2e debug tick %d request_id=%s", ticks++, options.request_id.c_str());
    vmon::heartbeat();
    if (ticks % 10 == 0) {
      VMON_INFO("e2e still working tick %d request_id=%s", ticks,
                options.request_id.c_str());
    }
    SleepMs(interval_ms);
  }

  VMON_INFO("e2e finished request_id=%s", options.request_id.c_str());
  std::printf("DONE pid=%d\n", static_cast<int>(::getpid()));
  std::fflush(stdout);
  vmon::shutdown();
  return 0;
}
