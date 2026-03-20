#include "WAL.h"
#include "StorageEngine.h"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <unistd.h>  // for fsync
#include <iostream>  // for debug

// WAL 日志格式：
// +----------------+----------------+----------------+----------------+
// | Header (8 bytes) | Length (4 bytes) | Type (1 byte)  | TTL (8 bytes) |
// +----------------+----------------+----------------+----------------+
// | Key Length (4 bytes) | Value Length (4 bytes) | Checksum (4 bytes) |
// +----------------+----------------+----------------+----------------+
// | Key (variable) | Value (variable) |
// +----------------+----------------+

// 头部魔数和版本
static const uint32_t WAL_MAGIC_NUMBER = 0x57414C4B; // "WALK"
static const uint16_t WAL_VERSION = 1;

// 头部结构
struct WALHeader {
    uint32_t magic_number;
    uint16_t version;
    uint16_t reserved;
    uint64_t sequence_number;
    uint64_t timestamp;
};

// WALLogEntry 实现

std::vector<uint8_t> WALLogEntry::serialize() const {
    std::vector<uint8_t> data;
    
    // 写入头部
    WALHeader header{};
    header.magic_number = WAL_MAGIC_NUMBER;
    header.version = WAL_VERSION;
    header.reserved = 0;
    header.sequence_number = sequence_number;
    header.timestamp = timestamp;
    
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), header_bytes, header_bytes + sizeof(header));
    
    // 写入操作类型
    data.push_back(static_cast<uint8_t>(operation));
    
    // 写入 TTL
    const uint8_t* ttl_bytes = reinterpret_cast<const uint8_t*>(&ttl_ms);
    data.insert(data.end(), ttl_bytes, ttl_bytes + sizeof(ttl_ms));
    
    // 写入键长度和键
    uint32_t key_len = static_cast<uint32_t>(key.size());
    const uint8_t* key_len_bytes = reinterpret_cast<const uint8_t*>(&key_len);
    data.insert(data.end(), key_len_bytes, key_len_bytes + sizeof(key_len));
    data.insert(data.end(), key.begin(), key.end());
    
    // 写入值长度和值
    uint32_t value_len = static_cast<uint32_t>(value.size());
    const uint8_t* value_len_bytes = reinterpret_cast<const uint8_t*>(&value_len);
    data.insert(data.end(), value_len_bytes, value_len_bytes + sizeof(value_len));
    data.insert(data.end(), value.begin(), value.end());
    
    // 计算并写入校验和
    uint32_t calculated_checksum = calculate_checksum();
    const uint8_t* checksum_bytes = reinterpret_cast<const uint8_t*>(&calculated_checksum);
    data.insert(data.end(), checksum_bytes, checksum_bytes + sizeof(calculated_checksum));
    
    return data;
}

std::unique_ptr<WALLogEntry> WALLogEntry::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(WALHeader) + 1 + sizeof(int64_t) + 2 * sizeof(uint32_t) + sizeof(uint32_t)) {
        return nullptr;
    }
    
    auto entry = std::make_unique<WALLogEntry>();
    size_t offset = 0;
    
    // 读取头部
    WALHeader header;
    std::memcpy(&header, data.data() + offset, sizeof(header));
    offset += sizeof(header);
    
    // 验证魔数和版本
    if (header.magic_number != WAL_MAGIC_NUMBER || header.version != WAL_VERSION) {
        return nullptr;
    }
    
    entry->sequence_number = header.sequence_number;
    entry->timestamp = header.timestamp;
    
    // 读取操作类型
    entry->operation = static_cast<WALOperationType>(data[offset]);
    offset += 1;
    
    // 读取 TTL
    std::memcpy(&entry->ttl_ms, data.data() + offset, sizeof(entry->ttl_ms));
    offset += sizeof(entry->ttl_ms);
    
    // 读取键长度和键
    uint32_t key_len;
    std::memcpy(&key_len, data.data() + offset, sizeof(key_len));
    offset += sizeof(key_len);
    
    if (data.size() < offset + key_len + sizeof(uint32_t) + sizeof(uint32_t)) {
        return nullptr;
    }
    
    entry->key.assign(reinterpret_cast<const char*>(data.data() + offset), key_len);
    offset += key_len;
    
    // 读取值长度和值
    uint32_t value_len;
    std::memcpy(&value_len, data.data() + offset, sizeof(value_len));
    offset += sizeof(value_len);
    
    if (data.size() < offset + value_len + sizeof(uint32_t)) {
        return nullptr;
    }
    
    entry->value.assign(reinterpret_cast<const char*>(data.data() + offset), value_len);
    offset += value_len;
    
    // 读取校验和
    std::memcpy(&entry->checksum, data.data() + offset, sizeof(entry->checksum));
    
    // 验证校验和
    if (!entry->verify_checksum()) {
        return nullptr;
    }
    
    return entry;
}

