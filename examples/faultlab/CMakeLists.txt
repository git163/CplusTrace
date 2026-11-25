add_executable(vmon_faultlab main.cpp)
target_link_libraries(vmon_faultlab PRIVATE vmon vmon_third_party)
# adapters/grpc/propagation.h 与 vmon/vmon.h 在源码根目录下。
target_include_directories(vmon_faultlab PRIVATE ${CMAKE_SOURCE_DIR})
