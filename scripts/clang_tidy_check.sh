#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

command -v clang-tidy >/dev/null 2>&1 || {
  echo "ERROR: clang-tidy not found in PATH" >&2
  exit 2
}

BUILD_DIR="${BUILD_DIR:-build}"
COMPILE_COMMANDS="${COMPILE_COMMANDS:-${BUILD_DIR}/compile_commands.json}"
if [[ ! -f "${COMPILE_COMMANDS}" ]]; then
  echo "ERROR: compile_commands.json not found at ${COMPILE_COMMANDS}" >&2
  echo "Hint: configure CMake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
  exit 2
fi

BASE_REF="${BASE_REF:-origin/main}"
if [[ -n "${GITHUB_BASE_REF:-}" ]]; then
  BASE_REF="origin/${GITHUB_BASE_REF}"
fi
git fetch --no-tags --depth=1 origin main >/dev/null 2>&1 || true

files="$(git diff --name-only --diff-filter=ACMRT "${BASE_REF}...HEAD" \
  | rg -n --no-heading -S '\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$' || true)"

if [[ -z "${files}" ]]; then
  echo "OK: clang-tidy (no changed C/C++ files)"
  exit 0
fi

rc=0
while IFS= read -r f; do
  [[ -f "${f}" ]] || continue
  # 只检查核心目录，避免对工具/测试/第三方造成噪声
  if [[ "${f}" != network/* && "${f}" != server/* && "${f}" != storage2/* && "${f}" != common/* && "${f}" != protocol/* ]]; then
    continue
  fi
  if ! clang-tidy "${f}" -p "${BUILD_DIR}"; then
    rc=1
  fi
done <<< "${files}"

if [[ "${rc}" -ne 0 ]]; then
  echo
  echo "ERROR: clang-tidy check failed." >&2
  exit 1
fi

echo "OK: clang-tidy"

