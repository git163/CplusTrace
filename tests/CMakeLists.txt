# ---------------------------------------------------------------- 端到端测试
# 使用真实 SDK + Agent + Server，由 Python 脚本编排并断言完整链路。
add_executable(vmon_e2e_client e2e/e2e_client.cpp)
target_link_libraries(vmon_e2e_client PRIVATE vmon)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND TARGET vmon_http_backend)
  add_test(NAME vmon_e2e
    COMMAND Python3::Interpreter
      ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_e2e.py
      --server-bin $<TARGET_FILE:vmon_server>
      --agent-bin $<TARGET_FILE:vmon_agent>
      --client-bin $<TARGET_FILE:vmon_e2e_client>
      --backend-bin $<TARGET_FILE:vmon_http_backend>
      --web-dir ${CMAKE_SOURCE_DIR}/web)
  set_tests_properties(vmon_e2e PROPERTIES TIMEOUT 240)

  # Python 客户端单测：traceparent 编解码、Span 父子关系、HTTP 传播。
  add_test(NAME vmon_python_client
    COMMAND Python3::Interpreter -m unittest discover
      -s ${CMAKE_SOURCE_DIR}/clients/python -p test_*.py)
  set_tests_properties(vmon_python_client PROPERTIES TIMEOUT 60)

  find_program(NODE_EXECUTABLE node)
  if(NODE_EXECUTABLE)
    add_test(NAME vmon_web_client
      COMMAND ${NODE_EXECUTABLE} --test
        ${CMAKE_SOURCE_DIR}/clients/web/test_vmon_trace.js)
    set_tests_properties(vmon_web_client PROPERTIES TIMEOUT 60)
  endif()
else()
  message(STATUS "未找到 Python3，跳过端到端测试 vmon_e2e")
endif()

# ---------------------------------------------------------------- 单元测试
# 单元测试：优先使用 third_party 中已解压的 GoogleTest；
# 未下载时跳过测试，保证“只跑运行时”的机器也能构建。
set(VMON_GTEST_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/src/googletest-release-1.12.1/googletest-release-1.12.1")

if(NOT EXISTS "${VMON_GTEST_ROOT}/CMakeLists.txt")
  message(WARNING "未找到 GoogleTest 源码，跳过单元测试：${VMON_GTEST_ROOT}")
  return()
endif()

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${VMON_GTEST_ROOT}" "${CMAKE_BINARY_DIR}/third_party/googletest"
                 EXCLUDE_FROM_ALL)

add_executable(vmon_sdk_tests
  ring_buffer_test.cpp
  span_test.cpp
  propagation_test.cpp
  socket_header_test.cpp
)

target_link_libraries(vmon_sdk_tests PRIVATE vmon GTest::gtest_main)
target_include_directories(vmon_sdk_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/sdk/src
  ${CMAKE_SOURCE_DIR}
)

add_test(NAME vmon_sdk_tests COMMAND vmon_sdk_tests)

# ---------------------------------------------------------------- 服务端单测
add_executable(vmon_server_tests
  ${CMAKE_SOURCE_DIR}/server/tests/api_test.cpp
  ${CMAKE_SOURCE_DIR}/server/tests/commands_test.cpp
  ${CMAKE_SOURCE_DIR}/server/tests/evidence_test.cpp
  ${CMAKE_SOURCE_DIR}/server/tests/log_cluster_test.cpp
  ${CMAKE_SOURCE_DIR}/server/tests/profiler_crash_test.cpp
  ${CMAKE_SOURCE_DIR}/server/tests/rules_test.cpp
  ${CMAKE_SOURCE_DIR}/server/tests/slo_test.cpp
  ${CMAKE_SOURCE_DIR}/server/tests/store_test.cpp
  ${CMAKE_SOURCE_DIR}/server/tests/trace_test.cpp
)
target_link_libraries(vmon_server_tests PRIVATE
  vmon_server_core GTest::gtest_main)
target_include_directories(vmon_server_tests PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/server/src
)
add_test(NAME vmon_server_tests COMMAND vmon_server_tests)

# ---------------------------------------------------------------- Agent 单测
add_executable(vmon_agent_tests
  ${CMAKE_SOURCE_DIR}/agent/tests/control_command_test.cpp
  ${CMAKE_SOURCE_DIR}/agent/tests/ingest_test.cpp
  ${CMAKE_SOURCE_DIR}/agent/tests/log_tail_test.cpp
  ${CMAKE_SOURCE_DIR}/agent/tests/metrics_test.cpp
  ${CMAKE_SOURCE_DIR}/agent/tests/process_scan_test.cpp
  ${CMAKE_SOURCE_DIR}/agent/tests/uploader_crash_test.cpp
  ${CMAKE_SOURCE_DIR}/agent/tests/wal_test.cpp
)
target_link_libraries(vmon_agent_tests PRIVATE
  vmon_agent_core vmon GTest::gtest_main)
target_include_directories(vmon_agent_tests PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/agent/src
)
add_test(NAME vmon_agent_tests COMMAND vmon_agent_tests)
