#!/usr/bin/env bash
# =============================================================================
# 生成 third_party/deps.tsv
# =============================================================================
# 从 third_party/downloads/ 中已有的文件计算 SHA256，并输出机器可读的依赖清单。
# 缺失的文件会被跳过（不会报错），便于不同 OS/架构分阶段下载。
#
# 用法：
#   ./scripts/gen-deps.sh            # 重新生成 third_party/deps.tsv
# =============================================================================

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/third_party/downloads"
OUT="$ROOT/third_party/deps.tsv"

if command -v sha256sum >/dev/null 2>&1; then
  SHA=(sha256sum)
else
  SHA=(shasum -a 256)
fi

sha_of() {
  local f="$1"
  if [[ -f "$f" ]]; then
    "${SHA[@]}" "$f" | awk '{print $1}'
  else
    echo "-"
  fi
}

emit() {
  # name version license kind os arch filename url delivery
  local name="$1" version="$2" license="$3" kind="$4"
  local os="$5" arch="$6" filename="$7" url="$8" delivery="$9"
  local subdir
  case "$kind" in
    source)  subdir="sources" ;;
    binary)  subdir="binaries" ;;
    asset)   subdir="frontend" ;;
    *)       subdir="" ;;
  esac

  local path="$DEST/$subdir/$filename"
  local hash="-"
  if [[ -n "$subdir" && -f "$path" ]]; then
    hash="$(sha_of "$path")"
  elif [[ -n "$subdir" ]]; then
    return 0   # 当前阶段未下载，跳过
  fi

  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$name" "$version" "$license" "$kind" "$os" "$arch" "$filename" "$hash" "$url" "$delivery"
}

