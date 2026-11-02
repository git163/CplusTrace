#!/usr/bin/env bash
# =============================================================================
# CplusTrace / vmon 三方依赖下载脚本
# =============================================================================
# 用法：
#   ./scripts/download-third-party.sh                 # 源码 + 本机 OS/架构的二进制
#   ./scripts/download-third-party.sh --os linux --arch amd64   # 指定目标 OS/架构
#   ./scripts/download-third-party.sh --all-arch      # 所选 OS 的 amd64/arm64 二进制
#   ./scripts/download-third-party.sh --sources       # 只下载源码/头文件
#   ./scripts/download-third-party.sh --binaries      # 只下载运行时二进制
#   ./scripts/download-third-party.sh --dry-run       # 只打印链接，不下载
#   ./scripts/download-third-party.sh --extract       # 下载后解压到 third_party/src 与 third_party/bin
#   ./scripts/download-third-party.sh --proxy URL     # 指定代理，例如 http://127.0.0.1:7890
#   ./scripts/download-third-party.sh --no-proxy      # 禁用自动代理检测
#
# 操作系统与目标环境：
#   --os 默认取本机：macOS 开发机下载 darwin 二进制，本机先跑通；Linux 目标机用 --os linux。
#   源码包与前端资源与 OS/CPU 无关，只下载一次。
#   - macOS arm64 : 首期开发/验证环境（当前开发机），用 clang 编译
#   - x86_64 Linux: 目标生产环境，gcc 12.3，C++14 基线
#   - aarch64 Linux: 目标生产环境，gcc 7.3.0，C++14 基线，但注意：
#       * 不要使用 std::filesystem（gcc 8+ 才完整），用 POSIX 或 Poco::File
#       * 不要使用 std::format / std::span / std::jthread / std::bit_cast（C++20）
#       * 不要依赖 std::to_chars/from_chars 的浮点支持（gcc 7 不完整）
#       * 链接 C++ 库时确认 _GLIBCXX_USE_CXX11_ABI 与目标系统一致
#       * aarch64 上部分库可能需要 -latomic
#
# 不在本脚本下载的组件（复用目标系统现有版本）：
#   - gRPC / protobuf / abseil：必须与目标系统完全一致。目标 protobuf 为 3.0，
#     新版 gRPC 与其不兼容；gRPC 适配器应随目标工程编译，不要静态链接第二份。
#   - POCO：目标系统已有线程池封装，直接复用；注意版本与 _GLIBCXX_USE_CXX11_ABI。
#   - OpenSSL / zlib / libunwind：优先使用系统包（见文末注释）。
#
# 许可证策略：
#   默认只下载 Apache-2.0 / MIT / BSD / ISC / zlib / Public Domain 组件。
#   GPL/AGPL 组件（gdb、perf、binutils、bpftool、Grafana）不进入产品；
#   如需使用，只作为独立外部工具，且不随产品分发。
# =============================================================================

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${DEST:-$ROOT/third_party/downloads}"
MODE="all"           # all | sources | binaries
ARCH_MODE="current"  # current | all | amd64 | arm64
OS_MODE="host"       # host | all | linux | darwin
DRY_RUN=0
EXTRACT=0
FAILED=0
PROXY="${PROXY:-}"
AUTO_PROXY=1

usage() {
  # 打印文件头注释，遇到第一条非注释行即停止（避免把脚本正文一起打出来）
  awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "${BASH_SOURCE[0]}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --all-arch)   ARCH_MODE="all"; shift ;;
    --arch)       ARCH_MODE="${2:-}"; shift 2 ;;
    --os)         OS_MODE="${2:-}"; shift 2 ;;
    --sources)    MODE="sources"; shift ;;
    --binaries)   MODE="binaries"; shift ;;
    --dest)       DEST="${2:-}"; shift 2 ;;
    --dry-run)    DRY_RUN=1; shift ;;
    --extract)    EXTRACT=1; shift ;;
    --proxy)      PROXY="${2:-}"; AUTO_PROXY=0; shift 2 ;;
    --no-proxy)   PROXY=""; AUTO_PROXY=0; shift ;;
    -h|--help)    usage; exit 0 ;;
    *)            echo "未知参数: $1" >&2; usage; exit 1 ;;
  esac
done

