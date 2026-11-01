#!/usr/bin/env bash
# vmon 本地栈管理：一条命令启动/停止/重启/查看状态与日志。
#
# 用法：
#   ./scripts/vmon.sh up        # 启动 Server + Agent
#   ./scripts/vmon.sh status    # 查看进程、健康检查、日志路径
#   ./scripts/vmon.sh logs server -f
#   ./scripts/vmon.sh down      # 优雅停止
#   ./scripts/vmon.sh restart
#   ./scripts/vmon.sh clean     # 停止并删除运行目录（需 FORCE=1）
#
# 常用环境变量：
#   PORT=18428          监听的 HTTP 端口
#   VMON_TOKEN=dev-token 访问令牌
#   VMON_HOME=.vmon-run  运行目录（PID、日志、WAL、状态文件）
#   NODE_NAME=...        节点名，默认主机名
#   PROCESS_FILTER=...   只采集命令行包含该子串的进程，默认全部
#   BUILD_DIR=build      构建产物目录
#
# 设计原则：只操作自己 PID 文件里的进程，不按进程名 pkill。
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD_DIR:-"$ROOT/build"}
SERVER_BIN=${SERVER_BIN:-"$BUILD/vmon_server"}
AGENT_BIN=${AGENT_BIN:-"$BUILD/vmon_agent"}

RUN_DIR=${VMON_HOME:-"$ROOT/.vmon-run"}
PORT=${PORT:-18428}
TOKEN=${VMON_TOKEN:-dev-token}
HOST=${LISTEN_HOST:-127.0.0.1}
NODE=${NODE_NAME:-$(hostname -s 2>/dev/null || hostname)}
UDS=${AGENT_UDS:-"$RUN_DIR/agent.sock"}
CTRL_DIR=${CONTROL_DIR:-"$RUN_DIR/ctrl"}
CRASH_DIR=${CRASH_DIR:-"$RUN_DIR/crash"}
SERVER_STATE="$RUN_DIR/server"
AGENT_STATE="$RUN_DIR/agent"
SERVER_LOG="$RUN_DIR/server.log"
AGENT_LOG="$RUN_DIR/agent.log"
SERVER_PID_FILE="$RUN_DIR/server.pid"
AGENT_PID_FILE="$RUN_DIR/agent.pid"
PROCESS_FILTER=${PROCESS_FILTER:-}
BASE_URL="http://$HOST:$PORT"

log() { printf '[vmon] %s\n' "$*"; }
err() { printf '[vmon] 错误：%s\n' "$*" >&2; }

require_bins() {
  local missing=0
  for bin in "$SERVER_BIN" "$AGENT_BIN"; do
    if [[ ! -x "$bin" ]]; then
      err "找不到可执行文件 $bin"
      missing=1
    fi
  done
  if [[ "$missing" -ne 0 ]]; then
    err "请先构建：cmake -S \"$ROOT\" -B \"$BUILD\" && cmake --build \"$BUILD\" -j"
    exit 1
  fi
}

# 读取 PID 文件，确认进程仍存活，避免 PID 复用误判。
pid_of() {
  local file=$1
  [[ -f "$file" ]] || return 1
  local pid
  pid=$(cat "$file" 2>/dev/null || true)
  [[ -n "$pid" ]] || return 1
  kill -0 "$pid" 2>/dev/null || return 1
  printf '%s' "$pid"
}

