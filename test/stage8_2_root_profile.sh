#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="/home/xhy/mycode/SunKV"
cd "${PROJECT_ROOT}"

LOG_DIR="${PROJECT_ROOT}/data/logs"
mkdir -p "${LOG_DIR}"

PORT="${PORT:-6379}"
THREAD_POOL_SIZE="${THREAD_POOL_SIZE:-4}"
MAX_CONNECTIONS="${MAX_CONNECTIONS:-2000}"
BENCH_N="${BENCH_N:-20000}"
BENCH_C="${BENCH_C:-50}"
BENCH_SECONDS="${BENCH_SECONDS:-35}"
PERF_SECONDS="${PERF_SECONDS:-20}"
STRACE_SECONDS="${STRACE_SECONDS:-10}"

cleanup() {
  pkill -f "./build/sunkv" || true
}
trap cleanup EXIT

echo "[1/6] 清理旧进程与旧产物..."
pkill -f "./build/sunkv" || true
rm -f "${LOG_DIR}/stage8_2_perf.data" \
      "${LOG_DIR}/stage8_2_perf_report.txt" \
      "${LOG_DIR}/stage8_2_perf_script.txt" \
      "${LOG_DIR}/stage8_2_strace_summary.txt" \
      "${LOG_DIR}/stage8_2_benchmark_qps.log" \
      "${LOG_DIR}/stage8_2_server_stdout.log"

echo "[2/6] 启动服务端..."
./build/sunkv --port "${PORT}" --thread-pool-size "${THREAD_POOL_SIZE}" --max-connections "${MAX_CONNECTIONS}" \
  > "${LOG_DIR}/stage8_2_server_stdout.log" 2>&1 &
SERVER_PID=$!
echo "SERVER_PID=${SERVER_PID}"
sleep 2

echo "[3/6] 启动 benchmark（后台，持续 ${BENCH_SECONDS}s）..."
# 用 timeout 按时间施压，避免 -n 太小导致采样窗口内负载已结束。
timeout "${BENCH_SECONDS}s" redis-benchmark -h 127.0.0.1 -p "${PORT}" \
  -n "${BENCH_N}" -c "${BENCH_C}" -t set,get -q \
  > "${LOG_DIR}/stage8_2_benchmark_qps.log" 2>&1 || true &
BENCH_PID=$!
echo "BENCH_PID=${BENCH_PID}"

echo "[4/6] 在压测负载下执行 perf 采样 ${PERF_SECONDS}s..."
sudo perf record -F 199 -g -p "${SERVER_PID}" -- sleep "${PERF_SECONDS}"
sudo perf report --stdio -i perf.data > "${LOG_DIR}/stage8_2_perf_report.txt"
sudo perf script -i perf.data > "${LOG_DIR}/stage8_2_perf_script.txt"
mv -f perf.data "${LOG_DIR}/stage8_2_perf.data"

echo "[5/6] 在压测负载下执行 strace 汇总 ${STRACE_SECONDS}s..."
# 注意：-p 附着模式不会因 "-- sleep N" 自动结束，必须用 timeout 强制收敛。
timeout "${STRACE_SECONDS}s" sudo strace -p "${SERVER_PID}" -c -f \
  -o "${LOG_DIR}/stage8_2_strace_summary.txt" -qq \
  -e trace=network,read,write,epoll_wait,epoll_ctl,futex,nanosleep >/dev/null 2>&1 || true

echo "[5.5/6] 等待 benchmark 结束..."
wait "${BENCH_PID}" || true

echo "[6/6] 关闭服务端..."
kill -TERM "${SERVER_PID}" || true
sleep 1
pkill -f "./build/sunkv" || true

echo
echo "===== PERF TOP ====="
grep -E "^[[:space:]]*[0-9]+\\.[0-9]+%|^[[:space:]]*[0-9]+%|Server::|RESP|epoll|futex|read|write" "${LOG_DIR}/stage8_2_perf_report.txt" || true

echo
echo "===== STRACE SUMMARY ====="
if [[ -f "${LOG_DIR}/stage8_2_strace_summary.txt" ]]; then
  sed -n '1,120p' "${LOG_DIR}/stage8_2_strace_summary.txt"
fi

echo
echo "===== BENCH QPS ====="
grep -E "SET:|GET:" "${LOG_DIR}/stage8_2_benchmark_qps.log" || true

echo
echo "产物目录: ${LOG_DIR}"
