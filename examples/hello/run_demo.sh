#!/usr/bin/env bash
# 一键运行最小示例：启动 Server + Agent + vmon_hello，等待数据到达后查询。
#
# 用法：
#   ./examples/hello/run_demo.sh              # 演示完自动清理
#   DEMO_KEEP=1 ./examples/hello/run_demo.sh  # 保留进程与目录，便于打开 Web UI
#   PORT=19428 ./examples/hello/run_demo.sh   # 指定端口
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${BUILD_DIR:-"$ROOT/build"}
PORT=${PORT:-18429}
TOKEN=${VMON_TOKEN:-demo-token}
KEEP=${DEMO_KEEP:-0}
WORK=${WORK_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/vmon-demo-XXXXXX")}
SERVER_LOG="$WORK/server.log"
AGENT_LOG="$WORK/agent.log"
UDS="$WORK/agent.sock"

if [[ ! -x "$BUILD/vmon_server" || ! -x "$BUILD/vmon_agent" ||
      ! -x "$BUILD/examples/hello/vmon_hello" ]]; then
  echo "未找到构建产物，请先执行："
  echo "  cmake -S \"$ROOT\" -B \"$ROOT/build\" -DCMAKE_BUILD_TYPE=Debug"
  echo "  cmake --build \"$ROOT/build\" -j"
  exit 1
fi

mkdir -p "$WORK/server" "$WORK/agent" "$WORK/ctrl" "$WORK/crash"
SERVER_PID=""
AGENT_PID=""

cleanup() {
  if [[ -n "$AGENT_PID" ]]; then kill "$AGENT_PID" 2>/dev/null || true; fi
  if [[ -n "$SERVER_PID" ]]; then kill "$SERVER_PID" 2>/dev/null || true; fi
  if [[ -n "$AGENT_PID" ]]; then wait "$AGENT_PID" 2>/dev/null || true; fi
  if [[ -n "$SERVER_PID" ]]; then wait "$SERVER_PID" 2>/dev/null || true; fi
}
trap cleanup EXIT INT TERM

echo "[demo] 工作目录: $WORK"
"$BUILD/vmon_server" --listen-host 127.0.0.1 --port "$PORT" \
  --token "$TOKEN" --state-dir "$WORK/server" --web-dir "$ROOT/web" \
  --no-forward >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 100); do
  if curl -sf "http://127.0.0.1:$PORT/api/health" >/dev/null 2>&1; then break; fi
  sleep 0.1
done
if ! curl -sf "http://127.0.0.1:$PORT/api/health" >/dev/null 2>&1; then
  echo "[demo] Server 启动失败，日志：$SERVER_LOG"
  cat "$SERVER_LOG"
  exit 1
fi
echo "[demo] Server 已启动: http://127.0.0.1:$PORT"

"$BUILD/vmon_agent" --server-url "http://127.0.0.1:$PORT" \
  --uds "$UDS" --state-dir "$WORK/agent" --node demo-node \
  --token "$TOKEN" --control-dir "$WORK/ctrl" --crash-dir "$WORK/crash" \
  --scan-interval-ms 2000 --upload-interval-ms 200 \
  --process-filter vmon_hello --wal-segment-bytes 65536 \
  >"$AGENT_LOG" 2>&1 &
AGENT_PID=$!

for _ in $(seq 1 100); do
  if [[ -S "$UDS" ]]; then break; fi
  sleep 0.1
done
if [[ ! -S "$UDS" ]]; then
  echo "[demo] Agent 启动失败，日志：$AGENT_LOG"
  cat "$AGENT_LOG"
  exit 1
fi
echo "[demo] Agent 已启动: $UDS"

echo "[demo] 运行 vmon_hello ..."
VMON_AGENT_ENDPOINT="$UDS" VMON_CONTROL_DIR="$WORK/ctrl" \
  "$BUILD/examples/hello/vmon_hello"

echo "[demo] 等待事件上传到中心 ..."
for _ in $(seq 1 100); do
  RESPONSE=$(curl -s -H "X-Vmon-Token: $TOKEN" \
    "http://127.0.0.1:$PORT/api/requests/demo-request-1" || true)
  if [[ "$RESPONSE" == *'"traces":[{'* ]]; then break; fi
  sleep 0.1
done
if [[ "$RESPONSE" != *'"traces":[{'* ]]; then
  echo "[demo] 未查到 trace，Server 日志：$SERVER_LOG，Agent 日志：$AGENT_LOG"
  exit 1
fi

echo
echo "==================== 请求详情 ===================="
printf '%s\n' "$RESPONSE"
echo
echo "==================== 最近日志 ===================="
curl -s -H "X-Vmon-Token: $TOKEN" \
  "http://127.0.0.1:$PORT/api/logs?request_id=demo-request-1&limit=10"
echo
echo
echo "Web UI: http://127.0.0.1:$PORT/"
echo "WAL:    $WORK/agent/wal/"
echo "日志:   $SERVER_LOG  $AGENT_LOG"

if [[ "$KEEP" == "1" ]]; then
  echo
  echo "[demo] DEMO_KEEP=1：进程保持运行，按 Ctrl+C 结束并清理。"
  while true; do sleep 1; done
fi