case "$(uname -m)" in
  x86_64|amd64)   HOST_ARCH="amd64" ;;
  aarch64|arm64)  HOST_ARCH="arm64" ;;
  *)              echo "不支持的架构: $(uname -m)" >&2; exit 1 ;;
esac

case "$(uname -s)" in
  Darwin) HOST_OS="darwin" ;;
  Linux)  HOST_OS="linux" ;;
  *)      echo "不支持的操作系统: $(uname -s)" >&2; exit 1 ;;
esac

case "$ARCH_MODE" in
  current) ARCHS=("$HOST_ARCH") ;;
  all)     ARCHS=("amd64" "arm64") ;;
  amd64|arm64) ARCHS=("$ARCH_MODE") ;;
  *)       echo "无效 --arch: ${ARCH_MODE}（可选 amd64/arm64/all/current）" >&2; exit 1 ;;
esac

case "$OS_MODE" in
  host)    OSES=("$HOST_OS") ;;
  all)     OSES=("linux" "darwin") ;;
  linux|darwin) OSES=("$OS_MODE") ;;
  *)       echo "无效 --os: ${OS_MODE}（可选 linux/darwin/all/host）" >&2; exit 1 ;;
esac

mkdir -p "$DEST/sources" "$DEST/binaries" "$DEST/frontend"

# -----------------------------------------------------------------------------
# 代理检测：macOS 读取系统代理，Linux 读取环境变量
# -----------------------------------------------------------------------------
detect_system_proxy() {
  if [[ -n "${HTTPS_PROXY:-}" ]]; then echo "$HTTPS_PROXY"; return; fi
  if [[ -n "${https_proxy:-}" ]]; then echo "$https_proxy"; return; fi

  if command -v scutil >/dev/null 2>&1; then
    local host port
    host="$(scutil --proxy | awk '/HTTPSProxy/ {print $3; exit}')"
    port="$(scutil --proxy | awk '/HTTPSPort/ {print $3; exit}')"
    if [[ -n "$host" && -n "$port" ]]; then
      echo "http://$host:$port"
      return
    fi
    host="$(scutil --proxy | awk '/HTTPProxy/ {print $3; exit}')"
    port="$(scutil --proxy | awk '/HTTPPort/ {print $3; exit}')"
    if [[ -n "$host" && -n "$port" ]]; then
      echo "http://$host:$port"
      return
    fi
  fi

  echo ""
}

if [[ "$AUTO_PROXY" -eq 1 && -z "$PROXY" ]]; then
  PROXY="$(detect_system_proxy)"
fi
if [[ -n "$PROXY" && "$PROXY" != http* && "$PROXY" != socks* ]]; then
  PROXY="http://$PROXY"
fi
if [[ -n "$PROXY" ]]; then
  echo "使用代理: $PROXY"
else
  echo "未检测到代理，直连下载"
fi

curl_fetch() {
  if [[ -n "$PROXY" ]]; then
    curl --proxy "$PROXY" "$@"
  else
    curl "$@"
  fi
}

# -----------------------------------------------------------------------------
# 下载函数：支持断点续传、失败继续、dry-run
# -----------------------------------------------------------------------------
fetch() {
  local name="$1"
  local url="$2"
  local subdir="${3:-sources}"
  local optional="${4:-0}"
  local dir="$DEST/$subdir"
  local out="$dir/$name"
  local part="$out.part"

  mkdir -p "$dir"

  if [[ -s "$out" ]]; then
    printf '[skip] %-52s %s\n' "$name" "$out"
    return 0
  fi

  printf '[get ] %-52s\n       %s\n' "$name" "$url"

  if [[ "$DRY_RUN" -eq 1 ]]; then
    return 0
  fi

  # 先写 .part，完整下载后再改名：进程被杀时不会把半截文件当成已完成，
  # 下次运行可用 -C - 断点续传。
  if ! curl_fetch -fL --retry 5 --retry-delay 2 --connect-timeout 20 -C - -o "$part" "$url"; then
    printf '[FAIL] %s\n' "$name" >&2
    if [[ "$optional" -eq 0 ]]; then
      FAILED=$((FAILED + 1))
    else
      printf '[warn] 可选组件下载失败，已跳过: %s\n' "$name" >&2
    fi
    return 1
  fi

  mv -f "$part" "$out"
}

