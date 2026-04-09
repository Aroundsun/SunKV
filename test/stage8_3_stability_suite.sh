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
  if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    kill -TERM "${SERVER_PID}" || true
    wait "${SERVER_PID}" || true
  fi
  SERVER_PID=""
}

cleanup() {
  stop_server
  pkill -f "${SERVER_BIN}" 2>/dev/null || true
}
trap cleanup EXIT

if [[ ! -x "${SERVER_BIN}" ]]; then
  echo "ERROR: missing server binary: ${SERVER_BIN}"
  exit 1
fi

if ! command -v redis-benchmark >/dev/null 2>&1; then
  echo "ERROR: redis-benchmark not found"
  exit 1
fi
if ! command -v redis-cli >/dev/null 2>&1; then
  echo "ERROR: redis-cli not found"
  exit 1
fi

echo "# 8.3 稳定性测试报告" > "${REPORT_FILE}"
echo "time=${TS}" >> "${REPORT_FILE}"
echo "server=${SERVER_BIN}" >> "${REPORT_FILE}"
echo "port=${PORT}" >> "${REPORT_FILE}"
echo "recovery_mode=${RECOVERY_MODE}" >> "${REPORT_FILE}"
echo >> "${REPORT_FILE}"

echo "[8.3-1] 长时间运行测试（${LONG_RUN_SECONDS}s）..."
start_server
echo "sec,rss_kb,vm_size_kb,threads" > "${RSS_LOG}"
for ((sec=0; sec<=LONG_RUN_SECONDS; sec+=SAMPLE_INTERVAL_SECONDS)); do
  if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "ERROR: server exited during long-run" | tee -a "${REPORT_FILE}"
    exit 1
  fi
  rss_kb="$(awk '/VmRSS/ {print $2}' "/proc/${SERVER_PID}/status" 2>/dev/null || echo 0)"
  vm_kb="$(awk '/VmSize/ {print $2}' "/proc/${SERVER_PID}/status" 2>/dev/null || echo 0)"
  th_cnt="$(awk '/Threads/ {print $2}' "/proc/${SERVER_PID}/status" 2>/dev/null || echo 0)"
  echo "${sec},${rss_kb},${vm_kb},${th_cnt}" >> "${RSS_LOG}"
  sleep "${SAMPLE_INTERVAL_SECONDS}"
done
echo "- 长跑完成，RSS 采样文件: ${RSS_LOG}" >> "${REPORT_FILE}"
stop_server

echo "[8.3-2] 内存泄漏检测（基于 RSS 趋势）..."
rss_first="$(awk -F',' 'NR==2 {print $2}' "${RSS_LOG}")"
rss_last="$(awk -F',' 'END {print $2}' "${RSS_LOG}")"
rss_delta=$((rss_last - rss_first))
echo "- RSS first=${rss_first}KB last=${rss_last}KB delta=${rss_delta}KB" | tee -a "${REPORT_FILE}"

echo "[8.3-3] 崩溃恢复测试（SIGKILL + 重启验证）..."
start_server
test_key="stage8_3_recover_key_${TS}"
test_val="stage8_3_recover_value_${TS}"
redis-cli -h "${HOST}" -p "${PORT}" SET "${test_key}" "${test_val}" >/dev/null
if [[ "${RECOVERY_MODE}" == "snapshot" ]]; then
  redis-cli -h "${HOST}" -p "${PORT}" SNAPSHOT >/dev/null || true
  sleep 1
else
  # WAL 模式下给后台写盘线程一个短暂窗口
  sleep 1
fi
kill -KILL "${SERVER_PID}" || true
wait "${SERVER_PID}" || true
SERVER_PID=""
sleep 1
start_server
recovered="$(redis-cli -h "${HOST}" -p "${PORT}" GET "${test_key}" || true)"
echo "recover_key=${test_key}" > "${RECOVERY_LOG}"
echo "recover_expected=${test_val}" >> "${RECOVERY_LOG}"
echo "recover_actual=${recovered}" >> "${RECOVERY_LOG}"
if [[ "${recovered}" == "${test_val}" ]]; then
  echo "- 崩溃恢复: PASS" | tee -a "${REPORT_FILE}"
else
  echo "- 崩溃恢复: FAIL (actual=${recovered})" | tee -a "${REPORT_FILE}"
fi
stop_server

echo "[8.3-4] 压力测试（${STRESS_SECONDS}s, c=${STRESS_CONCURRENCY}, P=${STRESS_PIPELINE}）..."
start_server
timeout "${STRESS_SECONDS}s" redis-benchmark -h "${HOST}" -p "${PORT}" -n 100000000 \
  -c "${STRESS_CONCURRENCY}" -P "${STRESS_PIPELINE}" -t set,get -q > "${STRESS_LOG}" 2>&1 || true
echo "- 压力测试日志: ${STRESS_LOG}" >> "${REPORT_FILE}"
stop_server

echo >> "${REPORT_FILE}"
echo "## 摘要" >> "${REPORT_FILE}"
echo "- report: ${REPORT_FILE}" >> "${REPORT_FILE}"
echo "- longrun_log: ${LONG_LOG}" >> "${REPORT_FILE}"
echo "- rss_log: ${RSS_LOG}" >> "${REPORT_FILE}"
echo "- recovery_log: ${RECOVERY_LOG}" >> "${REPORT_FILE}"
echo "- stress_log: ${STRESS_LOG}" >> "${REPORT_FILE}"

echo "完成。报告文件: ${REPORT_FILE}"
