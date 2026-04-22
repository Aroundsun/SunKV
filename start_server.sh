#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<'EOF'
用法:
  ./start_server.sh [PORT]

说明:
  1) 清理 build/
  2) 以 Release 重新构建 sunkv
  3) 将可执行文件复制到主目录 ./sunkv
  4) 启动服务端（默认端口 6379）
EOF
  exit 0
fi

PORT="${1:-6379}"

rm -rf "${BUILD_DIR}"


cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release


cmake --build "${BUILD_DIR}" -j"$(nproc)" --target sunkv

cp "${BUILD_DIR}/sunkv" "${ROOT_DIR}/sunkv"
exec "${ROOT_DIR}/sunkv" --host 127.0.0.1 --port "${PORT}"

