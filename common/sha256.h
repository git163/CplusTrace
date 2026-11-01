// 轻量 SHA-256：用于证据包文件校验与脱敏假名化。
#pragma once

#include <string>

namespace vmon {
namespace common {

std::string sha256_hex(const std::string& data);
std::string sha256_hex(const char* data, size_t size);
std::string sha256_file_hex(const std::string& path);

// 假名化：sha256(salt + value) 的前 12 个十六进制字符。
std::string pseudonymize(const std::string& value, const std::string& salt);

}  // namespace common
}  // namespace vmon
