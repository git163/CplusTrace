# 业务工程一行接入：vmon_attach(<target>)
#
# 用法：
#   list(APPEND CMAKE_MODULE_PATH "/opt/vmon/cmake")
#   include(vmon)
#   vmon_attach(your_target)
#
# 仅链接静态 SDK；不会改变业务编译选项、运行时行为或控制流。
function(vmon_attach target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "vmon_attach: 目标不存在: ${target}")
  endif()
  if(NOT TARGET vmon)
    message(FATAL_ERROR
      "vmon_attach: 未找到 vmon 目标；请先 add_subdirectory(CplusTrace) "
      "或链接已安装的 libvmon.a")
  endif()
  target_link_libraries(${target} PRIVATE vmon)
endfunction()
