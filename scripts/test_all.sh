#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/test"
# 按名字过滤测试用例 支持正则表达式
REGEX=""  

usage() {
  cat <<'EOF'
Usage:
  scripts/test_all.sh [options]

Options:
  -h, --help                 Show help
  -R, --regex <pattern>      Run only tests matching regex

Examples:
  scripts/test_all.sh
  scripts/test_all.sh -R "server_.*"
EOF
}
# 解析参数
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    -R|--regex)
      REGEX="${2:-}"
      shift 2
      ;;
    *)
      echo "[test_all] unknown argument: $1"
      usage
      exit 1
      ;;
  esac
done

# 创建构建目录
mkdir -p "${BUILD_DIR}"
echo "[test_all] configuring with tests ..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_TEST=ON
# 构建
echo "[test_all] building ..."
cmake --build "${BUILD_DIR}" -j
# 构建测试参数
CTEST_ARGS=(--test-dir "${BUILD_DIR}")
CTEST_ARGS+=(--output-on-failure)
if [[ -n "${REGEX}" ]]; then
  CTEST_ARGS+=(-R "${REGEX}")
fi
# 运行测试
echo "[test_all] running ctest ${CTEST_ARGS[*]}"
ctest "${CTEST_ARGS[@]}"
