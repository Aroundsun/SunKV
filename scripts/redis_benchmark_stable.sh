#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

SERVER_BIN="${SERVER_BIN:-${ROOT_DIR}/build/sunkv}"
CLIENT_BIN="${CLIENT_BIN:-${ROOT_DIR}/build/sunkvClient}"
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6381}"
STARTUP_WAIT_SECONDS="${STARTUP_WAIT_SECONDS:-15}"

# 默认参数：尽量稳定、可复现
NORMAL_N="${NORMAL_N:-30000}"
NORMAL_C="${NORMAL_C:-50}"
PIPE_N="${PIPE_N:-30000}"
PIPE_C="${PIPE_C:-50}"
PIPE_P="${PIPE_P:-16}"
STALL_ZERO_STREAK="${STALL_ZERO_STREAK:-8}"

LOG_DIR="${LOG_DIR:-${ROOT_DIR}/data/logs}"
mkdir -p "${LOG_DIR}"
TS="$(date +%Y%m%d_%H%M%S)"
SERVER_LOG="${LOG_DIR}/redis_bench_server_${TS}.log"
BENCH_LOG="${LOG_DIR}/redis_bench_result_${TS}.log"
SUMMARY_MD="${LOG_DIR}/redis_bench_summary_${TS}.md"
SS_LOG="${LOG_DIR}/redis_bench_ss_${TS}.log"
GDB_LOG="${LOG_DIR}/redis_bench_gdb_${TS}.log"

SERVER_PID=""
HAS_FAILURE=0
FAIL_REASONS=()

cleanup() {
  if [[ -n "${SERVER_PID}" ]]; then
    kill -TERM "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
    SERVER_PID=""
  fi
}
trap cleanup EXIT

require_bin() {
  local b="$1"
  if ! command -v "${b}" >/dev/null 2>&1; then
    echo "ERROR: missing binary '${b}'"
    exit 1
  fi
}

require_bin redis-benchmark

if [[ ! -x "${SERVER_BIN}" ]]; then
  echo "ERROR: server binary not found or not executable: ${SERVER_BIN}"
  exit 1
fi

if [[ ! -x "${CLIENT_BIN}" ]]; then
  echo "ERROR: client binary not found or not executable: ${CLIENT_BIN}"
  exit 1
fi

echo "== SunKV stable benchmark ==" | tee "${BENCH_LOG}"
echo "server=${SERVER_BIN} host=${HOST} port=${PORT}" | tee -a "${BENCH_LOG}"
echo "normal: n=${NORMAL_N}, c=${NORMAL_C}" | tee -a "${BENCH_LOG}"
echo "pipeline: n=${PIPE_N}, c=${PIPE_C}, p=${PIPE_P}" | tee -a "${BENCH_LOG}"
echo "stall rule: max consecutive 'rps=0.0' < ${STALL_ZERO_STREAK}" | tee -a "${BENCH_LOG}"
echo "startup wait timeout: ${STARTUP_WAIT_SECONDS}s" | tee -a "${BENCH_LOG}"
echo "server_log=${SERVER_LOG}" | tee -a "${BENCH_LOG}"
echo "summary=${SUMMARY_MD}" | tee -a "${BENCH_LOG}"

echo "[1/5] start server" | tee -a "${BENCH_LOG}"
"${SERVER_BIN}" --port "${PORT}" --log-level INFO --enable-console-log false --log-file "${SERVER_LOG}" &
SERVER_PID=$!
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
  echo "ERROR: server failed to start" | tee -a "${BENCH_LOG}"
  exit 1
fi

echo "[2/5] health check with sunkvClient" | tee -a "${BENCH_LOG}"
PING_OUT=""
ok=0
for ((i=1; i<=STARTUP_WAIT_SECONDS; i++)); do
  PING_OUT="$("${CLIENT_BIN}" "${HOST}" "${PORT}" PING 2>&1 || true)"
  if [[ "${PING_OUT}" == *"PONG"* ]]; then
    ok=1
    break
  fi
  sleep 1
done
echo "PING_OUT=${PING_OUT}" | tee -a "${BENCH_LOG}"
if [[ "${ok}" -ne 1 ]]; then
  echo "ERROR: health check failed, expected PONG" | tee -a "${BENCH_LOG}"
  exit 1
fi

run_case() {
  local name="$1"
  shift
  echo "[case] ${name}" | tee -a "${BENCH_LOG}"
  local out
  out="$(redis-benchmark "$@" 2>&1 || true)"
  printf "%s\n" "${out}" | tee -a "${BENCH_LOG}" >/dev/null
  local summary
  summary="$(printf "%s\n" "${out}" | rg "requests per second|p50=" -N -m 1 || true)"
  if [[ -z "${summary}" ]]; then
    summary="(no summary line captured)"
  fi
  echo "[case-summary] ${name}: ${summary}" | tee -a "${BENCH_LOG}"
  detect_zero_stall "${name}" "${out}"
}

