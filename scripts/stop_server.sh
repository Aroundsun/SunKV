#!/usr/bin/env bash
set -euo pipefail
# 获取所有sunkv进程
PIDS="$(pgrep -f '(^|/)sunkv($| )' || true)"

# 
if [[ -z "${PIDS}" ]]; then
  echo "[stop_server] sunkv is not running"
  exit 0
fi
# 停止sunkv进程
echo "[stop_server] stopping: ${PIDS}"
kill ${PIDS}

# 最多等 5 秒优雅退出
for _ in {1..5}; do
  sleep 1
  if ! pgrep -f '(^|/)sunkv($| )' >/dev/null 2>&1; then
    echo "[stop_server] stopped"
    exit 0
  fi
done

# 强制杀死进程
PIDS_LEFT="$(pgrep -f '(^|/)sunkv($| )' || true)"
if [[ -n "${PIDS_LEFT}" ]]; then
  echo "[stop_server] force kill: ${PIDS_LEFT}"
  kill -9 ${PIDS_LEFT}
fi

echo "[stop_server] stopped"