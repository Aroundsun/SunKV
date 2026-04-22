#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6379}"
RUNTIME_DIR="${RUNTIME_DIR:-${ROOT_DIR}/tmp/runtime/server_${PORT}}"

usage() {
  cat <<'EOF'
用法:
  scripts/start_server.sh [--host HOST] [--port PORT] [--build-dir DIR] [--runtime-dir DIR] [--] [额外服务端参数...]

说明:
  - 默认二进制: <build-dir>/sunkv
  - 自动创建 data/logs/wal/snapshot 目录
  - 未显式传日志参数时，默认写到 runtime 目录

示例:
  scripts/start_server.sh
  scripts/start_server.sh --port 6380 -- --thread-pool-size 8 --max-connections 4000
EOF
}

EXTRA_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --host)
      HOST="$2"
      shift 2
      ;;
    --port)
      PORT="$2"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --runtime-dir)
      RUNTIME_DIR="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      EXTRA_ARGS+=("$@")
      break
      ;;
    *)
      EXTRA_ARGS+=("$1")
      shift
      ;;
  esac
done

SERVER_BIN="${BUILD_DIR}/sunkv"
if [[ ! -x "${SERVER_BIN}" ]]; then
  echo "错误: 找不到可执行文件 ${SERVER_BIN}" >&2
  echo "请先执行: cmake --build \"${BUILD_DIR}\" -j" >&2
  exit 1
fi

DATA_DIR="${RUNTIME_DIR}/data"
LOG_DIR="${DATA_DIR}/logs"
WAL_DIR="${DATA_DIR}/wal"
SNAPSHOT_DIR="${DATA_DIR}/snapshot"
LOG_FILE="${LOG_DIR}/sunkv.log"

mkdir -p "${LOG_DIR}" "${WAL_DIR}" "${SNAPSHOT_DIR}"

HAS_DATA_DIR=0
HAS_WAL_DIR=0
HAS_SNAPSHOT_DIR=0
HAS_LOG_FILE=0

for arg in "${EXTRA_ARGS[@]}"; do
  case "${arg}" in
    --data-dir) HAS_DATA_DIR=1 ;;
    --wal-dir) HAS_WAL_DIR=1 ;;
    --snapshot-dir) HAS_SNAPSHOT_DIR=1 ;;
    --log-file) HAS_LOG_FILE=1 ;;
  esac
done

CMD=("${SERVER_BIN}" "--host" "${HOST}" "--port" "${PORT}")
if [[ ${HAS_DATA_DIR} -eq 0 ]]; then
  CMD+=("--data-dir" "${DATA_DIR}")
fi
if [[ ${HAS_WAL_DIR} -eq 0 ]]; then
  CMD+=("--wal-dir" "${WAL_DIR}")
fi
if [[ ${HAS_SNAPSHOT_DIR} -eq 0 ]]; then
  CMD+=("--snapshot-dir" "${SNAPSHOT_DIR}")
fi
if [[ ${HAS_LOG_FILE} -eq 0 ]]; then
  CMD+=("--log-file" "${LOG_FILE}")
fi
CMD+=("${EXTRA_ARGS[@]}")

echo "[start_server] host=${HOST} port=${PORT}"
echo "[start_server] runtime=${RUNTIME_DIR}"
echo "[start_server] exec: ${CMD[*]}"
exec "${CMD[@]}"

