// 离线符号化：通过外部 addr2line / llvm-symbolizer 解析地址。
//
// 约束：
//   - 只在 Agent/Server 侧离线执行，绝不在崩溃进程内做重活；
//   - 命令通过 fork/exec 直接传 argv，不经过 shell，避免命令注入；
//   - 找不到工具或二进制时返回错误，不影响崩溃记录入库。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vmon {
namespace symbolizer {

struct Frame {
  std::string address;
  std::string function;
  std::string file;
  int line = 0;
  bool resolved = false;
};

struct BuildId {
  std::string value;  // ELF build-id / Mach-O UUID
  std::string kind;   // elf / macho
  bool valid = false;
};

class Symbolizer {
 public:
  // 允许显式指定工具路径；为空时自动探测 addr2line / llvm-symbolizer。
  void set_tool(const std::string& tool) { tool_ = tool; }

  std::string tool() const;

  // 读取二进制的 build-id（Linux 用 readelf，macOS 用 dwarfdump）。
  BuildId build_id_of(const std::string& binary) const;

  // 符号化一组地址；地址可为 "0x1234" 或纯十六进制。
  bool symbolize(const std::string& binary,
                 const std::vector<std::string>& addresses,
                 std::vector<Frame>* frames, std::string* error) const;

  // 从一组原始栈帧文本中提取地址：支持 "0x1234"、"pc 0x1234"、"#3 0x..."。
  static std::string extract_address(const std::string& frame_text);

 private:
  std::string tool_;
};

}  // namespace symbolizer
}  // namespace vmon
