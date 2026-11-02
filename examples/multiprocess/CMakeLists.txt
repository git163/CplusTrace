add_executable(vmon_multiprocess main.cpp)
target_link_libraries(vmon_multiprocess PRIVATE vmon vmon_third_party)
# 使用 adapters/grpc/propagation.h 编码 traceparent。
target_include_directories(vmon_multiprocess PRIVATE ${CMAKE_SOURCE_DIR})