uint32_t WALLogEntry::calculate_checksum() const {
    // 简单的 CRC32 校验和计算
    uint32_t crc = 0xFFFFFFFF;
    
    // 计算序列号和时间戳的校验和
    const uint8_t* seq_bytes = reinterpret_cast<const uint8_t*>(&sequence_number);
    for (size_t i = 0; i < sizeof(sequence_number); ++i) {
        crc ^= seq_bytes[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    const uint8_t* time_bytes = reinterpret_cast<const uint8_t*>(&timestamp);
    for (size_t i = 0; i < sizeof(timestamp); ++i) {
        crc ^= time_bytes[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    // 计算操作类型的校验和
    uint8_t op = static_cast<uint8_t>(operation);
    crc ^= op;
    for (int j = 0; j < 8; ++j) {
        crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    
    // 计算键的校验和
    for (char c : key) {
        crc ^= static_cast<uint8_t>(c);
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    // 计算值的校验和
    for (char c : value) {
        crc ^= static_cast<uint8_t>(c);
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return crc ^ 0xFFFFFFFF;
}

bool WALLogEntry::verify_checksum() const {
    return calculate_checksum() == checksum;
}

size_t WALLogEntry::size() const {
    return sizeof(WALHeader) + 1 + sizeof(int64_t) + sizeof(uint32_t) + key.size() + 
           sizeof(uint32_t) + value.size() + sizeof(uint32_t);
}

// WALWriter 实现

WALWriter::WALWriter(const std::string& file_path, size_t buffer_size)
    : file_path_(file_path), buffer_size_(buffer_size), sequence_number_(0), sync_mode_(false),
      total_entries_(0), total_bytes_(0), flush_count_(0), sync_count_(0) {
    write_buffer_.reserve(buffer_size_);
}

WALWriter::~WALWriter() {
    close();
}

bool WALWriter::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_.is_open()) {
        return true;
    }
    
    // 确保目录存在
    std::filesystem::path file_path(file_path_);
    std::filesystem::path dir_path = file_path.parent_path();
    if (!dir_path.empty() && !std::filesystem::exists(dir_path)) {
        std::filesystem::create_directories(dir_path);
    }
    
    file_stream_.open(file_path_, std::ios::binary | std::ios::app);
    if (!file_stream_.is_open()) {
        return false;
    }
    
    // 如果文件不为空，读取最后一个序列号
    if (file_stream_.tellp() > 0) {
        // 这里可以实现读取最后一个条目的序列号
        // 暂时从文件大小估算
        sequence_number_ = file_stream_.tellp() / 100; // 粗略估算
    }
    
    return true;
}

void WALWriter::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_.is_open()) {
        flush_buffer();
        file_stream_.close();
    }
}

bool WALWriter::write_entry(const WALLogEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return false;
    }
    
    // 设置序列号
    const_cast<WALLogEntry&>(entry).sequence_number = ++sequence_number_;
    
    // 直接序列化并写入文件，不使用缓冲区
    auto data = entry.serialize();
    
    // 直接写入文件
    file_stream_.write(reinterpret_cast<const char*>(data.data()), data.size());
    
    // 立即刷新到磁盘
    file_stream_.flush();
    
    // 检查写入是否成功
    if (file_stream_.fail()) {
        return false;
    }
    
    total_entries_++;
    total_bytes_ += data.size();
    
    return true;
}

bool WALWriter::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_.is_open()) {
        file_stream_.flush();
        return true;
    }
    
    return false;
}

