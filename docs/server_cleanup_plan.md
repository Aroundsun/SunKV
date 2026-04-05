# Server 文件夹清理计划

## 已完成的清理

### 1. 文件结构清理 ✅
- 移动备份文件到 `tmp/server_backup/`:
  - `Server.h.backup`
  - `main.cpp.bak`
- 移动测试文件到 `tmp/server_backup/`:
  - `simple_main.cpp`
  - `test_compile.cpp` 
  - `test_server.cpp`

### 2. 当前 server 文件夹结构 ✅
```
server/
├── Server.cpp    (68K, 主要服务器实现)
├── Server.h      (6K, 服务器头文件)
└── main.cpp      (6K, 主程序入口)
```

## 待处理的清理项目

### 3. 调试代码清理 🔄
**问题**: `Server.cpp` 包含 111 行 `DEBUG:` 调试代码，文件过大

**清理选项**:
1. **完全移除**: 删除所有调试代码
2. **条件编译**: 使用 `#ifdef DEBUG` 条件编译
3. **日志级别**: 将调试代码改为使用日志系统

**推荐方案**: 条件编译 + 日志系统

### 4. 代码优化 🔄
- 移除重复的调试输出
- 统一使用日志系统
- 优化代码结构

### 5. 文档更新 🔄
- 更新代码注释
- 添加调试模式说明

## 实施步骤

### 第一步：条件化调试代码
```cpp
#ifdef DEBUG
    std::cerr << "DEBUG: Server::start() called" << std::endl;
#endif
```

### 第二步：替换为日志系统
```cpp
LOG_DEBUG("Server::start() called");
```

### 第三步：清理构建系统
- 更新 CMakeLists.txt
- 添加 DEBUG 编译选项

## 预期结果

清理后的 server 文件夹:
- 文件大小减少约 30-40%
- 代码更专业和整洁
- 支持调试模式编译
- 保持功能完整性

## 风险评估

- **低风险**: 调试代码清理不影响核心功能
- **可恢复**: 所有清理的文件都备份在 `tmp/server_backup/`
- **测试验证**: 清理后需要完整测试优雅关闭功能
