#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_BIN="${ROOT_DIR}/build/sunkv"
CLIENT_BIN="${ROOT_DIR}/build/simple_client"
PORT="${1:-6391}"
HOST="127.0.0.1"

if [[ ! -x "${SERVER_BIN}" ]]; then
  echo "ERROR: server binary not found: ${SERVER_BIN}"
  exit 1
fi

if [[ ! -x "${CLIENT_BIN}" ]]; then
  echo "INFO: building simple_client ..."
  g++ -std=c++17 -O2 "${ROOT_DIR}/client/simple_client.cpp" -o "${CLIENT_BIN}"
fi

SERVER_LOG="${ROOT_DIR}/data/logs/stage8_1_test_server.log"
mkdir -p "${ROOT_DIR}/data/logs"

echo "INFO: starting server on ${HOST}:${PORT}"
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

run_cmd() {
  "${CLIENT_BIN}" "${HOST}" "${PORT}" "$@"
}

assert_contains() {
  local output="$1"
  local expected="$2"
  local title="$3"
  if [[ "${output}" != *"${expected}"* ]]; then
    echo "FAIL: ${title}"
    echo "Expected contains: ${expected}"
    echo "Actual output:"
    echo "${output}"
    exit 1
  fi
  echo "PASS: ${title}"
}

echo "INFO: running stage 8.1 functional checks ..."

out="$(run_cmd PING)"
assert_contains "${out}" "+PONG" "PING"

out="$(run_cmd SET stage8_key stage8_value)"
assert_contains "${out}" "+OK" "SET"

out="$(run_cmd GET stage8_key)"
assert_contains "${out}" "stage8_value" "GET existing key"

out="$(run_cmd DEL stage8_key)"
assert_contains "${out}" ":1" "DEL existing key"

out="$(run_cmd GET stage8_key)"
assert_contains "${out}" '$-1' "GET deleted key should be nil"

# 边界测试：较长 value
long_value="$(python3 - <<'PY'
print("v" * 8192)
PY
)"
out="$(run_cmd SET stage8_long_key "${long_value}")"
assert_contains "${out}" "+OK" "SET long value"
out="$(run_cmd GET stage8_long_key)"
assert_contains "${out}" '$8192' "GET long value length check"

# 异常测试：参数不足
out="$(run_cmd SET only_key)"
assert_contains "${out}" "Response: -" "SET missing argument should fail"
out="$(run_cmd GET)"
assert_contains "${out}" "Response: -" "GET missing argument should fail"

# 异常测试：未知命令
out="$(run_cmd UNKNOWN_CMD any)"
assert_contains "${out}" "Response: -" "Unknown command should fail"

echo "INFO: stage 8.1 functional suite passed."
