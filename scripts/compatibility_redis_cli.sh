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
OUT_LOG="${LOG_DIR}/redis_cli_compat_${TS}.log"
SERVER_LOG="${LOG_DIR}/redis_cli_compat_server_${TS}.log"

cleanup() {
  pkill -TERM -x sunkv 2>/dev/null || true
  sleep 1
  pkill -9 -x sunkv 2>/dev/null || true
}
trap cleanup EXIT

if ! command -v redis-cli >/dev/null 2>&1; then
  echo "ERROR: redis-cli not found" | tee -a "${OUT_LOG}"
  exit 1
fi

echo "[1/3] start server" | tee -a "${OUT_LOG}"
"${SERVER_BIN}" --port "${PORT}" --log-level INFO --enable-console-log false --log-strategy fixed --log-file "${SERVER_LOG}" &
sleep 2

echo "[2/3] redis-cli basic compatibility" | tee -a "${OUT_LOG}"
redis-cli -h "${HOST}" -p "${PORT}" PING | tee -a "${OUT_LOG}"
redis-cli -h "${HOST}" -p "${PORT}" SET compat_key compat_val | tee -a "${OUT_LOG}"
redis-cli -h "${HOST}" -p "${PORT}" GET compat_key | tee -a "${OUT_LOG}"
redis-cli -h "${HOST}" -p "${PORT}" DEL compat_key | tee -a "${OUT_LOG}"
redis-cli -h "${HOST}" -p "${PORT}" GET compat_key | tee -a "${OUT_LOG}" || true

echo "[3/3] unknown command check" | tee -a "${OUT_LOG}"
redis-cli -h "${HOST}" -p "${PORT}" DOES_NOT_EXIST 2>&1 | tee -a "${OUT_LOG}" || true

echo "OK" | tee -a "${OUT_LOG}"