# -----------------------------------------------------------------------------
# 源码 / 头文件（与 CPU 架构无关）
# -----------------------------------------------------------------------------
if [[ "$MODE" == "all" || "$MODE" == "sources" ]]; then
  echo "== 源码 / 头文件 =="

  # --- 基础通用库：gcc 7.3 / gcc 12.3 都应可编译 ---
  fetch "nlohmann-json-3.12.0.tar.gz" \
    "https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz"

  fetch "cpp-httplib-0.64.0.tar.gz" \
    "https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.64.0.tar.gz"

  fetch "CLI11-2.7.2.tar.gz" \
    "https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.7.2.tar.gz"

  fetch "xxHash-0.8.4.tar.gz" \
    "https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.4.tar.gz"

  fetch "zstd-1.5.7.tar.gz" \
    "https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz"

  fetch "zlib-1.3.2.tar.gz" \
    "https://github.com/madler/zlib/archive/refs/tags/v1.3.2.tar.gz"

  # 测试：gcc 7.3 建议用 1.12.1（C++14 基线，不依赖 std::filesystem）
  fetch "googletest-release-1.12.1.tar.gz" \
    "https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz"

  fetch "google-benchmark-1.10.0.tar.gz" \
    "https://github.com/google/benchmark/archive/refs/tags/v1.10.0.tar.gz"

  # --- 可选：gcc 12.3 / 新工具链优先；aarch64 gcc 7.3 如编译失败可先跳过 ---
  # TOML 配置。若 gcc 7.3 下 toml++ 的字符转换有问题，改用 nlohmann/json 做配置。
  fetch "tomlplusplus-3.4.0.tar.gz" \
    "https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz" \
    sources 1

  # 日志字段提取（M2）。RE2 依赖 abseil，gcc 7.3 上建议先在 x86 验证。
  fetch "re2-2025-11-05.tar.gz" \
    "https://github.com/google/re2/archive/refs/tags/2025-11-05.tar.gz" \
    sources 1

  # 可选格式化库；若 gcc 7.3 编译失败，用 snprintf 或自定义格式化替代。
  fetch "fmt-12.2.0.tar.gz" \
    "https://github.com/fmtlib/fmt/archive/refs/tags/12.2.0.tar.gz" \
    sources 1

  # 可选 YAML 配置；与 toml++ 二选一。
  fetch "yaml-cpp-0.9.0.tar.gz" \
    "https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.9.0.tar.gz" \
    sources 1

  # 可选栈回溯库；不要用于信号处理器内部。
  fetch "cpptrace-1.0.4.tar.gz" \
    "https://github.com/jeremy-rifkin/cpptrace/archive/refs/tags/v1.0.4.tar.gz" \
    sources 1

  fetch "backward-cpp-1.6.tar.gz" \
    "https://github.com/bombela/backward-cpp/archive/refs/tags/v1.6.tar.gz" \
    sources 1

  # 可选 MPMC 队列（Agent 内部用；SDK 热路径仍用自研 SPSC）。
  fetch "concurrentqueue-1.0.5.tar.gz" \
    "https://github.com/cameron314/concurrentqueue/archive/refs/tags/v1.0.5.tar.gz" \
    sources 1

  # 可选 M3：eBPF。libbpf 选 BSD-2 分支；bpftool 是 GPL 外部工具，不随产品分发。
  fetch "libbpf-1.8.0.tar.gz" \
    "https://github.com/libbpf/libbpf/archive/refs/tags/v1.8.0.tar.gz" \
    sources 1

  # 注意：bpftool 默认不下载。需要时自行从以下页面获取，并只作为外部工具使用：
  #   https://github.com/libbpf/bpftool/releases/tag/v7.8.1

  # 注意：SQLite 可选。若 Agent 本地 WAL 用分段文件实现，则不需要 SQLite。
  # 若需要 amalgamation，请到 https://sqlite.org/download.html 选择
  # sqlite-amalgamation-*.zip（Public Domain），脚本不写死版本号。
fi

