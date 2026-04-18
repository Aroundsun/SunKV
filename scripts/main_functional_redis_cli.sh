#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6379}"
SERVER_BIN="${SERVER_BIN:-${ROOT_DIR}/build/sunkv}"
LOG_DIR="${ROOT_DIR}/data/logs"
mkdir -p "${LOG_DIR}"

TS="$(date +%Y%m%d_%H%M%S)"
OUT_LOG="${LOG_DIR}/main_functional_redis_cli_${TS}.log"
SERVER_LOG="${LOG_DIR}/main_functional_redis_cli_server_${TS}.log"
SERVER_PID=""
FAIL_COUNT=0

log() {
  echo "[$(date +%H:%M:%S)] $*" | tee -a "${OUT_LOG}"
}

fail() {
  log "FAIL: $*"
  FAIL_COUNT=$((FAIL_COUNT + 1))
}

assert_eq() {
  local got="$1"
  local exp="$2"
  local msg="$3"
  if [[ "${got}" != "${exp}" ]]; then
    fail "${msg} (got='${got}', exp='${exp}')"
    return
  fi
  log "PASS: ${msg}"
}

redis_raw() {
  redis-cli --raw -h "${HOST}" -p "${PORT}" "$@"
}

wait_ready() {
  for _ in $(seq 1 30); do
    if redis_raw PING >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

start_server() {
  log "启动服务..."
  "${SERVER_BIN}" \
    --host "${HOST}" \
    --port "${PORT}" \
    --log-level INFO \
    --enable-console-log false \
    --log-strategy fixed \
    --log-file "${SERVER_LOG}" &
  SERVER_PID=$!
  if ! wait_ready; then
    fail "服务启动失败，未通过 PING 检测"
  fi
  log "服务已就绪，pid=${SERVER_PID}"
}

stop_server() {
  if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    log "停止服务 pid=${SERVER_PID}"
    kill -TERM "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi
  SERVER_PID=""
}

cleanup() {
  stop_server
}
trap cleanup EXIT

if ! command -v redis-cli >/dev/null 2>&1; then
  echo "未找到 redis-cli" | tee -a "${OUT_LOG}"
  exit 1
fi
if [[ ! -x "${SERVER_BIN}" ]]; then
  echo "服务二进制不存在或不可执行: ${SERVER_BIN}" | tee -a "${OUT_LOG}"
  exit 1
fi

log "测试开始：主功能（redis-cli）"
start_server

log "准备环境：清空数据库"
assert_eq "$(redis_raw FLUSHALL)" "OK" "FLUSHALL"

log "1) 字符串命令"
assert_eq "$(redis_raw SET k1 v1)" "OK" "SET k1"
assert_eq "$(redis_raw GET k1)" "v1" "GET k1"
assert_eq "$(redis_raw EXISTS k1)" "1" "EXISTS k1"
assert_eq "$(redis_raw DEL k1)" "1" "DEL k1"
assert_eq "$(redis_raw GET k1)" "" "GET k1 after DEL returns nil"

log "2) 列表命令"
assert_eq "$(redis_raw LPUSH l1 c b a)" "3" "LPUSH l1"
assert_eq "$(redis_raw LLEN l1)" "3" "LLEN l1"
assert_eq "$(redis_raw LPOP l1)" "a" "LPOP l1"
assert_eq "$(redis_raw RPOP l1)" "c" "RPOP l1"
assert_eq "$(redis_raw LLEN l1)" "1" "LLEN l1 after pops"

log "3) 集合命令"
assert_eq "$(redis_raw SADD s1 x y x)" "2" "SADD s1"
assert_eq "$(redis_raw SCARD s1)" "2" "SCARD s1"
assert_eq "$(redis_raw SISMEMBER s1 x)" "1" "SISMEMBER s1 x"
assert_eq "$(redis_raw SREM s1 x)" "1" "SREM s1 x"
assert_eq "$(redis_raw SISMEMBER s1 x)" "0" "SISMEMBER s1 x after remove"

log "4) 哈希命令"
assert_eq "$(redis_raw HSET h1 f1 v1)" "1" "HSET h1 f1"
assert_eq "$(redis_raw HSET h1 f2 v2)" "1" "HSET h1 f2"
assert_eq "$(redis_raw HGET h1 f1)" "v1" "HGET h1 f1"
assert_eq "$(redis_raw HLEN h1)" "2" "HLEN h1"
assert_eq "$(redis_raw HEXISTS h1 f2)" "1" "HEXISTS h1 f2"
assert_eq "$(redis_raw HDEL h1 f2)" "1" "HDEL h1 f2"
assert_eq "$(redis_raw HEXISTS h1 f2)" "0" "HEXISTS h1 f2 after delete"

log "5) TTL 命令"
assert_eq "$(redis_raw SET tk tv)" "OK" "SET tk"
assert_eq "$(redis_raw EXPIRE tk 2)" "1" "EXPIRE tk 2"
ttl_now="$(redis_raw TTL tk)"
if [[ "${ttl_now}" -lt 1 || "${ttl_now}" -gt 2 ]]; then
  fail "TTL tk 范围异常 (got=${ttl_now}, exp=1~2)"
else
  log "PASS: TTL tk in [1,2]"
fi
sleep 3
assert_eq "$(redis_raw GET tk)" "" "GET tk after expire returns nil"

log "6) 统计与键空间命令"
assert_eq "$(redis_raw DBSIZE)" "3" "DBSIZE 应为 3"
keys_out="$(redis_raw KEYS)"
if [[ -z "${keys_out}" ]]; then
  fail "KEYS 返回为空"
else
  log "PASS: KEYS non-empty"
fi

log "7) 持久化恢复（快照 + 重启）"
assert_eq "$(redis_raw SET p:k p:v)" "OK" "SET p:k"
assert_eq "$(redis_raw SNAPSHOT)" "OK" "SNAPSHOT"
stop_server
start_server
assert_eq "$(redis_raw GET p:k)" "p:v" "GET p:k after restart"
assert_eq "$(redis_raw HGET h1 f1)" "v1" "HGET h1 f1 after restart"

if [[ "${FAIL_COUNT}" -eq 0 ]]; then
  log "测试完成：全部通过"
  echo "OK" | tee -a "${OUT_LOG}"
else
  log "测试完成：发现 ${FAIL_COUNT} 项失败"
  echo "FAILED(${FAIL_COUNT})" | tee -a "${OUT_LOG}"
  exit 1
fi

