cmake_minimum_required(VERSION 3.20)
project(CplusTrace LANGUAGES CXX)

# C++14：用户要求尽量使用 C++14。不要使用 C++17 特性（结构化绑定、
# std::optional、std::string_view、std::filesystem、if constexpr、std::byte 等）。
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "构建类型" FORCE)
endif()

option(VMON_BUILD_TESTS "构建单元测试" ON)
option(VMON_BUILD_EXAMPLES "构建示例程序" ON)

set(VMON_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/src")
set(VMON_JSON_INCLUDE
    "${VMON_THIRD_PARTY}/nlohmann-json-3.12.0/json-3.12.0/single_include")
set(VMON_HTTPLIB_INCLUDE
    "${VMON_THIRD_PARTY}/cpp-httplib-0.64.0/cpp-httplib-0.64.0")
set(VMON_CLI11_INCLUDE
    "${VMON_THIRD_PARTY}/CLI11-2.7.2/CLI11-2.7.2/include")

find_package(Threads REQUIRED)

# 三方依赖以 interface 形式复用，避免把源码编进每个 target。
add_library(vmon_third_party INTERFACE)
target_include_directories(vmon_third_party INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/common
  ${VMON_JSON_INCLUDE}
  ${VMON_HTTPLIB_INCLUDE}
  ${VMON_CLI11_INCLUDE}
)
target_link_libraries(vmon_third_party INTERFACE Threads::Threads)
target_compile_definitions(vmon_third_party INTERFACE
  CPPHTTPLIB_THREAD_POOL_COUNT=4
)

# ---------------------------------------------------------------- SDK
add_library(vmon STATIC
  sdk/src/vmon.cpp
  sdk/src/trace/span.cpp
  sdk/src/log/log.cpp
  sdk/src/core/registry.cpp
  sdk/src/transport/sender.cpp
  sdk/src/control/control_server.cpp
  sdk/src/crash/crash_handler.cpp
  adapters/grpc/propagation.cpp
)

target_include_directories(vmon
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}/sdk/src
          ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(vmon PUBLIC Threads::Threads PRIVATE vmon_third_party)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vmon PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 业务侧一行接入：vmon_attach(<target>)。
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/vmon.cmake)

# ---------------------------------------------------------------- Agent
# Agent 逻辑拆成静态库，便于单元测试直接复用（main 只做参数解析与编排）。
add_library(vmon_agent_core STATIC
  agent/src/agent.cpp
  agent/src/ingest.cpp
  agent/src/wal.cpp
  agent/src/uploader.cpp
  agent/src/process_scan.cpp
  agent/src/log_tail.cpp
  agent/src/metrics.cpp
  agent/src/control_client.cpp
  agent/src/command_runner.cpp
  agent/src/crash_collect.cpp
)
target_include_directories(vmon_agent_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/common
  ${CMAKE_CURRENT_SOURCE_DIR}/agent/src
)
target_link_libraries(vmon_agent_core PUBLIC vmon_third_party)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vmon_agent_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(vmon_agent agent/src/main.cpp)
target_link_libraries(vmon_agent PRIVATE vmon_agent_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vmon_agent PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------- 中心服务
# Server 业务逻辑拆成静态库，API/存储/规则等可被单元测试与端到端测试复用。
add_library(vmon_server_core STATIC
  server/src/api.cpp
  server/src/catalog.cpp
  server/src/commands.cpp
  server/src/config.cpp
  server/src/crash.cpp
  server/src/diagnostics.cpp
  server/src/evidence.cpp
  server/src/forwarder.cpp
  server/src/log_cluster.cpp
  server/src/profiler.cpp
  server/src/rules.cpp
  server/src/serialize.cpp
  server/src/slo.cpp
  server/src/store.cpp
  server/src/trace_builder.cpp
  common/sha256.cpp
  symbolizer/symbolizer.cpp
)
target_include_directories(vmon_server_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/common
  ${CMAKE_CURRENT_SOURCE_DIR}/server/src
  ${CMAKE_CURRENT_SOURCE_DIR}/symbolizer
)
target_link_libraries(vmon_server_core PUBLIC vmon_third_party)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vmon_server_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(vmon_server server/src/main.cpp)
target_link_libraries(vmon_server PRIVATE vmon_server_core)
target_compile_definitions(vmon_server PRIVATE
  VMON_WEB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/web"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vmon_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------- 工具
add_subdirectory(tools)

# ---------------------------------------------------------------- 示例
if(VMON_BUILD_EXAMPLES)
  add_subdirectory(examples/hello)
  add_subdirectory(examples/http_backend)
endif()

# ---------------------------------------------------------------- 测试
if(VMON_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
