#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
BIN="${BUILD_DIR}/sunkv"

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6379}"

KEYS_TOTAL="${KEYS_TOTAL:-10000}"
SEED="${SEED:-42}"

WORK_DIR="${WORK_DIR:-${ROOT_DIR}/.cache/recovery_10k}"
DATA_DIR="${DATA_DIR:-${ROOT_DIR}/data}"
LOG_DIR="${LOG_DIR:-${DATA_DIR}/logs}"

mkdir -p "${WORK_DIR}" "${LOG_DIR}"

RESP_FILE="${WORK_DIR}/dataset_${KEYS_TOTAL}_seed_${SEED}.resp"
VERIFY_FILE="${WORK_DIR}/verify_${KEYS_TOTAL}_seed_${SEED}.resp"

SERVER_LOG="${LOG_DIR}/recovery_10k_debug.log"

function die() {
  echo "ERROR: $*" >&2
  exit 1
}

function ensure_tools() {
  command -v redis-cli >/dev/null 2>&1 || die "need redis-cli in PATH"
  [[ -x "${BIN}" ]] || die "missing ${BIN}; build it first"
}

function wait_ready() {
  # 恢复阶段可能需要较长时间（尤其 WAL 较大），这里给到 120s 等待窗口。
  for _ in $(seq 1 600); do
    if redis-cli -h "${HOST}" -p "${PORT}" PING >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

function start_server() {
  pkill -TERM -x sunkv >/dev/null 2>&1 || true
  sleep 0.2
  {
    echo ""
    echo "=============================="
    echo "START_SERVER ts=$(date +'%F %T') build=${BIN} port=${PORT}"
    echo "=============================="
  } >> "${SERVER_LOG}"

  "${BIN}" --port "${PORT}" --enable-console-log 0 --log-file "${SERVER_LOG}" --log-level DEBUG >/dev/null 2>&1 &
  local pid=$!
  echo "START_SERVER pid=${pid} log=${SERVER_LOG}"

  if ! wait_ready; then
    echo "--- server log (tail) ---" >&2
    python3 - <<PY
import pathlib
p=pathlib.Path("${SERVER_LOG}")
print(p.read_text(errors="ignore")[-4000:])
PY
    die "server not ready"
  fi
}

function stop_server() {
  pkill -TERM -x sunkv >/dev/null 2>&1 || true
  # 等待退出
  for _ in $(seq 1 100); do
    pgrep -x sunkv >/dev/null 2>&1 || return 0
    sleep 0.05
  done
  pkill -KILL -x sunkv >/dev/null 2>&1 || true
}

function gen_dataset() {
  # 1 万 key 分 4 类：string/list/set/hash
  python3 - <<'PY' > "${RESP_FILE}"
import random, os, sys

keys_total = int(os.environ.get("KEYS_TOTAL","10000"))
seed = int(os.environ.get("SEED","42"))
rng = random.Random(seed)

def bulk(s: str) -> bytes:
    b = s.encode("utf-8")
    return b"$%d\r\n" % len(b) + b + b"\r\n"

def arr(*items: str) -> bytes:
    out = [f"*{len(items)}\r\n".encode("ascii")]
    for it in items:
        out.append(bulk(it))
    return b"".join(out)

n = keys_total
per = n // 4
rest = n - per*4
counts = [per, per, per, per+rest]  # hash 多拿余数

def rand_suffix():
    return "%08x" % rng.getrandbits(32)

out = bytearray()
out += arr("FLUSHALL")

# strings:  s:{i}:{rand} -> v:{i}:{rand}
for i in range(counts[0]):
    suf = rand_suffix()
    k = f"s:{i}:{suf}"
    v = f"sv:{i}:{suf}"
    out += arr("SET", k, v)

# lists: l:{i}:{rand} with 3 elems
for i in range(counts[1]):
    suf = rand_suffix()
    k = f"l:{i}:{suf}"
    out += arr("RPUSH", k, f"l0:{i}:{suf}", f"l1:{i}:{suf}", f"l2:{i}:{suf}")

# sets: t:{i}:{rand} with 3 members
for i in range(counts[2]):
    suf = rand_suffix()
    k = f"t:{i}:{suf}"
    out += arr("SADD", k, f"m0:{i}:{suf}", f"m1:{i}:{suf}", f"m2:{i}:{suf}")

# hashes: h:{i}:{rand} with 3 fields
for i in range(counts[3]):
    suf = rand_suffix()
    k = f"h:{i}:{suf}"
    out += arr("HSET", k, f"f0:{i}:{suf}", f"v0:{i}:{suf}", f"f1:{i}:{suf}", f"v1:{i}:{suf}", f"f2:{i}:{suf}", f"v2:{i}:{suf}")

sys.stdout.buffer.write(out)
PY
}

function apply_dataset() {
  python3 - <<'PY'
import os, socket, sys

host = os.environ.get("HOST","127.0.0.1")
port = int(os.environ.get("PORT","6379"))
resp_path = os.environ["RESP_FILE"]

data = open(resp_path,"rb").read()

def read_one(sock: socket.socket):
    # 仅用于计数：不完全解析所有类型的内容，只保证把一个 RESP 回复从 TCP 流里消费掉。
    # 支持: + - : $ *
    def recv_exact(n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = sock.recv(n - len(buf))
            if not chunk:
                raise EOFError("socket closed")
            buf += chunk
        return bytes(buf)

    def recv_until_crlf() -> bytes:
        buf = bytearray()
        while True:
            ch = sock.recv(1)
            if not ch:
                raise EOFError("socket closed")
            buf += ch
            if len(buf) >= 2 and buf[-2:] == b"\r\n":
                return bytes(buf)

    prefix = recv_exact(1)
    if prefix in (b"+", b"-", b":"):
        recv_until_crlf()
        return
    if prefix == b"$":
        line = recv_until_crlf()
        n = int(line[:-2])
        if n == -1:
            return
        recv_exact(n + 2)
        return
    if prefix == b"*":
        line = recv_until_crlf()
        n = int(line[:-2])
        if n == -1:
            return
        for _ in range(n):
            read_one(sock)
        return
    raise ValueError(f"bad resp prefix: {prefix!r}")

s = socket.create_connection((host, port), timeout=5)
s.settimeout(10)

s.sendall(data)

# 估算命令数：按 RESP 数组头 '*<n>' 计数（每条命令是一条数组）
# 这里简单扫描：遇到 "\n*" 的位置统计不可靠；直接按生成逻辑计算更稳。
# 为了脚本自包含，我们粗略统计：每条命令都有 "*<k>\r\n" 开头，逐字节扫描。
cmds = 0
i = 0
while True:
    j = data.find(b"*", i)
    if j == -1:
        break
    # 只计数“行首的 *”
    if j == 0 or data[j-1:j] == b"\n":
        cmds += 1
    i = j + 1

for _ in range(cmds):
    read_one(s)

s.close()
print(f"LOAD ok cmds={cmds} bytes={len(data)}")
PY
}

function gen_verify() {
  # 生成验证指令：对每个 key 做严格校验（string GET / list LLEN+LINDEX / set SCARD+SISMEMBER / hash HLEN+HGET）
  # 注意：VERIFY_FILE 只包含“命令”，期望值由脚本内部按同一 seed 计算，不写进协议流（避免把 EXPECT 当作命令发给服务端）。
  python3 - <<'PY' > "${VERIFY_FILE}"
import random, os, sys

keys_total = int(os.environ.get("KEYS_TOTAL","10000"))
seed = int(os.environ.get("SEED","42"))
rng = random.Random(seed)

def bulk(s: str) -> bytes:
    b = s.encode("utf-8")
    return b"$%d\r\n" % len(b) + b + b"\r\n"

def arr(*items: str) -> bytes:
    out = [f"*{len(items)}\r\n".encode("ascii")]
    for it in items:
        out.append(bulk(it))
    return b"".join(out)

n = keys_total
per = n // 4
rest = n - per*4
counts = [per, per, per, per+rest]

def rand_suffix():
    return "%08x" % rng.getrandbits(32)

out = bytearray()

for i in range(counts[0]):
    suf = rand_suffix()
    k = f"s:{i}:{suf}"
    out += arr("GET", k)

for i in range(counts[1]):
    suf = rand_suffix()
    k = f"l:{i}:{suf}"
    out += arr("LLEN", k)
    for j in range(3):
        out += arr("LINDEX", k, str(j))

for i in range(counts[2]):
    suf = rand_suffix()
    k = f"t:{i}:{suf}"
    out += arr("SCARD", k)
    for j in range(3):
        out += arr("SISMEMBER", k, f"m{j}:{i}:{suf}")

for i in range(counts[3]):
    suf = rand_suffix()
    k = f"h:{i}:{suf}"
    out += arr("HLEN", k)
    for j in range(3):
        out += arr("HGET", k, f"f{j}:{i}:{suf}")

sys.stdout.buffer.write(out)
PY
}

function run_verify() {
  python3 - <<'PY'
import os, random, socket, sys

host = os.environ.get("HOST","127.0.0.1")
port = int(os.environ.get("PORT","6379"))
verify_path = os.environ["VERIFY_FILE"]
keys_total = int(os.environ.get("KEYS_TOTAL","10000"))
seed = int(os.environ.get("SEED","42"))

data = open(verify_path,"rb").read()

def recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("socket closed")
        buf += chunk
    return bytes(buf)

def recv_until_crlf(sock: socket.socket) -> bytes:
    buf = bytearray()
    while True:
        ch = sock.recv(1)
        if not ch:
            raise EOFError("socket closed")
        buf += ch
        if len(buf) >= 2 and buf[-2:] == b"\r\n":
            return bytes(buf)

def read_resp(sock: socket.socket):
    p = recv_exact(sock, 1)
    if p in (b"+", b"-", b":"):
        line = recv_until_crlf(sock)[:-2]
        return p, line
    if p == b"$":
        line = recv_until_crlf(sock)[:-2]
        n = int(line)
        if n == -1:
            return p, None
        body = recv_exact(sock, n)
        recv_exact(sock, 2)  # CRLF
        return p, body
    if p == b"*":
        line = recv_until_crlf(sock)[:-2]
        n = int(line)
        if n == -1:
            return p, None
        items = []
        for _ in range(n):
            items.append(read_resp(sock))
        return p, items
    raise ValueError(f"bad resp prefix: {p!r}")

s = socket.create_connection((host, port), timeout=5)
s.settimeout(10)
s.sendall(data)

n = keys_total
per = n // 4
rest = n - per*4
counts = [per, per, per, per+rest]

rng = random.Random(seed)
def rand_suffix():
    return "%08x" % rng.getrandbits(32)

errors = 0
checked = 0

def expect_bulk_str(exp: str):
    global errors, checked
    t, v = read_resp(s)
    checked += 1
    if t != b"$" or (v is None):
        errors += 1
        return
    if v.decode("utf-8", "strict") != exp:
        errors += 1

def expect_int(exp: int):
    global errors, checked
    t, v = read_resp(s)
    checked += 1
    if t != b":":
        errors += 1
        return
    if int(v) != exp:
        errors += 1

# strings
for i in range(counts[0]):
    suf = rand_suffix()
    k = f"s:{i}:{suf}"
    expect_bulk_str(f"sv:{i}:{suf}")

# lists
for i in range(counts[1]):
    suf = rand_suffix()
    expect_int(3)  # LLEN
    for j in range(3):
        expect_bulk_str(f"l{j}:{i}:{suf}")

# sets
for i in range(counts[2]):
    suf = rand_suffix()
    expect_int(3)  # SCARD
    for _ in range(3):
        expect_int(1)  # SISMEMBER

# hashes
for i in range(counts[3]):
    suf = rand_suffix()
    expect_int(3)  # HLEN
    for j in range(3):
        expect_bulk_str(f"v{j}:{i}:{suf}")

s.close()
print(f"VERIFY checked={checked} errors={errors}")
sys.exit(1 if errors else 0)
PY
}

function verify_wrongtype_semantics() {
  local key="wrongtype_probe_key"
  redis-cli -h "${HOST}" -p "${PORT}" DEL "${key}" >/dev/null
  redis-cli -h "${HOST}" -p "${PORT}" SET "${key}" "v" >/dev/null

  local out
  out="$(redis-cli -h "${HOST}" -p "${PORT}" LPUSH "${key}" "x" 2>&1 || true)"
  [[ "${out}" == *"WRONGTYPE"* ]] || die "LPUSH on string key should be WRONGTYPE, got: ${out}"

  out="$(redis-cli -h "${HOST}" -p "${PORT}" SADD "${key}" "x" 2>&1 || true)"
  [[ "${out}" == *"WRONGTYPE"* ]] || die "SADD on string key should be WRONGTYPE, got: ${out}"

  out="$(redis-cli -h "${HOST}" -p "${PORT}" HSET "${key}" "f" "v" 2>&1 || true)"
  [[ "${out}" == *"WRONGTYPE"* ]] || die "HSET on string key should be WRONGTYPE, got: ${out}"
}

ensure_tools

export KEYS_TOTAL SEED HOST PORT RESP_FILE VERIFY_FILE

echo "[1/6] 生成 1 万条混合数据 (seed=${SEED})"
gen_dataset

echo "[2/6] 启动服务（Debug 日志级别=DEBUG，log=${SERVER_LOG}）"
start_server

echo "[3/6] 写入数据（--pipe）"
apply_dataset

echo "[4/6] 写入后立刻校验一次"
gen_verify
run_verify
verify_wrongtype_semantics

echo "[5/6] 停机 -> 重启"
stop_server
start_server

echo "[6/6] 重启后校验恢复"
run_verify
verify_wrongtype_semantics

echo "OK: recovery verify passed (keys_total=${KEYS_TOTAL}, seed=${SEED})"
stop_server