# -----------------------------------------------------------------------------
# 运行时二进制（Go 静态二进制，按架构区分）
# -----------------------------------------------------------------------------
if [[ "$MODE" == "all" || "$MODE" == "binaries" ]]; then
  echo "== 运行时二进制 =="

  # Jaeger 的 release 级 checksum 覆盖所有架构的 tarball；
  # 单架构的 .sha256sum.txt 校验的是解压后的文件，不适用于 tarball，因此不用。
  fetch "jaeger-2.22.0.sha256sum.txt" \
    "https://github.com/jaegertracing/jaeger/releases/download/v2.22.0/jaeger-2.22.0.sha256sum.txt" \
    binaries 1

  for tgt_os in "${OSES[@]}"; do
    for arch in "${ARCHS[@]}"; do
      echo "-- os: $tgt_os, arch: $arch --"

      # VictoriaMetrics 指标存储 + vmutils（含 vmalert / vmagent）
      fetch "victoria-metrics-${tgt_os}-${arch}-v1.155.0.tar.gz" \
        "https://github.com/VictoriaMetrics/VictoriaMetrics/releases/download/v1.155.0/victoria-metrics-${tgt_os}-${arch}-v1.155.0.tar.gz" \
        binaries

      fetch "vmutils-${tgt_os}-${arch}-v1.155.0.tar.gz" \
        "https://github.com/VictoriaMetrics/VictoriaMetrics/releases/download/v1.155.0/vmutils-${tgt_os}-${arch}-v1.155.0.tar.gz" \
        binaries

      fetch "victoria-metrics-${tgt_os}-${arch}-v1.155.0_checksums.txt" \
        "https://github.com/VictoriaMetrics/VictoriaMetrics/releases/download/v1.155.0/victoria-metrics-${tgt_os}-${arch}-v1.155.0_checksums.txt" \
        binaries 1

      fetch "vmutils-${tgt_os}-${arch}-v1.155.0_checksums.txt" \
        "https://github.com/VictoriaMetrics/VictoriaMetrics/releases/download/v1.155.0/vmutils-${tgt_os}-${arch}-v1.155.0_checksums.txt" \
        binaries 1

      # VictoriaLogs 是独立仓库，版本号与 VictoriaMetrics 不同
      fetch "victoria-logs-${tgt_os}-${arch}-v1.53.0.tar.gz" \
        "https://github.com/VictoriaMetrics/VictoriaLogs/releases/download/v1.53.0/victoria-logs-${tgt_os}-${arch}-v1.53.0.tar.gz" \
        binaries

      fetch "vlutils-${tgt_os}-${arch}-v1.53.0.tar.gz" \
        "https://github.com/VictoriaMetrics/VictoriaLogs/releases/download/v1.53.0/vlutils-${tgt_os}-${arch}-v1.53.0.tar.gz" \
        binaries

      fetch "victoria-logs-${tgt_os}-${arch}-v1.53.0_checksums.txt" \
        "https://github.com/VictoriaMetrics/VictoriaLogs/releases/download/v1.53.0/victoria-logs-${tgt_os}-${arch}-v1.53.0_checksums.txt" \
        binaries 1

      fetch "vlutils-${tgt_os}-${arch}-v1.53.0_checksums.txt" \
        "https://github.com/VictoriaMetrics/VictoriaLogs/releases/download/v1.53.0/vlutils-${tgt_os}-${arch}-v1.53.0_checksums.txt" \
        binaries 1

      # Jaeger v2：Trace 存储与查询
      fetch "jaeger-2.22.0-${tgt_os}-${arch}.tar.gz" \
        "https://github.com/jaegertracing/jaeger/releases/download/v2.22.0/jaeger-2.22.0-${tgt_os}-${arch}.tar.gz" \
        binaries

      # Alertmanager（M6 可选）
      fetch "alertmanager-0.34.2.${tgt_os}-${arch}.tar.gz" \
        "https://github.com/prometheus/alertmanager/releases/download/v0.34.2/alertmanager-0.34.2.${tgt_os}-${arch}.tar.gz" \
        binaries 1
    done
  done
fi

# -----------------------------------------------------------------------------
# 前端静态资源（可选）
# -----------------------------------------------------------------------------
if [[ "$MODE" == "all" || "$MODE" == "sources" ]]; then
  echo "== 前端静态资源 =="

  # uPlot：MIT，轻量时序图。用 unpkg，raw.githubusercontent.com 在部分代理下会失败。
  fetch "uplot.iife.min.js" \
    "https://unpkg.com/uplot@1.6.32/dist/uplot.iife.min.js" \
    frontend

  fetch "uPlot.min.css" \
    "https://unpkg.com/uplot@1.6.32/dist/uPlot.min.css" \
    frontend

  # htmx：BSD-2，可选。若不需要可删除。
  fetch "htmx.min.js" \
    "https://unpkg.com/htmx.org@4.0.0/dist/htmx.min.js" \
    frontend 1
