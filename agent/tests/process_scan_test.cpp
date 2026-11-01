#include <gtest/gtest.h>

#include <unistd.h>

#include <string>
#include <vector>

#include "process_scan.h"

namespace {

using vmon::agent::collect_host_info;
using vmon::agent::ProcessInfo;
using vmon::agent::process_matches;
using vmon::agent::scan_processes;

TEST(ProcessScan, FindsCurrentProcess) {
  const std::vector<ProcessInfo> processes = scan_processes();
  ASSERT_FALSE(processes.empty());

  bool found = false;
  const int32_t self = static_cast<int32_t>(::getpid());
  for (const ProcessInfo& info : processes) {
    if (info.pid == self) {
      found = true;
      EXPECT_FALSE(info.name.empty());
      EXPECT_FALSE(info.cmdline.empty());
      break;
    }
  }
  EXPECT_TRUE(found) << "当前测试进程应出现在进程列表中";
}

TEST(ProcessMatch, EmptyFiltersMatchEverything) {
  ProcessInfo info;
  info.name = "order";
  info.cmdline = "/opt/order --port=9000";
  EXPECT_TRUE(process_matches(info, std::vector<std::string>()));
}

TEST(ProcessMatch, MatchesNameOrCommandLine) {
  ProcessInfo info;
  info.name = "order";
  info.cmdline = "/opt/order --port=9000";

  EXPECT_TRUE(process_matches(info, {"order"}));
  EXPECT_TRUE(process_matches(info, {"--port=9000"}));
  EXPECT_FALSE(process_matches(info, {"payment"}));
}

TEST(ProcessMatch, SupportsCommaSeparatedAlternatives) {
  ProcessInfo info;
  info.name = "order";
  info.cmdline = "/opt/order";
  EXPECT_TRUE(process_matches(info, {"pay,gateway,order"}));
  EXPECT_FALSE(process_matches(info, {"pay,gateway"}));
}

TEST(ProcessMatch, AnyFilterHits) {
  ProcessInfo info;
  info.name = "order";
  info.cmdline = "/opt/order";
  EXPECT_TRUE(process_matches(info, {"pay", "order"}));
  EXPECT_FALSE(process_matches(info, {"pay", "gateway"}));
}

TEST(HostInfo, CollectsBasicHostFields) {
  const auto host = collect_host_info("node-a");
  EXPECT_EQ("node-a", host.node);
  EXPECT_FALSE(host.os.empty());
  EXPECT_FALSE(host.kernel.empty());
  EXPECT_FALSE(host.arch.empty());
  EXPECT_GT(host.mem_total_kb, 0u);
  EXPECT_GE(host.load1, 0.0);
}

}  // namespace
