#include "core/registry.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace vmon {
namespace detail {
namespace {

struct Registry {
  std::mutex mutex;
  std::vector<ThreadContext*> contexts;
};

// 故意不析构：线程退出可能发生在静态析构之后，注册表必须始终有效。
Registry& registry() noexcept {
  static Registry* instance = new Registry();
  return *instance;
}

}  // namespace

void register_thread_context(ThreadContext* ctx) noexcept {
  Registry& reg = registry();
  std::lock_guard<std::mutex> lock(reg.mutex);
  reg.contexts.push_back(ctx);
}

void unregister_thread_context(ThreadContext* ctx) noexcept {
  Registry& reg = registry();
  std::lock_guard<std::mutex> lock(reg.mutex);
  auto it = std::find(reg.contexts.begin(), reg.contexts.end(), ctx);
  if (it != reg.contexts.end()) {
    reg.contexts.erase(it);
  }
}

void for_each_thread_context(ContextVisitor visitor, void* user) noexcept {
  if (visitor == nullptr) return;
  Registry& reg = registry();
  std::lock_guard<std::mutex> lock(reg.mutex);
  for (ThreadContext* ctx : reg.contexts) {
    visitor(ctx, user);
  }
}

}  // namespace detail
}  // namespace vmon
