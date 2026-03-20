#include "storage/WAL.h"
#include <iostream>
#include <filesystem>

int main() {
    std::string test_file = "/tmp/debug_wal.log";
    std::filesystem::remove(test_file);
    
    std::cout << "Before WALWriter: " << (std::filesystem::exists(test_file) ? "exists" : "not exists") << std::endl;
    
    {
        WALWriter writer(test_file, 32);  // 小缓冲区
        bool opened = writer.open();
        std::cout << "Open result: " << (opened ? "success" : "failed") << std::endl;
        
        if (opened) {
            writer.set_sync_mode(true);
            
            WALLogEntry entry;
            entry.operation = WALOperationType::SET;
            entry.key = "test";
            entry.value = "value";
            entry.ttl_ms = -1;
            
            bool written = writer.write_entry(entry);
            std::cout << "Write result: " << (written ? "success" : "failed") << std::endl;
            
            bool flushed = writer.flush();
            std::cout << "Flush result: " << (flushed ? "success" : "failed") << std::endl;
            
            std::cout << "After operations: " << (std::filesystem::exists(test_file) ? "exists" : "not exists") << std::endl;
            
            if (std::filesystem::exists(test_file)) {
                auto size = std::filesystem::file_size(test_file);
                std::cout << "File size: " << size << " bytes" << std::endl;
            }
        }
    }
    
    std::cout << "After destruction: " << (std::filesystem::exists(test_file) ? "exists" : "not exists") << std::endl;
    
    if (std::filesystem::exists(test_file)) {
        auto size = std::filesystem::file_size(test_file);
        std::cout << "Final file size: " << size << " bytes" << std::endl;
        
        // 显示文件内容
        std::ifstream file(test_file, std::ios::binary);
        if (file.is_open()) {
            char buffer[64];
            file.read(buffer, 64);
            std::cout << "File content: ";
            for (int i = 0; i < file.gcount(); ++i) {
                printf("%02x ", (unsigned char)buffer[i]);
            }
            std::cout << std::endl;
            file.close();
        }
    }
    
    return 0;
}
