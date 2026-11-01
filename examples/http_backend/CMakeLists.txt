add_executable(vmon_http_backend main.cpp)
# cpp-httplib / nlohmann json 由 vmon_third_party 提供头文件。
target_link_libraries(vmon_http_backend PRIVATE vmon vmon_third_party)