{
  printf '# 生成时间: %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf '# 说明: 源码与前端资源与 OS/CPU 无关；二进制按平台分列。\n'
  printf '# 用法: Linux 移植阶段执行 ./scripts/download-third-party.sh --os linux --arch amd64 后重新生成本文件。\n'
  printf 'name\tversion\tlicense\tkind\tos\tarch\tfilename\tsha256\turl\tdelivery\n'

  # --- 复用目标系统，不下载 ---
  emit "gRPC"                 "目标版本" "Apache-2.0"          reuse any any "-" "-" "复用目标系统"
  emit "protobuf"             "目标版本" "BSD-3-Clause"        reuse any any "-" "-" "复用目标系统"
  emit "abseil"               "目标版本" "Apache-2.0"          reuse any any "-" "-" "复用目标系统"
  emit "POCO"                 "目标版本" "BSL-1.0"             reuse any any "-" "-" "复用目标系统"

  # --- 源码 / 头文件（与平台无关） ---
  emit "nlohmann/json"        "3.12.0"   "MIT"                 source any any "nlohmann-json-3.12.0.tar.gz"          "https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz" "源码编译"
  emit "cpp-httplib"          "0.64.0"   "MIT"                 source any any "cpp-httplib-0.64.0.tar.gz"             "https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.64.0.tar.gz" "源码编译"
  emit "CLI11"                "2.7.2"    "BSD-3-Clause"        source any any "CLI11-2.7.2.tar.gz"                   "https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.7.2.tar.gz" "源码编译"
  emit "xxHash"               "0.8.4"    "BSD-2-Clause"        source any any "xxHash-0.8.4.tar.gz"                  "https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.4.tar.gz" "源码编译"
  emit "zstd"                 "1.5.7"    "BSD-3-Clause"        source any any "zstd-1.5.7.tar.gz"                    "https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz" "源码编译"
  emit "zlib"                 "1.3.2"    "Zlib"                source any any "zlib-1.3.2.tar.gz"                    "https://github.com/madler/zlib/archive/refs/tags/v1.3.2.tar.gz" "源码编译"
  emit "GoogleTest"           "1.12.1"   "BSD-3-Clause"        source any any "googletest-release-1.12.1.tar.gz"     "https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz" "源码编译"
  emit "Google Benchmark"     "1.10.0"   "Apache-2.0"          source any any "google-benchmark-1.10.0.tar.gz"       "https://github.com/google/benchmark/archive/refs/tags/v1.10.0.tar.gz" "源码编译"
  emit "toml++"               "3.4.0"    "MIT"                 source any any "tomlplusplus-3.4.0.tar.gz"             "https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz" "源码编译（可选）"
  emit "RE2"                  "2025-11-05" "BSD-3-Clause"      source any any "re2-2025-11-05.tar.gz"                "https://github.com/google/re2/archive/refs/tags/2025-11-05.tar.gz" "源码编译（可选）"
  emit "fmt"                  "12.2.0"   "MIT"                 source any any "fmt-12.2.0.tar.gz"                    "https://github.com/fmtlib/fmt/archive/refs/tags/12.2.0.tar.gz" "源码编译（可选）"
  emit "yaml-cpp"             "0.9.0"    "MIT"                 source any any "yaml-cpp-0.9.0.tar.gz"                 "https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.9.0.tar.gz" "源码编译（可选）"
  emit "cpptrace"             "1.0.4"    "MIT"                 source any any "cpptrace-1.0.4.tar.gz"                 "https://github.com/jeremy-rifkin/cpptrace/archive/refs/tags/v1.0.4.tar.gz" "源码编译（可选）"
  emit "backward-cpp"         "1.6"      "MIT"                 source any any "backward-cpp-1.6.tar.gz"               "https://github.com/bombela/backward-cpp/archive/refs/tags/v1.6.tar.gz" "源码编译（可选）"
  emit "concurrentqueue"      "1.0.5"    "BSD-2-Clause"        source any any "concurrentqueue-1.0.5.tar.gz"          "https://github.com/cameron314/concurrentqueue/archive/refs/tags/v1.0.5.tar.gz" "源码编译（可选）"
  emit "libbpf"               "1.8.0"    "BSD-2-Clause"        source any any "libbpf-1.8.0.tar.gz"                  "https://github.com/libbpf/libbpf/archive/refs/tags/v1.8.0.tar.gz" "源码编译（可选，仅 Linux）"

  # --- 运行时二进制：macOS arm64（首期本机验证） ---
  emit "VictoriaMetrics"      "1.155.0"  "Apache-2.0"          binary darwin arm64 "victoria-metrics-darwin-arm64-v1.155.0.tar.gz" "https://github.com/VictoriaMetrics/VictoriaMetrics/releases/download/v1.155.0/victoria-metrics-darwin-arm64-v1.155.0.tar.gz" "预编译二进制"
  emit "vmutils"              "1.155.0"  "Apache-2.0"          binary darwin arm64 "vmutils-darwin-arm64-v1.155.0.tar.gz"           "https://github.com/VictoriaMetrics/VictoriaMetrics/releases/download/v1.155.0/vmutils-darwin-arm64-v1.155.0.tar.gz" "预编译二进制"
  emit "VictoriaLogs"         "1.53.0"   "Apache-2.0"          binary darwin arm64 "victoria-logs-darwin-arm64-v1.53.0.tar.gz"     "https://github.com/VictoriaMetrics/VictoriaLogs/releases/download/v1.53.0/victoria-logs-darwin-arm64-v1.53.0.tar.gz" "预编译二进制"
  emit "vlutils"              "1.53.0"   "Apache-2.0"          binary darwin arm64 "vlutils-darwin-arm64-v1.53.0.tar.gz"           "https://github.com/VictoriaMetrics/VictoriaLogs/releases/download/v1.53.0/vlutils-darwin-arm64-v1.53.0.tar.gz" "预编译二进制"
  emit "Jaeger"               "2.22.0"   "Apache-2.0"          binary darwin arm64 "jaeger-2.22.0-darwin-arm64.tar.gz"             "https://github.com/jaegertracing/jaeger/releases/download/v2.22.0/jaeger-2.22.0-darwin-arm64.tar.gz" "预编译二进制"
  emit "Alertmanager"         "0.34.2"   "Apache-2.0"          binary darwin arm64 "alertmanager-0.34.2.darwin-arm64.tar.gz"       "https://github.com/prometheus/alertmanager/releases/download/v0.34.2/alertmanager-0.34.2.darwin-arm64.tar.gz" "预编译二进制（可选）"

  # --- 运行时二进制：Linux amd64（已下载的会列出，缺失则跳过；移植阶段补齐） ---
  emit "VictoriaMetrics"      "1.155.0"  "Apache-2.0"          binary linux amd64 "victoria-metrics-linux-amd64-v1.155.0.tar.gz" "https://github.com/VictoriaMetrics/VictoriaMetrics/releases/download/v1.155.0/victoria-metrics-linux-amd64-v1.155.0.tar.gz" "预编译二进制"
  emit "vmutils"              "1.155.0"  "Apache-2.0"          binary linux amd64 "vmutils-linux-amd64-v1.155.0.tar.gz"           "https://github.com/VictoriaMetrics/VictoriaMetrics/releases/download/v1.155.0/vmutils-linux-amd64-v1.155.0.tar.gz" "预编译二进制"
  emit "VictoriaLogs"         "1.53.0"   "Apache-2.0"          binary linux amd64 "victoria-logs-linux-amd64-v1.53.0.tar.gz"     "https://github.com/VictoriaMetrics/VictoriaLogs/releases/download/v1.53.0/victoria-logs-linux-amd64-v1.53.0.tar.gz" "预编译二进制"
  emit "vlutils"              "1.53.0"   "Apache-2.0"          binary linux amd64 "vlutils-linux-amd64-v1.53.0.tar.gz"           "https://github.com/VictoriaMetrics/VictoriaLogs/releases/download/v1.53.0/vlutils-linux-amd64-v1.53.0.tar.gz" "预编译二进制"
  emit "Jaeger"               "2.22.0"   "Apache-2.0"          binary linux amd64 "jaeger-2.22.0-linux-amd64.tar.gz"             "https://github.com/jaegertracing/jaeger/releases/download/v2.22.0/jaeger-2.22.0-linux-amd64.tar.gz" "预编译二进制"
  emit "Alertmanager"         "0.34.2"   "Apache-2.0"          binary linux amd64 "alertmanager-0.34.2.linux-amd64.tar.gz"       "https://github.com/prometheus/alertmanager/releases/download/v0.34.2/alertmanager-0.34.2.linux-amd64.tar.gz" "预编译二进制（可选）"

  # --- 前端静态资源 ---
  emit "uPlot"                "1.6.32"   "MIT"                 asset any any "uplot.iife.min.js" "https://unpkg.com/uplot@1.6.32/dist/uplot.iife.min.js" "静态资源"
  emit "uPlot (CSS)"          "1.6.32"   "MIT"                 asset any any "uPlot.min.css"     "https://unpkg.com/uplot@1.6.32/dist/uPlot.min.css" "静态资源"
  emit "htmx"                 "4.0.0"    "BSD-2-Clause"        asset any any "htmx.min.js"       "https://unpkg.com/htmx.org@4.0.0/dist/htmx.min.js" "静态资源"
} > "$OUT"

echo "已生成: $OUT"
echo "行数: $(wc -l < "$OUT" | tr -d ' ')"
