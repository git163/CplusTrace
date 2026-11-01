// SDK 运行期内部配置（由 init() 写入，热路径只读原子变量）。
#pragma once

#include <cstdint>

namespace vmon {
namespace detail {

uint32_t sample_permyriad() noexcept;

}  // namespace detail
}  // namespace vmon