wait_http() {
  local i
  for i in $(seq 1 100); do
    if curl -sf --max-time 1 "$BASE_URL/api/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

wait_socket() {
  local i
  for i in $(seq 1 100); do
    [[ -S "$UDS" ]] && return 0
    sleep 0.1
  done
  return 1
}

start_server() {
  if pid=$(pid_of "$SERVER_PID_FILE"); then
    log "Server 已在运行（pid=$pid）"
    return 0
  fi
  mkdir -p "$RUN_DIR" "$SERVER_STATE"
  log "启动 Server：$BASE_URL"
  nohup "$SERVER_BIN" \
    --listen-host "$HOST" --port "$PORT" --token "$TOKEN" \
    --state-dir "$SERVER_STATE" --web-dir "$ROOT/web" \
    --no-forward >"$SERVER_LOG" 2>&1 &
  echo $! >"$SERVER_PID_FILE"
  if ! wait_http; then
    err "Server 未在 10 秒内就绪，最近日志："
    tail -n 20 "$SERVER_LOG" >&2 || true
    return 1
  fi
  log "Server 就绪（pid=$(cat "$SERVER_PID_FILE")，日志 $SERVER_LOG）"
}

start_agent() {
  if pid=$(pid_of "$AGENT_PID_FILE"); then
    log "Agent 已在运行（pid=$pid）"
    return 0
  fi
  mkdir -p "$RUN_DIR" "$AGENT_STATE" "$CTRL_DIR" "$CRASH_DIR"
  local args=(
    --server-url "$BASE_URL"
    --uds "$UDS"
    --state-dir "$AGENT_STATE"
    --node "$NODE"
    --token "$TOKEN"
    --control-dir "$CTRL_DIR"
    --crash-dir "$CRASH_DIR"
    --scan-interval-ms 2000
    --upload-interval-ms 200
  )
  if [[ -n "$PROCESS_FILTER" ]]; then
    args+=(--process-filter "$PROCESS_FILTER")
  fi
  log "启动 Agent：node=$NODE uds=$UDS"
  nohup "$AGENT_BIN" "${args[@]}" >"$AGENT_LOG" 2>&1 &
  echo $! >"$AGENT_PID_FILE"
  if ! wait_socket; then
    err "Agent 未在 10 秒内就绪，最近日志："
    tail -n 20 "$AGENT_LOG" >&2 || true
    return 1
  fi
  log "Agent 就绪（pid=$(cat "$AGENT_PID_FILE")，日志 $AGENT_LOG）"
}

stop_one() {
  local name=$1 file=$2
  local pid
  if ! pid=$(pid_of "$file"); then
    rm -f "$file"
    log "$name 未运行"
    return 0
  fi
  log "停止 $name（pid=$pid）"
  kill "$pid" 2>/dev/null || true
  local i
  for i in $(seq 1 50); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.1
  done
  if kill -0 "$pid" 2>/dev/null; then
    log "$name 未在 5 秒内退出，强制结束"
    kill -9 "$pid" 2>/dev/null || true
  fi
  rm -f "$file"
}

cmd_up() {
  require_bins
  start_server
  start_agent
  cat <<EOF

[vmon] 已启动
  Web UI : $BASE_URL/
  令牌   : $TOKEN
  UDS    : $UDS
  日志   : $SERVER_LOG
           $AGENT_LOG
  停止   : ./scripts/vmon.sh down
EOF
}

cmd_down() {
  stop_one "Agent" "$AGENT_PID_FILE"
  stop_one "Server" "$SERVER_PID_FILE"
}

cmd_restart() {
  cmd_down
  cmd_up
}

cmd_status() {
  local ok=0
  if pid=$(pid_of "$SERVER_PID_FILE"); then
    if curl -sf --max-time 1 "$BASE_URL/api/health" >/dev/null 2>&1; then
      log "Server  : 运行中 pid=$pid，健康检查通过"
    else
      log "Server  : 运行中 pid=$pid，但健康检查失败"
      ok=1
    fi
  else
    log "Server  : 未运行"
    ok=1
  fi
  if pid=$(pid_of "$AGENT_PID_FILE"); then
    if [[ -S "$UDS" ]]; then
      log "Agent   : 运行中 pid=$pid，UDS 就绪"
    else
      log "Agent   : 运行中 pid=$pid，但 UDS 不存在"
      ok=1
    fi
  else
    log "Agent   : 未运行"
    ok=1
  fi
  log "Web UI  : $BASE_URL/（令牌 $TOKEN）"
  log "运行目录: $RUN_DIR"
  return "$ok"
}

cmd_logs() {
  local target=${1:-server}
  local follow=${2:-}
  local file
  case "$target" in
    server) file=$SERVER_LOG ;;
    agent) file=$AGENT_LOG ;;
    *) err "未知日志目标：$target（可选 server / agent）"; exit 2 ;;
  esac
  [[ -f "$file" ]] || { err "日志不存在：$file"; exit 1; }
  if [[ "$follow" == "-f" || "$follow" == "--follow" ]]; then
    tail -n 100 -f "$file"
  else
    tail -n 200 "$file"
  fi
}

cmd_clean() {
  if [[ "${FORCE:-0}" != "1" ]]; then
    err "clean 会删除 $RUN_DIR（含 WAL、证据包、日志）。确认请用：FORCE=1 $0 clean"
    exit 2
  fi
  cmd_down
  rm -rf "$RUN_DIR"
  log "已删除 $RUN_DIR"
}

usage() {
  sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'
}

case "${1:-}" in
  up) cmd_up ;;
  down) cmd_down ;;
  restart) cmd_restart ;;
  status) cmd_status ;;
  logs) shift; cmd_logs "${1:-server}" "${2:-}" ;;
  clean) cmd_clean ;;
  ""|-h|--help|help) usage ;;
  *) err "未知命令：$1"; usage; exit 2 ;;
esac
