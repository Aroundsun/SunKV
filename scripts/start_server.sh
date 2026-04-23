#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/server"
BIN_PATH="${BUILD_DIR}/bin/sunkv"

# 默认服务器配置文件路径
CONFIG_PATH="${ROOT_DIR}/sunkv.conf"
# 默认构建模式
BUILD_MODE="Release"
# 是否配置
DO_CONFIGURE="1"  
# 是否构建
DO_BUILD="1"

# 帮助信息
usage() {
  cat <<'EOF'
Usage:
  scripts/start_server.sh [options] [-- <extra server args>]

Options:
  -h, --help                 Show help
  --config <path>            Server config path (default: ./sunkv.conf)
  --build-mode <mode>        CMake build mode (default: Release)
  --no-configure             Skip cmake configure
  --no-build                 Skip cmake build

Examples:
  scripts/start_server.sh
  scripts/start_server.sh --config ./sunkv.conf
  scripts/start_server.sh --no-build
EOF
}
# 解析参数
SERVER_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --config)
      CONFIG_PATH="${2:-}"
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
      SERVER_ARGS+=("$@")
      break
      ;;
    *)
      SERVER_ARGS+=("$1")
      shift
      ;;
  esac
done

if [[ -z "${CONFIG_PATH}" ]]; then
  echo "[start_server] config path is empty"
  exit 1
fi
# 检查配置文件是否存在
if [[ ! -f "${CONFIG_PATH}" ]]; then
  echo "[start_server] config file not found: ${CONFIG_PATH}"
  exit 1
fi

# 创建构建目录
mkdir -p "${BUILD_DIR}"

if [[ "${DO_CONFIGURE}" == "1" ]]; then
  echo "[start_server] configuring (${BUILD_MODE}) ..."
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_MODE}"
fi

# 构建
if [[ "${DO_BUILD}" == "1" ]]; then
  echo "[start_server] building sunkv ..."
  cmake --build "${BUILD_DIR}" --target sunkv -j
fi

# 检查二进制文件是否存在
if [[ ! -x "${BIN_PATH}" ]]; then
  echo "[start_server] binary not found: ${BIN_PATH}"
  exit 1
fi

# 构建服务器启动参数
RUN_ARGS=(--config "${CONFIG_PATH}")
RUN_ARGS+=("${SERVER_ARGS[@]}")

# 启动服务器
echo "[start_server] starting: ${BIN_PATH} ${RUN_ARGS[*]-}"
exec "${BIN_PATH}" "${RUN_ARGS[@]}"