size_t WALWriter::get_file_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return 0;
    }
    
    // 需要临时移除 const 限制来获取文件位置
    auto& non_const_stream = const_cast<std::ofstream&>(file_stream_);
    auto current_pos = non_const_stream.tellp();
    return static_cast<size_t>(current_pos);
}

WALWriter::WriteStats WALWriter::get_stats() const {
    return {
        total_entries_.load(),
        total_bytes_.load(),
        flush_count_.load(),
        sync_count_.load()
    };
}

bool WALWriter::write_to_buffer(const WALLogEntry& entry) {
    auto data = entry.serialize();
    
    // 如果缓冲区空间不足，先刷新
    if (write_buffer_.size() + data.size() > buffer_size_) {
        if (!flush_buffer()) {
            return false;
        }
    }
    
    // 写入到缓冲区
    write_buffer_.insert(write_buffer_.end(), data.begin(), data.end());
    
    return true;
}

bool WALWriter::flush_buffer() {
    if (write_buffer_.empty()) {
        return true;
    }
    
    file_stream_.write(reinterpret_cast<const char*>(write_buffer_.data()), write_buffer_.size());
    if (!file_stream_.good()) {
        return false;
    }
    
    write_buffer_.clear();
    flush_count_++;
    
    if (sync_mode_) {
        sync_to_disk();
    }
    
    return true;
}

void WALWriter::sync_to_disk() {
    file_stream_.flush();
    sync_count_++;
}

// WALReader 实现

WALReader::WALReader(const std::string& file_path)
    : file_path_(file_path), buffer_size_(64 * 1024),
      total_entries_(0), valid_entries_(0), invalid_entries_(0), total_bytes_(0) {
    read_buffer_.reserve(buffer_size_);
}

WALReader::~WALReader() {
    close();
}

bool WALReader::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_.is_open()) {
        return true;
    }
    
    file_stream_.open(file_path_, std::ios::binary);
    return file_stream_.is_open();
}

void WALReader::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

std::unique_ptr<WALLogEntry> WALReader::read_next_entry() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open() || eof()) {
        return nullptr;
    }
    
    // 读取头部
    WALHeader header;
    file_stream_.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file_stream_.good()) {
        return nullptr;
    }
    
    // 验证魔数和版本
    if (header.magic_number != WAL_MAGIC_NUMBER || header.version != WAL_VERSION) {
        return nullptr;
    }
    
    // 读取操作类型
    uint8_t operation;
    file_stream_.read(reinterpret_cast<char*>(&operation), 1);
    if (!file_stream_.good()) {
        return nullptr;
    }
    
    // 读取 TTL
    int64_t ttl_ms;
    file_stream_.read(reinterpret_cast<char*>(&ttl_ms), sizeof(ttl_ms));
    if (!file_stream_.good()) {
        return nullptr;
    }
    
    // 读取键长度和键
    uint32_t key_len;
    file_stream_.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
    if (!file_stream_.good()) {
        return nullptr;
    }
    
    std::string key(key_len, '\0');
    file_stream_.read(&key[0], key_len);
    if (!file_stream_.good()) {
        return nullptr;
    }
    
    // 读取值长度和值
    uint32_t value_len;
    file_stream_.read(reinterpret_cast<char*>(&value_len), sizeof(value_len));
    if (!file_stream_.good()) {
        return nullptr;
    }
    
    std::string value(value_len, '\0');
    file_stream_.read(&value[0], value_len);
    if (!file_stream_.good()) {
        return nullptr;
    }
    
    // 读取校验和
    uint32_t checksum;
    file_stream_.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
    if (!file_stream_.good()) {
        return nullptr;
    }
    
    // 创建条目
    auto entry = std::make_unique<WALLogEntry>();
    entry->sequence_number = header.sequence_number;
    entry->timestamp = header.timestamp;
    entry->operation = static_cast<WALOperationType>(operation);
    entry->key = std::move(key);
    entry->value = std::move(value);
    entry->ttl_ms = ttl_ms;
    entry->checksum = checksum;
    
    // 验证校验和
    if (!entry->verify_checksum()) {
        invalid_entries_++;
        return nullptr;
    }
    
    total_entries_++;
    valid_entries_++;
    total_bytes_ += entry->size();
    
    return entry;
}

