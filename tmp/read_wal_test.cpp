#include "../storage/WAL.h"
#include <iostream>

int main() {
    std::string test_file = "test_wal_writer.log";
    
    WALReader reader(test_file);
    if (!reader.open()) {
        std::cout << "Failed to open WAL file" << std::endl;
        return 1;
    }
    
    std::cout << "=== WAL File Content ===" << std::endl;
    
    int count = 0;
    while (!reader.eof()) {
        auto entry = reader.read_next_entry();
        if (!entry) {
            break;
        }
        
        std::cout << "Entry " << count++ << ": ";
        std::cout << "seq=" << entry->sequence_number;
        std::cout << ", op=" << static_cast<int>(entry->operation);
        std::cout << ", key=" << entry->key;
        std::cout << ", value=" << entry->value;
        std::cout << ", ttl=" << entry->ttl_ms;
        std::cout << std::endl;
        
        if (count > 10) break; // 限制输出
    }
    
    reader.close();
    return 0;
}
