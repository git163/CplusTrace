// 线程上下文注册表的遍历接口（实现见 registry.cpp）。
#pragma once

#include "core/thread_context.h"

namespace vmon {
namespace detail {

typedef void (*ContextVisitor)(ThreadContext* ctx, void* user);

// 持有注册表锁遍历所有线程上下文；visitor 必须短小且不可重入。
void for_each_thread_context(ContextVisitor visitor, void* user) noexcept;

}  // namespace detail
}  // namespace vmon