record_failure() {
  local reason="$1"
  HAS_FAILURE=1
  FAIL_REASONS+=("${reason}")
  echo "[FAIL] ${reason}" | tee -a "${BENCH_LOG}"
}

detect_zero_stall() {
  local case_name="$1"
  local out="$2"
  local max_streak
  max_streak="$(printf "%s\n" "${out}" | awk '
    BEGIN {cur=0; max=0}
    /rps=0\.0/ {cur++; if (cur>max) max=cur; next}
    {cur=0}
    END {print max}
  ')"
  echo "[case-metric] ${case_name}: max_zero_streak=${max_streak}" | tee -a "${BENCH_LOG}"
  if [[ "${max_streak}" -ge "${STALL_ZERO_STREAK}" ]]; then
    record_failure "${case_name} 出现连续 ${max_streak} 次 rps=0.0 (阈值=${STALL_ZERO_STREAK})"
    collect_diagnostics "${case_name}"
  fi
}

collect_diagnostics() {
  local case_name="$1"
  echo "[diag] collect diagnostics for ${case_name}" | tee -a "${BENCH_LOG}"
  {
    echo "== snapshot ts=$(date +%F' '%T) case=${case_name} =="
    echo "-- ss -tin '( sport = :${PORT} or dport = :${PORT} )' --"
    if command -v ss >/dev/null 2>&1; then
      ss -tin "( sport = :${PORT} or dport = :${PORT} )" || true
    else
      echo "ss not found"
    fi
    echo
  } >> "${SS_LOG}" 2>&1

  if command -v gdb >/dev/null 2>&1 && [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    {
      echo "== gdb thread backtrace ts=$(date +%F' '%T) case=${case_name} pid=${SERVER_PID} =="
      gdb -q -n -batch \
        -ex "set pagination off" \
        -ex "thread apply all bt" \
        -p "${SERVER_PID}" || true
      echo
    } >> "${GDB_LOG}" 2>&1
  else
    echo "[diag] gdb unavailable or server already exited, skip backtrace" | tee -a "${BENCH_LOG}"
  fi
}

echo "[3/5] benchmark normal path" | tee -a "${BENCH_LOG}"
run_case "SET normal" -h "${HOST}" -p "${PORT}" -t set -n "${NORMAL_N}" -c "${NORMAL_C}" -q
run_case "GET normal" -h "${HOST}" -p "${PORT}" -t get -n "${NORMAL_N}" -c "${NORMAL_C}" -q

echo "[4/5] benchmark pipeline path" | tee -a "${BENCH_LOG}"
run_case "SET pipeline" -h "${HOST}" -p "${PORT}" -t set -n "${PIPE_N}" -c "${PIPE_C}" -P "${PIPE_P}" -q
run_case "GET pipeline" -h "${HOST}" -p "${PORT}" -t get -n "${PIPE_N}" -c "${PIPE_C}" -P "${PIPE_P}" -q

echo "[5/5] generate markdown summary" | tee -a "${BENCH_LOG}"
{
  echo "# SunKV 压测摘要 (${TS})"
  echo
  echo "- host: \`${HOST}\`"
  echo "- port: \`${PORT}\`"
  echo "- server bin: \`${SERVER_BIN}\`"
  echo "- client bin: \`${CLIENT_BIN}\`"
  echo "- server log: \`${SERVER_LOG}\`"
  echo "- raw bench log: \`${BENCH_LOG}\`"
  echo "- ss snapshot log: \`${SS_LOG}\`"
  echo "- gdb backtrace log: \`${GDB_LOG}\`"
  echo
  echo "## 关键结果"
  rg "\\[case-summary\\]" "${BENCH_LOG}" | sed 's/^/- /'
  rg "\\[case-metric\\]" "${BENCH_LOG}" | sed 's/^/- /'
  echo
  echo "## 自动判定"
  if [[ "${HAS_FAILURE}" -eq 0 ]]; then
    echo "- PASS: 未触发 stall 规则。"
  else
    echo "- FAIL: 触发 stall 规则。"
    for r in "${FAIL_REASONS[@]}"; do
      echo "- ${r}"
    done
  fi
  echo
  echo "## 说明"
  echo "- 本脚本仅使用 RESP 命令路径（SET/GET），避免 PING_INLINE 干扰。"
  echo "- 若出现 \`Could not fetch server CONFIG\`，通常是兼容性提示，不影响核心命令压测。"
} > "${SUMMARY_MD}"

echo "DONE: ${SUMMARY_MD}" | tee -a "${BENCH_LOG}"
if [[ "${HAS_FAILURE}" -ne 0 ]]; then
  exit 2
fi
