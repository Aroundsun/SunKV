#include "../storage/WAL.h"
#include <iostream>
#include <filesystem>

int main() {
    std::string wal_file = "simple_persistence.log";
    
    std::cout << "=== WAL Reader 调试 ===" << std::endl;
    
    if (!std::filesystem::exists(wal_file)) {
        std::cout << "WAL 文件不存在: " << wal_file << std::endl;
        return 1;
    }
    
    std::cout << "文件大小: " << std::filesystem::file_size(wal_file) << " 字节" << std::endl;
    
    WALReader reader(wal_file);
    if (!reader.open()) {
        std::cout << "无法打开 WAL 文件" << std::endl;
        return 1;
    }
    
    std::cout << "WAL 文件已打开" << std::endl;
    
    // 检查初始状态
    std::cout << "初始 eof 状态: " << reader.eof() << std::endl;
    std::cout << "初始位置: " << reader.get_position() << std::endl;
    
    int count = 0;
    while (count < 10) {  // 限制循环次数
        std::cout << "\n=== 尝试读取条目 " << count << " ===" << std::endl;
        std::cout << "读取前 eof 状态: " << reader.eof() << std::endl;
        std::cout << "读取前位置: " << reader.get_position() << std::endl;
        
        auto entry = reader.read_next_entry();
        
        std::cout << "读取后 eof 状态: " << reader.eof() << std::endl;
        std::cout << "读取后位置: " << reader.get_position() << std::endl;
        
        if (!entry) {
            std::cout << "没有读取到条目，停止" << std::endl;
            break;
        }
        
        std::cout << "成功读取条目: " << entry->key << " = " << entry->value 
                  << " (操作: " << static_cast<int>(entry->operation) << ")" << std::endl;
        
        count++;
    }
    
    reader.close();
    std::cout << "\n总共读取了 " << count << " 个条目" << std::endl;
    
    return 0;
}
