# SunKV 测试规范

## 测试文件管理

### 测试文件存放位置
1. **测试日志文件**: 必须存放在 `tmp/test_logs/` 目录下
2. **测试源码文件**: 必须存放在 `tmp/test_files/` 目录下
3. **可执行文件**: 必须存放在 `tmp/test_files/` 目录下
4. **禁止**: 任何测试相关文件都不允许放在项目主目录

### 主目录清理规范

#### 禁止在主目录出现的文件类型
- `*.cpp` - C++ 源文件
- `*.o` - 目标文件
- `*.exe` - 可执行文件
- `sunkv` - 服务器可执行文件
- `simple_*` - 简单测试文件
- `test_*` - 测试可执行文件
- `*.log` - 日志文件

#### 正确的文件组织
```
SunKV/
├── build/           # 构建输出
├── tmp/
│   ├── test_logs/   # 测试日志
│   └── test_files/  # 测试源码和可执行文件
├── server/          # 服务器源码
├── network/         # 网络模块
├── storage/         # 存储模块
└── ...
```

### 测试命令规范

#### 启动测试服务器
```bash
# 正确的测试命令
./build/sunkv --port 6399 > tmp/test_logs/test_name.log 2>&1 &

# 错误的测试命令（不要使用）
./build/sunkv --port 6399 > test_name.log 2>&1 &
```

#### 测试端口分配
- 优雅关闭测试：6390-6399
- 功能测试：6400-6499  
- 性能测试：6500-6599
- 压力测试：6600-6699

#### 清理测试进程
```bash
# 清理所有测试进程
pkill -f "sunkv --port"

# 清理特定端口进程
kill -TERM $(pgrep -f "sunkv --port 6399")

# 强制清理
pkill -9 -f "sunkv --port"
```

### 测试日志命名规范

#### 功能测试
- `graceful_shutdown_test.log` - 优雅关闭测试
- `server_startup_test.log` - 服务器启动测试
- `command_parsing_test.log` - 命令解析测试

#### 性能测试
- `performance_qps_test.log` - QPS性能测试
- `performance_latency_test.log` - 延迟测试
- `performance_concurrency_test.log` - 并发测试

#### 稳定性测试
- `stability_longrun_test.log` - 长时间运行测试
- `stability_memory_test.log` - 内存泄漏测试
- `stability_crash_test.log` - 崩溃恢复测试

### 测试环境管理

#### 测试前准备
```bash
# 确保没有残留进程
pkill -9 -f "sunkv --port"

# 清理测试数据（如需要）
rm -rf data/test_*

# 创建测试目录
mkdir -p tmp/test_logs
```

#### 测试后清理
```bash
# 停止测试进程
pkill -f "sunkv --port"

# 检查进程残留
ps aux | grep "sunkv" | grep -v grep

# 清理临时文件（可选）
# rm -f tmp/test_logs/test_name.log
```

### 自动化测试脚本

#### 单次测试脚本示例
```bash
#!/bin/bash
# test_graceful_shutdown.sh

TEST_NAME="graceful_shutdown"
TEST_PORT=6399
LOG_FILE="tmp/test_logs/${TEST_NAME}_$(date +%Y%m%d_%H%M%S).log"

echo "Starting ${TEST_NAME} test..."
./build/sunkv --port $TEST_PORT > $LOG_FILE 2>&1 &
SERVER_PID=$!

sleep 3
kill -TERM $SERVER_PID

sleep 5
if ps -p $SERVER_PID > /dev/null; then
    echo "ERROR: Server process still running"
    kill -9 $SERVER_PID
    exit 1
fi

echo "Test completed successfully"
echo "Log file: $LOG_FILE"
```

### 持续集成集成

#### CI/CD 测试配置
- 所有测试日志自动存放在 `tmp/test_logs/`
- 测试失败时保留日志用于调试
- 定期清理过期测试日志（保留最近7天）

### 注意事项

1. **禁止在主目录创建测试文件**
2. **使用有意义的日志文件名**
3. **测试完成后清理进程**
4. **避免端口冲突**
5. **定期清理过期测试日志**

### 违规处理

如果在主目录发现测试文件：
```bash
# 移动到正确位置
mv *.log tmp/test_logs/

# 或者删除（如果是临时测试）
rm *.log
```

## 测试报告

测试报告应包含：
- 测试目标和范围
- 测试环境配置
- 测试执行步骤
- 测试结果和日志位置
- 发现的问题和解决方案
