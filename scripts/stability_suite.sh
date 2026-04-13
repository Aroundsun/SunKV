#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

SERVER_BIN="${ROOT_DIR}/build/sunkv"
LOG_DIR="${ROOT_DIR}/data/logs"
mkdir -p "${LOG_DIR}"

PORT="${PORT:-6395}"
HOST="${HOST:-127.0.0.1}"
THREAD_POOL_SIZE="${THREAD_POOL_SIZE:-4}"
MAX_CONNECTIONS="${MAX_CONNECTIONS:-4000}"

LONG_RUN_SECONDS="${LONG_RUN_SECONDS:-120}"
SAMPLE_INTERVAL_SECONDS="${SAMPLE_INTERVAL_SECONDS:-2}"
STRESS_SECONDS="${STRESS_SECONDS:-30}"
STRESS_CONCURRENCY="${STRESS_CONCURRENCY:-200}"
STRESS_PIPELINE="${STRESS_PIPELINE:-1}"
RECOVERY_MODE="${RECOVERY_MODE:-snapshot}"  # snapshot | wal

TS="$(date +%Y%m%d_%H%M%S)"
REPORT_FILE="${LOG_DIR}/stage8_3_stability_report_${TS}.txt"
LONG_LOG="${LOG_DIR}/stage8_3_longrun_${TS}.log"
RSS_LOG="${LOG_DIR}/stage8_3_rss_${TS}.csv"
STRESS_LOG="${LOG_DIR}/stage8_3_stress_${TS}.log"
RECOVERY_LOG="${LOG_DIR}/stage8_3_recovery_${TS}.log"

SERVER_PID=""

start_server() {
  "${SERVER_BIN}" --port "${PORT}" --thread-pool-size "${THREAD_POOL_SIZE}" --max-connections "${MAX_CONNECTIONS}" >"${LONG_LOG}" 2>&1 &
  SERVER_PID=$!
  sleep 2
  if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "ERROR: server failed to start" | tee -a "${REPORT_FILE}"
    exit 1
  fi
}

stop_server() {
  if [[ -n "${SERVER_PID}" ]]; then
    kill -TERM "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
    SERVER_PID=""
  fi
}

cleanup() {
  stop_server
}
trap cleanup EXIT

echo "== SunKV stability suite ==" | tee "${REPORT_FILE}"
echo "PORT=${PORT} THREAD_POOL_SIZE=${THREAD_POOL_SIZE} MAX_CONNECTIONS=${MAX_CONNECTIONS}" | tee -a "${REPORT_FILE}"
echo "LONG_RUN_SECONDS=${LONG_RUN_SECONDS} SAMPLE_INTERVAL_SECONDS=${SAMPLE_INTERVAL_SECONDS}" | tee -a "${REPORT_FILE}"
echo "STRESS_SECONDS=${STRESS_SECONDS} STRESS_CONCURRENCY=${STRESS_CONCURRENCY} STRESS_PIPELINE=${STRESS_PIPELINE}" | tee -a "${REPORT_FILE}"
echo "RECOVERY_MODE=${RECOVERY_MODE}" | tee -a "${REPORT_FILE}"

echo "[1/4] start server" | tee -a "${REPORT_FILE}"
start_server

echo "[2/4] long run + rss sampling" | tee -a "${REPORT_FILE}"
echo "ts,rss_kb" > "${RSS_LOG}"
end_ts=$(( $(date +%s) + LONG_RUN_SECONDS ))
while [[ $(date +%s) -lt ${end_ts} ]]; do
  rss_kb="$(ps -o rss= -p "${SERVER_PID}" | tr -d ' ' || echo 0)"
  echo "$(date +%s),${rss_kb}" >> "${RSS_LOG}"
  sleep "${SAMPLE_INTERVAL_SECONDS}"
done

echo "[3/4] stress (redis-benchmark)" | tee -a "${REPORT_FILE}"
if command -v redis-benchmark >/dev/null 2>&1; then
  timeout "${STRESS_SECONDS}s" redis-benchmark -h "${HOST}" -p "${PORT}" -n 100000000 \
    -c "${STRESS_CONCURRENCY}" -P "${STRESS_PIPELINE}" -t set,get -q \
    > "${STRESS_LOG}" 2>&1 || true
else
  echo "WARN: redis-benchmark not found, skip stress" | tee -a "${REPORT_FILE}"
fi

echo "[4/4] recovery check (crash + restart)" | tee -a "${REPORT_FILE}"
kill -9 "${SERVER_PID}" 2>/dev/null || true
wait "${SERVER_PID}" 2>/dev/null || true
SERVER_PID=""

start_server
if command -v redis-cli >/dev/null 2>&1; then
  redis-cli -h "${HOST}" -p "${PORT}" PING > "${RECOVERY_LOG}" 2>&1 || true
fi
stop_server

echo "DONE. report=${REPORT_FILE}" | tee -a "${REPORT_FILE}"

