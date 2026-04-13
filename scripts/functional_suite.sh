#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

PORT="${PORT:-6379}"
HOST="${HOST:-127.0.0.1}"

SERVER_BIN="${ROOT_DIR}/build/sunkv"
LOG_DIR="${ROOT_DIR}/data/logs"
mkdir -p "${LOG_DIR}"

TS="$(date +%Y%m%d_%H%M%S)"
OUT_LOG="${LOG_DIR}/functional_suite_${TS}.log"
SERVER_LOG="${LOG_DIR}/functional_suite_server_${TS}.log"

cleanup() {
  pkill -TERM -x sunkv 2>/dev/null || true
  sleep 1
  pkill -9 -x sunkv 2>/dev/null || true
}
trap cleanup EXIT

echo "[1/4] start server" | tee -a "${OUT_LOG}"
"${SERVER_BIN}" --port "${PORT}" --log-level INFO --enable-console-log false --log-strategy fixed --log-file "${SERVER_LOG}" &
sleep 2

echo "[2/4] basic commands" | tee -a "${OUT_LOG}"
redis-cli -h "${HOST}" -p "${PORT}" PING | tee -a "${OUT_LOG}"
redis-cli -h "${HOST}" -p "${PORT}" SET key1 value1 | tee -a "${OUT_LOG}"
redis-cli -h "${HOST}" -p "${PORT}" GET key1 | tee -a "${OUT_LOG}"
redis-cli -h "${HOST}" -p "${PORT}" DEL key1 | tee -a "${OUT_LOG}"

echo "[3/4] boundary value" | tee -a "${OUT_LOG}"
python3 - <<'PY' | redis-cli -h "${HOST}" -p "${PORT}" -x SET long_value_key >>"${OUT_LOG}" 2>&1
print("x" * 8192)
PY
redis-cli -h "${HOST}" -p "${PORT}" GET long_value_key >>"${OUT_LOG}" 2>&1 || true

echo "[4/4] done" | tee -a "${OUT_LOG}"
echo "OK" | tee -a "${OUT_LOG}"

