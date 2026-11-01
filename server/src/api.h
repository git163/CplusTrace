// HTTP API 与 Web UI 静态资源注册。
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include <httplib.h>

#include "commands.h"
#include "config.h"
#include "crash.h"
#include "forwarder.h"
#include "notifier.h"
#include "pipeline.h"
#include "profiler.h"
#include "rules.h"
#include "slo.h"
#include "store.h"

namespace vmon {
namespace server {

struct ServerContext {
  explicit ServerContext(const ServerConfig& server_config);

  ServerConfig config;
  Store store;
  CrashStore crashes;
  ProfileStore profiles;
  CommandStore commands;
  RuleEngine rules;
  SloManager slos;
  Forwarder forwarder;
  Pipeline pipeline;
  Notifier notifier;
  uint64_t started_ns = 0;
  std::string rules_path;
  std::string slos_path;

  void load_rules();
  void save_rules();
  void load_slos();
  bool save_slos(std::string* error);

  void start_background();
  void stop_background();

 private:
  void background_loop();

  std::thread background_;
  std::atomic<bool> running_{false};
};

void register_routes(httplib::Server& server, ServerContext& context);

}  // namespace server
}  // namespace vmon