void WALReader::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_.is_open()) {
        file_stream_.seekg(0, std::ios::beg);
    }
}

bool WALReader::seek_to_sequence(uint64_t sequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return false;
    }
    
    reset();
    
    while (!eof()) {
        auto entry = read_next_entry();
        if (!entry) {
            continue;
        }
        
        if (entry->sequence_number >= sequence) {
            // 回退到这个条目的开始位置
            auto current_pos = file_stream_.tellg();
            file_stream_.seekg(current_pos - static_cast<std::streamoff>(entry->size()));
            return true;
        }
    }
    
    return false;
}

bool WALReader::eof() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return true;
    }
    
    // 简单的 EOF 检查
    return file_stream_.eof();
}

size_t WALReader::get_position() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return 0;
    }
    
    // 需要临时移除 const 限制来获取文件位置
    auto& non_const_stream = const_cast<std::ifstream&>(file_stream_);
    return static_cast<size_t>(non_const_stream.tellg());
}

bool WALReader::read_all_entries(EntryCallback callback) {
    reset();
    
    while (!eof()) {
        auto entry = read_next_entry();
        if (!entry) {
            continue;
        }
        
        if (!callback(*entry)) {
            return false;  // 回调返回 false，停止读取
        }
    }
    
    return true;
}

WALReader::ReadStats WALReader::get_stats() const {
    return {
        total_entries_.load(),
        valid_entries_.load(),
        invalid_entries_.load(),
        total_bytes_.load()
    };
}

bool WALReader::read_from_buffer(size_t bytes, std::vector<uint8_t>& data) {
    data.resize(bytes);
    file_stream_.read(reinterpret_cast<char*>(data.data()), bytes);
    return file_stream_.good();
}

// WALManager 实现

WALManager::WALManager(const std::string& wal_dir, size_t max_file_size)
    : wal_dir_(wal_dir), max_file_size_(max_file_size), current_sequence_(0), in_transaction_(false),
      total_entries_(0), total_files_(0), total_size_(0), write_ops_(0), read_ops_(0), flush_ops_(0) {
}

WALManager::~WALManager() {
    if (current_writer_) {
        current_writer_->close();
    }
}

bool WALManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 创建 WAL 目录
    if (!create_wal_directory()) {
        return false;
    }
    
    // 获取当前序列号
    auto wal_files = list_wal_files();
    if (!wal_files.empty()) {
        // 从文件名解析最大的序列号
        uint64_t max_seq = 0;
        for (const auto& file : wal_files) {
            // 文件名格式：wal_<sequence>.log
            size_t pos = file.find("wal_");
            if (pos != std::string::npos) {
                size_t end_pos = file.find(".log");
                if (end_pos != std::string::npos) {
                    std::string seq_str = file.substr(pos + 4, end_pos - pos - 4);
                    uint64_t seq = std::stoull(seq_str);
                    max_seq = std::max(max_seq, seq);
                }
            }
        }
        current_sequence_ = max_seq;
    }
    
    // 创建当前写入器
    std::string current_file = get_current_wal_file();
    current_writer_ = std::make_unique<WALWriter>(current_file);
    
    if (!current_writer_->open()) {
        return false;
    }
    
    return true;
}

bool WALManager::write_set(const std::string& key, const std::string& value, int64_t ttl_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    WALLogEntry entry;
    entry.operation = WALOperationType::SET;
    entry.key = key;
    entry.value = value;
    entry.ttl_ms = ttl_ms;
    entry.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    write_ops_++;
    
    return current_writer_->write_entry(entry);
}

bool WALManager::write_del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    WALLogEntry entry;
    entry.operation = WALOperationType::DEL;
    entry.key = key;
    entry.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    write_ops_++;
    
    return current_writer_->write_entry(entry);
}

bool WALManager::write_clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    WALLogEntry entry;
    entry.operation = WALOperationType::CLEAR;
    entry.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    write_ops_++;
    
    return current_writer_->write_entry(entry);
}