fi

# -----------------------------------------------------------------------------
# 校验：官方 checksum（存在则校验，失败只告警） + 本地 SHA256 清单
# -----------------------------------------------------------------------------
if [[ "$DRY_RUN" -eq 0 ]]; then
  echo "== 校验 =="

  if command -v sha256sum >/dev/null 2>&1; then
    SHA_CMD=(sha256sum)
  elif command -v shasum >/dev/null 2>&1; then
    SHA_CMD=(shasum -a 256)
  else
    SHA_CMD=()
  fi

  if [[ ${#SHA_CMD[@]} -gt 0 ]]; then
    for sums in "$DEST"/binaries/*_checksums.txt "$DEST"/binaries/*.sha256sum.txt; do
      [[ -f "$sums" ]] || continue
      echo "-- verify $(basename "$sums") --"
      ( cd "$DEST/binaries" && "${SHA_CMD[@]}" -c --ignore-missing "$(basename "$sums")" ) \
        || echo "[warn] 校验未完全通过，请人工确认: $(basename "$sums")"
    done

    echo "-- 生成本地 SHA256SUMS.local --"
    (
      cd "$DEST" || exit 1
      : > SHA256SUMS.local
      find . -type f ! -name 'SHA256SUMS.local' -print0 \
        | while IFS= read -r -d '' f; do
            "${SHA_CMD[@]}" "$f"
          done >> SHA256SUMS.local
    ) || true
  fi
fi

# -----------------------------------------------------------------------------
# 可选解压
# -----------------------------------------------------------------------------
if [[ "$EXTRACT" -eq 1 && "$DRY_RUN" -eq 0 ]]; then
  echo "== 解压 =="
  mkdir -p "$ROOT/third_party/src" "$ROOT/third_party/bin"

  for f in "$DEST"/sources/*.tar.gz; do
    [[ -f "$f" ]] || continue
    base="$(basename "$f" .tar.gz)"
    mkdir -p "$ROOT/third_party/src/$base"
    tar -xzf "$f" -C "$ROOT/third_party/src/$base" || echo "[warn] 解压失败: $f"
  done

  for f in "$DEST"/binaries/*.tar.gz; do
    [[ -f "$f" ]] || continue
    base="$(basename "$f" .tar.gz)"
    mkdir -p "$ROOT/third_party/bin/$base"
    tar -xzf "$f" -C "$ROOT/third_party/bin/$base" || echo "[warn] 解压失败: $f"
  done
fi

# -----------------------------------------------------------------------------
# 汇总
# -----------------------------------------------------------------------------
echo
echo "下载目录: $DEST"
echo "失败数量: $FAILED"
echo
echo "下一步："
echo "  1. 检查 third_party/downloads/SHA256SUMS.local，把哈希值填入 third_party/deps.tsv"
echo "  2. 目标系统已有的 gRPC/protobuf/abseil/POCO 不要覆盖；适配器随目标工程编译"
echo "  3. 确认 _GLIBCXX_USE_CXX11_ABI 与目标系统一致（gcc 5+ 双 ABI）"
echo "  4. aarch64 gcc 7.3 编译失败时，优先降级依赖或改用系统包，不要强行升级编译器"
echo
echo "系统包建议（离线环境请提前准备 RPM/DEB）："
echo "  RHEL/CentOS: dnf install openssl-devel zlib-devel libunwind-devel libatomic"
echo "  Debian/Ubuntu: apt install libssl-dev zlib1g-dev libunwind-dev libatomic1"
echo
echo "许可证提醒："
echo "  - gdb / perf / binutils / bpftool 是 GPL 工具，只作为外部诊断工具，不打包进产品"
echo "  - Grafana 是 AGPL-3.0，默认不引入；使用自研 UI + uPlot"
echo "  - 第三方源码保留 LICENSE/NOTICE，发布前跑一次许可证扫描"

if [[ "$FAILED" -gt 0 ]]; then
  exit 1
fi
