# SunKV 一键演示 Runbook

## 前置条件

- 已完成构建：

```bash
cmake -S . -B build -DBUILD_TEST=ON
cmake --build build -j$(nproc)
```

- 本机已安装 `redis-cli` 与 `redis-benchmark`。

## 一键演示

```bash
chmod +x scripts/demo_all_in_one.sh
HOST=127.0.0.1 PORT=6391 ./scripts/demo_all_in_one.sh
```

脚本会串行执行：

1. 功能回归（`scripts/functional_suite.sh`）
2. 稳定压测（`scripts/redis_benchmark_stable.sh`）
3. 自动汇总输出

## 结果查看

脚本完成后会输出 `summary` 路径，默认在：

- `data/logs/demo_<timestamp>/summary.md`
- `data/logs/demo_<timestamp>/functional.log`
- `data/logs/demo_<timestamp>/benchmark.log`

## 常见问题

- 如果功能回归失败，先看 `functional.log` 最后 30 行。
- 如果压测失败，优先查 `benchmark.log` 中 `FAIL` 和 `[case-metric]`。
- 端口冲突时切换端口重跑：`PORT=6392 ./scripts/demo_all_in_one.sh`。
