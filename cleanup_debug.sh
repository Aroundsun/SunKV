#!/bin/bash

# 清理 Server.cpp 中的调试代码
# 将 std::cerr << "DEBUG: ..."; 包装为条件编译

FILE="server/Server.cpp"
BACKUP="tmp/server_backup/Server.cpp.debug_backup"

# 创建备份
cp "$FILE" "$BACKUP"

# 使用 sed 批量替换
sed -i 's|std::cerr << "DEBUG:|#ifdef DEBUG\n    std::cerr << "DEBUG:|g' "$FILE"
sed -i 's|" << std::endl;|" << std::endl;\n#endif|g' "$FILE"

echo "调试代码清理完成"
echo "备份文件: $BACKUP"
echo "处理文件: $FILE"