bool WALManager::begin_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (in_transaction_) {
        return false;
    }
    
    WALLogEntry entry;
    entry.operation = WALOperationType::BEGIN;
    entry.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    in_transaction_ = current_writer_->write_entry(entry);
    return in_transaction_;
}

bool WALManager::commit_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!in_transaction_) {
        return false;
    }
    
    WALLogEntry entry;
    entry.operation = WALOperationType::COMMIT;
    entry.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    bool result = current_writer_->write_entry(entry);
    if (result) {
        in_transaction_ = false;
    }
    
    return result;
}

bool WALManager::rollback_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!in_transaction_) {
        return false;
    }
    
    WALLogEntry entry;
    entry.operation = WALOperationType::ROLLBACK;
    entry.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    bool result = current_writer_->write_entry(entry);
    if (result) {
        in_transaction_ = false;
    }
    
    return result;
}

bool WALManager::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!current_writer_) {
        return false;
    }
    
    flush_ops_++;
    return current_writer_->flush();
}

bool WALManager::replay(StorageEngine& storage) {
    auto wal_files = list_wal_files();
    
    for (const auto& file : wal_files) {
        std::string file_path = wal_dir_ + "/" + file;
        WALReader reader(file_path);
        
        if (!reader.open()) {
            continue;
        }
        
        bool success = reader.read_all_entries([&storage](const WALLogEntry& entry) {
            switch (entry.operation) {
                case WALOperationType::SET:
                    storage.set(entry.key, entry.value, entry.ttl_ms);
                    break;
                case WALOperationType::DEL:
                    storage.del(entry.key);
                    break;
                case WALOperationType::CLEAR:
                    storage.clear();
                    break;
                case WALOperationType::BEGIN:
                case WALOperationType::COMMIT:
                case WALOperationType::ROLLBACK:
                    // 事务操作暂时忽略
                    break;
            }
            return true;
        });
        
        reader.close();
        
        if (!success) {
            return false;
        }
    }
    
    return true;
}

bool WALManager::cleanup_old_files() {
    auto wal_files = list_wal_files();
    
    // 保留最近的几个文件
    const size_t max_files = 5;
    if (wal_files.size() <= max_files) {
        return true;
    }
    
    // 删除最旧的文件
    for (size_t i = 0; i < wal_files.size() - max_files; ++i) {
        std::string file_path = wal_dir_ + "/" + wal_files[i];
        std::filesystem::remove(file_path);
    }
    
    return true;
}

uint64_t WALManager::get_sequence_number() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (current_writer_) {
        return current_writer_->get_sequence_number();
    }
    
    return current_sequence_;
}

WALManager::WALStats WALManager::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    return {
        total_entries_.load(),
        total_files_.load(),
        total_size_.load(),
        write_ops_.load(),
        read_ops_.load(),
        flush_ops_.load()
    };
}

std::string WALManager::get_current_wal_file() const {
    return get_wal_file_path(current_sequence_);
}

std::string WALManager::get_wal_file_path(uint64_t sequence) const {
    return wal_dir_ + "/wal_" + std::to_string(sequence) + ".log";
}

bool WALManager::rotate_wal_file() {
    if (!current_writer_) {
        return false;
    }
    
    // 检查当前文件大小
    if (current_writer_->get_file_size() < max_file_size_) {
        return true;
    }
    
    // 关闭当前文件
    current_writer_->close();
    
    // 创建新文件
    current_sequence_++;
    std::string new_file = get_current_wal_file();
    current_writer_ = std::make_unique<WALWriter>(new_file);
    
    return current_writer_->open();
}

std::vector<std::string> WALManager::list_wal_files() const {
    std::vector<std::string> files;
    
    if (!std::filesystem::exists(wal_dir_)) {
        return files;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(wal_dir_)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            if (filename.find("wal_") == 0 && filename.find(".log") != std::string::npos) {
                files.push_back(filename);
            }
        }
    }
    
    std::sort(files.begin(), files.end());
    return files;
}

bool WALManager::create_wal_directory() {
    return std::filesystem::create_directories(wal_dir_);
}
