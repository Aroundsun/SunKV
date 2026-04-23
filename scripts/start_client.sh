#!/usr/bin/env bash

set -euo pipefail # 严格模式，禁止未定义变量，禁止管道错误

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/client"
BIN_PATH="${BUILD_DIR}/bin/sunkvClient"

HOST="127.0.0.1"
PORT="6379"
BUILD_MODE="Release"
DO_CONFIGURE="1"
DO_BUILD="1"

usage() {
  cat <<'EOF'
Usage:
  scripts/start_client.sh [options] [-- <client args>]

Options:
  -h, --help                 Show help
  --host <host>              Server host (default: 127.0.0.1)
  --port <port>              Server port (default: 6379)
  --build-mode <mode>        CMake build mode (default: Release)
  --no-configure             Skip cmake configure
  --no-build                 Skip cmake build

Examples:
  scripts/start_client.sh
  scripts/start_client.sh -- --raw
EOF
}

CLIENT_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --host)
      HOST="${2:-}"
      shift 2
      ;;
    --port)
      PORT="${2:-}"
      shift 2
      ;;
    --build-mode)
      BUILD_MODE="${2:-}"
      shift 2
      ;;
    --no-configure)
      DO_CONFIGURE="0"
      shift
      ;;
    --no-build)
      DO_BUILD="0"
      shift
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
# 检查主机和端口是否为空
if [[ -z "${HOST}" || -z "${PORT}" ]]; then
  echo "[start_client] host/port is empty"
  exit 1
fi
 
# 创建构建目录
mkdir -p "${BUILD_DIR}"
# 配置
if [[ "${DO_CONFIGURE}" == "1" ]]; then
  echo "[start_client] configuring (${BUILD_MODE}) ..."
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_MODE}"
fi
# 构建
if [[ "${DO_BUILD}" == "1" ]]; then
  echo "[start_client] building sunkvClient ..."
  cmake --build "${BUILD_DIR}" --target sunkvClient -j
fi
# 检查二进制文件是否存在
if [[ ! -x "${BIN_PATH}" ]]; then
  echo "[start_client] binary not found: ${BIN_PATH}"
  exit 1
fi

# 启动客户端
echo "[start_client] starting: ${BIN_PATH} --host ${HOST} --port ${PORT} ${CLIENT_ARGS[*]-}"
exec "${BIN_PATH}" --host "${HOST}" --port "${PORT}" "${CLIENT_ARGS[@]}"
