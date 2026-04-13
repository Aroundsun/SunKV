#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

# 允许测试/客户端使用 stdout/stderr；运行期代码需统一走 Logger/LOG_*
TARGET_DIRS=(
  "server"
  "network"
  "storage"
  "common"
  "protocol"
  "command"
)

PATTERN='std::cout|std::cerr|\bperror\(|\bprintf\('

set +e
matches="$(rg -n --no-heading -S "${PATTERN}" "${TARGET_DIRS[@]}" 2>/dev/null)"
rc=$?
set -e

if [[ $rc -eq 0 && -n "${matches}" ]]; then
  echo "ERROR: runtime code uses stdout/stderr directly (double-track logging)."
  echo "Please use LOG_DEBUG/INFO/WARN/ERROR instead."
  echo
  echo "${matches}"
  exit 1
fi

echo "OK: no stdio logging in runtime dirs."

