#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6379}"

usage() {
  cat <<'EOF'
用法:
  scripts/start_client.sh [--host HOST] [--port PORT] [--build-dir DIR] [--] [CLIENT_ARGS...]

说明:
  - 默认二进制: <build-dir>/sunkvClient
  - 不带 CLIENT_ARGS 时进入交互模式
  - 带 CLIENT_ARGS 时执行单次命令

示例:
  scripts/start_client.sh
  scripts/start_client.sh -- PING
  scripts/start_client.sh --port 6380 -- SET mykey myvalue
EOF
}

CLIENT_ARGS=()
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
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      CLIENT_ARGS+=("$@")
      break
      ;;
    *)
      CLIENT_ARGS+=("$1")
      shift
      ;;
  esac
done

CLIENT_BIN="${BUILD_DIR}/sunkvClient"
if [[ ! -x "${CLIENT_BIN}" ]]; then
  echo "错误: 找不到可执行文件 ${CLIENT_BIN}" >&2
  echo "请先执行: cmake --build \"${BUILD_DIR}\" -j --target sunkvClient" >&2
  exit 1
fi

CMD=("${CLIENT_BIN}" "${HOST}" "${PORT}")
CMD+=("${CLIENT_ARGS[@]}")

echo "[start_client] host=${HOST} port=${PORT}"
echo "[start_client] exec: ${CMD[*]}"
exec "${CMD[@]}"

