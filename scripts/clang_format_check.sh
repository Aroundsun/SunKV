#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

command -v clang-format >/dev/null 2>&1 || {
  echo "ERROR: clang-format not found in PATH" >&2
  exit 2
}

# 在 CI 中只检查“本次变更涉及的文件”，避免对历史代码一次性强制重排。
# push：对比 origin/main...HEAD
# PR：对比 github.base_ref...HEAD（若可用）
BASE_REF="${BASE_REF:-origin/main}"

if [[ -n "${GITHUB_BASE_REF:-}" ]]; then
  BASE_REF="origin/${GITHUB_BASE_REF}"
fi

git fetch --no-tags --depth=1 origin main >/dev/null 2>&1 || true

files="$(git diff --name-only --diff-filter=ACMRT "${BASE_REF}...HEAD" \
  | rg -n --no-heading -S '\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$' || true)"

if [[ -z "${files}" ]]; then
  echo "OK: clang-format (no changed C/C++ files)"
  exit 0
fi

rc=0
while IFS= read -r f; do
  [[ -f "${f}" ]] || continue
  if ! clang-format --dry-run --Werror "${f}"; then
    rc=1
  fi
done <<< "${files}"

if [[ "${rc}" -ne 0 ]]; then
  echo
  echo "ERROR: clang-format check failed. Run:" >&2
  echo "  clang-format -i <file>" >&2
  exit 1
fi

echo "OK: clang-format"

