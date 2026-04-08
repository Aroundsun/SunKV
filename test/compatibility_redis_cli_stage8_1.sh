#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_BIN="${ROOT_DIR}/build/sunkv"
PORT="${1:-6392}"
HOST="127.0.0.1"
SERVER_LOG="${ROOT_DIR}/data/logs/stage8_1_compat_server.log"

if [[ ! -x "${SERVER_BIN}" ]]; then
  echo "ERROR: server binary not found: ${SERVER_BIN}"
  exit 1
fi

if ! command -v redis-cli >/dev/null 2>&1; then
  echo "ERROR: redis-cli not found"
  exit 1
fi

mkdir -p "${ROOT_DIR}/data/logs"
"${SERVER_BIN}" --port "${PORT}" >"${SERVER_LOG}" 2>&1 &
SERVER_PID=$!

cleanup() {
  if kill -0 "${SERVER_PID}" 2>/dev/null; then
    kill -TERM "${SERVER_PID}" || true
    wait "${SERVER_PID}" || true
  fi
}
trap cleanup EXIT

sleep 1
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
  echo "ERROR: server failed to start, log: ${SERVER_LOG}"
  exit 1
fi

redis_call() {
  redis-cli -h "${HOST}" -p "${PORT}" --raw "$@"
}

assert_eq() {
  local actual="$1"
  local expected="$2"
  local name="$3"
  if [[ "${actual}" != "${expected}" ]]; then
    echo "FAIL: ${name}"
    echo "expected: ${expected}"
    echo "actual  : ${actual}"
    exit 1
  fi
  echo "PASS: ${name}"
}

echo "INFO: running redis-cli compatibility checks ..."

assert_eq "$(redis_call PING)" "PONG" "PING compatibility"
assert_eq "$(redis_call SET compat:key compat:value)" "OK" "SET compatibility"
assert_eq "$(redis_call GET compat:key)" "compat:value" "GET compatibility"
assert_eq "$(redis_call DEL compat:key)" "1" "DEL compatibility"
assert_eq "$(redis_call GET compat:key)" "" "GET nil compatibility"
assert_eq "$(redis_call UNKNOWN_CMD hello | tr -d '\r')" "ERR unknown command" "unknown command compatibility"

echo "INFO: redis-cli compatibility checks passed."
