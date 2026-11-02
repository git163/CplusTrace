#!/usr/bin/env bash
# 一键编译脚本：配置 + 构建，可选跑测试。
#
# 用法：
#   ./scripts/build.sh                    # Debug + 单测 + 示例
#   ./scripts/build.sh --release          # Release
#   ./scripts/build.sh --test             # 构建后跑 ctest
#   ./scripts/build.sh --clean            # 删除构建目录后重新构建
#   ./scripts/build.sh --cpu-profiler     # 打开常驻 CPU SIGPROF（诊断版）
#   ./scripts/build.sh --ebpf --grpc      # 打开 eBPF/gRPC 接口编译开关
#   ./scripts/build.sh --no-tests         # 不构建测试（更快）
#   ./scripts/build.sh -j 4               # 指定并行度
#
# 常用环境变量：
#   BUILD_DIR=build    构建目录
#   CMAKE=cmake        cmake 可执行文件
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build"}
CMAKE=${CMAKE:-cmake}
BUILD_TYPE=Debug
BUILD_TESTS=ON
BUILD_EXAMPLES=ON
CPU_PROFILER=OFF
EBPF=OFF
GRPC=OFF
CLEAN=0
RUN_TESTS=0
JOBS=""

log() { printf '[build] %s\n' "$*"; }
err() { printf '[build] 错误：%s\n' "$*" >&2; }

usage() {
  sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug) BUILD_TYPE=Debug ;;
    --release) BUILD_TYPE=Release ;;
    --clean) CLEAN=1 ;;
    --test|--run-tests) RUN_TESTS=1 ;;
    --no-tests) BUILD_TESTS=OFF ;;
    --no-examples) BUILD_EXAMPLES=OFF ;;
    --cpu-profiler) CPU_PROFILER=ON ;;
    --ebpf) EBPF=ON ;;
    --grpc) GRPC=ON ;;
    -j) JOBS="$2"; shift ;;
    -j*) JOBS="${1#-j}" ;;
    -h|--help) usage; exit 0 ;;
    *) err "未知参数：$1"; usage; exit 2 ;;
  esac
  shift
done

if [[ -z "$JOBS" ]]; then
  if command -v sysctl >/dev/null 2>&1; then
    JOBS=$(sysctl -n hw.ncpu 2>/dev/null || true)
  fi
  if [[ -z "$JOBS" ]] && command -v nproc >/dev/null 2>&1; then
    JOBS=$(nproc)
  fi
  JOBS=${JOBS:-4}
fi

if [[ ! -d "$ROOT/third_party/src" ]]; then
  err "缺少 third_party/src；请先执行："
  err "  ./scripts/download-third-party.sh --proxy http://127.0.0.1:7890"
  exit 1
fi

if [[ ! -f "$ROOT/CMakeLists.txt" ]]; then
  err "找不到 CMakeLists.txt：$ROOT"
  exit 1
fi

if [[ "$CLEAN" == "1" ]]; then
  if [[ -z "$BUILD_DIR" || "$BUILD_DIR" == "/" || "$BUILD_DIR" == "$ROOT" ]]; then
    err "拒绝删除危险路径：$BUILD_DIR"
    exit 2
  fi
  log "删除构建目录：$BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

log "配置：$BUILD_TYPE  build=$BUILD_DIR  tests=$BUILD_TESTS  examples=$BUILD_EXAMPLES"
"$CMAKE" -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DVMON_BUILD_TESTS="$BUILD_TESTS" \
  -DVMON_BUILD_EXAMPLES="$BUILD_EXAMPLES" \
  -DVMON_ENABLE_CPU_PROFILER="$CPU_PROFILER" \
  -DVMON_ENABLE_EBPF="$EBPF" \
  -DVMON_ENABLE_GRPC="$GRPC"

log "编译：-j $JOBS"
"$CMAKE" --build "$BUILD_DIR" -j "$JOBS"

if [[ "$RUN_TESTS" == "1" ]]; then
  if [[ "$BUILD_TESTS" != "ON" ]]; then
    err "--test 需要测试目标；不要同时使用 --no-tests"
    exit 2
  fi
  log "运行测试"
  (cd "$BUILD_DIR" && ctest --output-on-failure)
fi

cat <<EOF

[build] 完成
  Server : $BUILD_DIR/vmon_server
  Agent  : $BUILD_DIR/vmon_agent
  CLI    : $BUILD_DIR/tools/vmonctl
  测试   : $BUILD_DIR/tests/
  启动   : ./scripts/vmon.sh up
  示例   : python3 examples/scenarios/demo.py --scenario all --keep
EOF
