#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6391}"
TS="$(date +%Y%m%d_%H%M%S)"

OUT_DIR="${OUT_DIR:-${ROOT_DIR}/data/logs/demo_${TS}}"
mkdir -p "${OUT_DIR}"

FUNCTIONAL_LOG="${OUT_DIR}/functional.log"
BENCH_LOG="${OUT_DIR}/benchmark.log"
SUMMARY_MD="${OUT_DIR}/summary.md"

echo "[1/4] 功能回归（functional_suite）"
if PORT="${PORT}" HOST="${HOST}" ./scripts/functional_suite.sh >"${FUNCTIONAL_LOG}" 2>&1; then
  FUNCTIONAL_STATUS="PASS"
else
  FUNCTIONAL_STATUS="FAIL"
fi

echo "[2/4] 稳定压测（redis_benchmark_stable）"
if PORT="${PORT}" HOST="${HOST}" NORMAL_N=10000 PIPE_N=10000 ./scripts/redis_benchmark_stable.sh >"${BENCH_LOG}" 2>&1; then
  BENCH_STATUS="PASS"
else
  BENCH_STATUS="FAIL"
fi

echo "[3/4] 生成摘要"
{
  echo "# SunKV 一键演示摘要"
  echo
  echo "- timestamp: \`${TS}\`"
  echo "- host: \`${HOST}\`"
  echo "- port: \`${PORT}\`"
  echo "- functional status: \`${FUNCTIONAL_STATUS}\`"
  echo "- benchmark status: \`${BENCH_STATUS}\`"
  echo "- functional log: \`${FUNCTIONAL_LOG}\`"
  echo "- benchmark log: \`${BENCH_LOG}\`"
  echo
  echo "## 关键片段"
  echo
  echo "### functional tail"
  echo '```text'
  tail -n 20 "${FUNCTIONAL_LOG}" || true
  echo '```'
  echo
  echo "### benchmark summaries"
  echo '```text'
  rg "\\[case-summary\\]|\\[case-metric\\]|DONE:" "${BENCH_LOG}" || true
  echo '```'
} > "${SUMMARY_MD}"

echo "[4/4] 完成"
echo "summary: ${SUMMARY_MD}"

if [[ "${FUNCTIONAL_STATUS}" != "PASS" || "${BENCH_STATUS}" != "PASS" ]]; then
  exit 2
fi
