#include "Snapshot.h"
#include "../common/DataValue.h"  // 数据值定义
#include "../network/logger.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <list>
#include <set>
#include <iostream>

// CRC32 校验和计算
static uint32_t calculate_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

// SnapshotEntry 实现
SnapshotEntry::SnapshotEntry() 
    : type(SnapshotEntryType::DATA), ttl_ms(-1), timestamp(0), checksum(0) {
}

SnapshotEntry::SnapshotEntry(SnapshotEntryType t, const std::string& k, const std::string& v, int64_t ttl)
    : type(t), key(k), value(v), ttl_ms(ttl), 
      timestamp(std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count()) {
    update_checksum();
}

std::vector<uint8_t> SnapshotEntry::serialize() const {
    std::vector<uint8_t> data;
    
    // 条目头
    SnapshotEntryHeader header;
    header.type = static_cast<uint8_t>(type);
    header.key_length = key.length();
    header.value_length = value.length();
    header.ttl_ms = ttl_ms;
    header.timestamp = timestamp;
    header.checksum = calculate_checksum();
    
    // 写入头部
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), header_bytes, header_bytes + sizeof(header));
    
    // 写入键
    data.insert(data.end(), key.begin(), key.end());
    
    // 写入值
    data.insert(data.end(), value.begin(), value.end());
    
    return data;
}

std::unique_ptr<SnapshotEntry> SnapshotEntry::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(SnapshotEntryHeader)) {
        return nullptr;
    }
    
    // 读取头部
    SnapshotEntryHeader header;
    std::memcpy(&header, data.data(), sizeof(header));
    
    // 检查数据长度
    size_t expected_size = sizeof(header) + header.key_length + header.value_length;
    if (data.size() < expected_size) {
        return nullptr;
    }
    
    // 创建条目
    auto entry = std::make_unique<SnapshotEntry>();
    entry->type = static_cast<SnapshotEntryType>(header.type);
    entry->ttl_ms = header.ttl_ms;
    entry->timestamp = header.timestamp;
    entry->checksum = header.checksum;
    
    // 读取键和值
    const uint8_t* key_start = data.data() + sizeof(header);
    entry->key.assign(reinterpret_cast<const char*>(key_start), header.key_length);
    
    const uint8_t* value_start = key_start + header.key_length;
    entry->value.assign(reinterpret_cast<const char*>(value_start), header.value_length);
    
    // 验证校验和
    if (!entry->verify_checksum()) {
        return nullptr;
    }
    
    return entry;
}

uint32_t SnapshotEntry::calculate_checksum() const {
    std::vector<uint8_t> data;
    
    // 类型
    data.push_back(static_cast<uint8_t>(type));
    
    // TTL 和时间戳
    const uint8_t* ttl_bytes = reinterpret_cast<const uint8_t*>(&ttl_ms);
    data.insert(data.end(), ttl_bytes, ttl_bytes + sizeof(ttl_ms));
    
    const uint8_t* timestamp_bytes = reinterpret_cast<const uint8_t*>(&timestamp);
    data.insert(data.end(), timestamp_bytes, timestamp_bytes + sizeof(timestamp));
    
    // 键长度和键
    uint32_t key_len = key.length();
    const uint8_t* key_len_bytes = reinterpret_cast<const uint8_t*>(&key_len);
    data.insert(data.end(), key_len_bytes, key_len_bytes + sizeof(key_len));
    data.insert(data.end(), key.begin(), key.end());
    
    // 值长度和值
    uint32_t value_len = value.length();
    const uint8_t* value_len_bytes = reinterpret_cast<const uint8_t*>(&value_len);
    data.insert(data.end(), value_len_bytes, value_len_bytes + sizeof(value_len));
    data.insert(data.end(), value.begin(), value.end());
    
    return calculate_crc32(data.data(), data.size());
}

bool SnapshotEntry::verify_checksum() const {
    return calculate_checksum() == checksum;
}

size_t SnapshotEntry::size() const {
    return sizeof(SnapshotEntryHeader) + key.length() + value.length();
}

void SnapshotEntry::update_checksum() {
    checksum = calculate_checksum();
}

// SnapshotWriter 实现
SnapshotWriter::SnapshotWriter(const std::string& file_path) 
    : file_path_(file_path) {
}

SnapshotWriter::~SnapshotWriter() {
    close();
}

bool SnapshotWriter::open() {
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
    
    file_stream_.open(file_path_, std::ios::binary);
    if (!file_stream_.is_open()) {
        return false;
    }
    
    // 写入占位符头部
    SnapshotHeader header{};
    file_stream_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (file_stream_.fail()) {
        file_stream_.close();
        return false;
    }
    
    // 强制刷新
    file_stream_.flush();
    
    start_timestamp_ = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    return true;
}

void SnapshotWriter::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_.is_open()) {
        update_header();
        file_stream_.close();
    }
}

bool SnapshotWriter::write_data(const std::string& key, const std::string& value, int64_t ttl_ms) {
    // 直接内联 write_entry 的逻辑
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return false;
    }
    
    SnapshotEntry entry(SnapshotEntryType::DATA, key, value, ttl_ms);
    auto data = entry.serialize();
    
    file_stream_.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (file_stream_.fail()) {
        return false;
    }
    
    // 强制刷新到磁盘
    file_stream_.flush();
    if (file_stream_.fail()) {
        return false;
    }
    
    entries_written_++;
    bytes_written_ += data.size();
    sequence_number_++;
    
    // 更新统计
    switch (entry.type) {
        case SnapshotEntryType::DATA:
            data_entries_++;
            break;
        case SnapshotEntryType::DELETED:
            deleted_entries_++;
            break;
        case SnapshotEntryType::METADATA:
            metadata_entries_++;
            break;
        case SnapshotEntryType::TTL:
            break;
    }
    
    return true;
}

bool SnapshotWriter::write_deleted(const std::string& key) {
    // 直接内联 write_entry 的逻辑
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return false;
    }
    
    SnapshotEntry entry(SnapshotEntryType::DELETED, key, "", -1);
    auto data = entry.serialize();
    
    file_stream_.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (file_stream_.fail()) {
        return false;
    }
    
    // 强制刷新到磁盘
    file_stream_.flush();
    if (file_stream_.fail()) {
        return false;
    }
    
    entries_written_++;
    bytes_written_ += data.size();
    sequence_number_++;
    
    // 更新统计
    switch (entry.type) {
        case SnapshotEntryType::DATA:
            data_entries_++;
            break;
        case SnapshotEntryType::DELETED:
            deleted_entries_++;
            break;
        case SnapshotEntryType::METADATA:
            metadata_entries_++;
            break;
        case SnapshotEntryType::TTL:
            break;
    }
    
    return true;
}

bool SnapshotWriter::write_metadata(const std::string& key, const std::string& value) {
    // 直接内联 write_entry 的逻辑
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return false;
    }
    
    SnapshotEntry entry(SnapshotEntryType::METADATA, key, value, -1);
    auto data = entry.serialize();
    
    file_stream_.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (file_stream_.fail()) {
        return false;
    }
    
    // 强制刷新到磁盘
    file_stream_.flush();
    if (file_stream_.fail()) {
        return false;
    }
    
    entries_written_++;
    bytes_written_ += data.size();
    sequence_number_++;
    
    // 更新统计
    switch (entry.type) {
        case SnapshotEntryType::DATA:
            data_entries_++;
            break;
        case SnapshotEntryType::DELETED:
            deleted_entries_++;
            break;
        case SnapshotEntryType::METADATA:
            metadata_entries_++;
            break;
        case SnapshotEntryType::TTL:
            break;
    }
    
    return true;
}

bool SnapshotWriter::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return false;
    }
    
    file_stream_.flush();
    return !file_stream_.fail();
}

SnapshotWriter::WriteStats SnapshotWriter::get_stats() const {
    return {
        entries_written_.load(),
        bytes_written_.load(),
        data_entries_.load(),
        deleted_entries_.load(),
        metadata_entries_.load()
    };
}

bool SnapshotWriter::write_entry(const SnapshotEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return false;
    }
    
    auto data = entry.serialize();
    
    file_stream_.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (file_stream_.fail()) {
        return false;
    }
    
    // 强制刷新到磁盘
    file_stream_.flush();
    if (file_stream_.fail()) {
        return false;
    }
    
    entries_written_++;
    bytes_written_ += data.size();
    sequence_number_++;
    
    // 更新统计
    switch (entry.type) {
        case SnapshotEntryType::DATA:
            data_entries_++;
            break;
        case SnapshotEntryType::DELETED:
            deleted_entries_++;
            break;
        case SnapshotEntryType::METADATA:
            metadata_entries_++;
            break;
        case SnapshotEntryType::TTL:
            break;
    }
    
    return true;
}

void SnapshotWriter::write_header() {
    SnapshotHeader header;
    header.magic_number = SNAPSHOT_MAGIC_NUMBER;
    header.version = SNAPSHOT_VERSION;
    header.timestamp = start_timestamp_;
    header.sequence_number = sequence_number_;
    header.entry_count = entries_written_;
    header.checksum = 0;
    
    // 计算头部校验和
    std::vector<uint8_t> header_data;
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header);
    header_data.insert(header_data.end(), header_bytes, header_bytes + sizeof(header) - sizeof(uint32_t));
    header.checksum = calculate_crc32(header_data.data(), header_data.size());
    
    // 写入头部
    file_stream_.seekp(0);
    file_stream_.write(reinterpret_cast<const char*>(&header), sizeof(header));
}

void SnapshotWriter::update_header() {
    write_header();
}

// SnapshotReader 实现
SnapshotReader::SnapshotReader(const std::string& file_path) 
    : file_path_(file_path), header_read_(false) {
}

SnapshotReader::~SnapshotReader() {
    close();
}

bool SnapshotReader::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_.is_open()) {
        return true;
    }
    
    file_stream_.open(file_path_, std::ios::binary);
    if (!file_stream_.is_open()) {
        return false;
    }
    
    return read_header();
}

void SnapshotReader::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

std::unique_ptr<SnapshotEntry> SnapshotReader::read_next_entry() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return nullptr;
    }
    
    auto current_pos = file_stream_.tellg();
    file_stream_.seekg(0, std::ios::end);
    auto file_size = file_stream_.tellg();
    file_stream_.seekg(current_pos);
    
    if (current_pos >= file_size) {
        return nullptr;
    }
    
    if (static_cast<size_t>(file_size - current_pos) < sizeof(SnapshotEntryHeader)) {
        return nullptr;
    }
    
    SnapshotEntryHeader header;
    file_stream_.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (file_stream_.eof() || !file_stream_.good()) {
        return nullptr;
    }
    
    if (header.key_length > 1024 || header.value_length > 1024 * 1024) {
        return nullptr;
    }
    
    std::string key(header.key_length, '\0');
    file_stream_.read(&key[0], header.key_length);
    if (file_stream_.eof() || !file_stream_.good()) {
        return nullptr;
    }
    
    std::string value(header.value_length, '\0');
    file_stream_.read(&value[0], header.value_length);
    if (file_stream_.eof() || !file_stream_.good()) {
        return nullptr;
    }
    
    auto entry = std::make_unique<SnapshotEntry>();
    entry->type = static_cast<SnapshotEntryType>(header.type);
    entry->key = key;
    entry->value = value;
    entry->ttl_ms = header.ttl_ms;
    entry->timestamp = header.timestamp;
    entry->checksum = header.checksum;
    
    if (!entry->verify_checksum()) {
        invalid_entries_++;
        return nullptr;
    }
    
    entries_read_++;
    bytes_read_ += entry->size();
    valid_entries_++;
    
    switch (entry->type) {
        case SnapshotEntryType::DATA:
            data_entries_++;
            break;
        case SnapshotEntryType::DELETED:
            deleted_entries_++;
            break;
        case SnapshotEntryType::METADATA:
            metadata_entries_++;
            break;
        case SnapshotEntryType::TTL:
            break;
    }
    
    return entry;
}

bool SnapshotReader::eof() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return true;
    }
    
    return file_stream_.eof();
}

void SnapshotReader::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_.is_open()) {
        file_stream_.seekg(0);
        read_header();
    }
}

size_t SnapshotReader::get_position() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_stream_.is_open()) {
        return 0;
    }
    
    auto& non_const_stream = const_cast<std::ifstream&>(file_stream_);
    return static_cast<size_t>(non_const_stream.tellg());
}

SnapshotReader::ReadStats SnapshotReader::get_stats() const {
    return {
        entries_read_.load(),
        bytes_read_.load(),
        valid_entries_.load(),
        invalid_entries_.load(),
        data_entries_.load(),
        deleted_entries_.load(),
        metadata_entries_.load()
    };
}

SnapshotReader::SnapshotInfo SnapshotReader::get_snapshot_info() const {
    return {
        header_.timestamp,
        header_.sequence_number,
        header_.entry_count
    };
}

bool SnapshotReader::read_header() {
    if (header_read_) {
        return true;
    }
    
    file_stream_.read(reinterpret_cast<char*>(&header_), sizeof(header_));
    if (file_stream_.fail()) {
        return false;
    }
    
    if (header_.magic_number != SNAPSHOT_MAGIC_NUMBER || header_.version != SNAPSHOT_VERSION) {
        return false;
    }
    
    header_read_ = true;
    return true;
}

bool SnapshotReader::read_from_buffer(size_t bytes, std::vector<uint8_t>& data) {
    data.resize(bytes);
    file_stream_.read(reinterpret_cast<char*>(data.data()), bytes);
    return file_stream_.good();
}

// SnapshotManager 实现
SnapshotManager::SnapshotManager(const std::string& snapshot_dir, size_t max_file_size) 
    : snapshot_dir_(snapshot_dir), max_file_size_(max_file_size) {
}

SnapshotManager::~SnapshotManager() {
}

bool SnapshotManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    return create_snapshot_directory();
}

bool SnapshotManager::create_snapshot(const std::map<std::string, std::string>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string filename = generate_snapshot_filename();
    SnapshotWriter writer(filename);
    
    if (!writer.open()) {
        return false;
    }
    
    for (const auto& pair : data) {
        if (!writer.write_data(pair.first, pair.second)) {
            return false;
        }
    }
    
    writer.close();
    return true;
}

bool SnapshotManager::create_multi_type_snapshot(const std::map<std::string, DataValue>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string filename = generate_snapshot_filename();
    SnapshotWriter writer(filename);
    
    if (!writer.open()) {
        return false;
    }
    
    for (const auto& pair : data) {
        // 将 DataValue 转换为字符串存储
        std::string value_str;
        switch (pair.second.type) {
            case DataType::STRING:
                value_str = pair.second.string_value;
                break;
            case DataType::LIST:
                // 简单地将列表元素用逗号连接
                for (const auto& item : pair.second.list_value) {
                    if (!value_str.empty()) value_str += ",";
                    value_str += item;
                }
                break;
            case DataType::SET:
                // 简单地将集合元素用逗号连接
                for (const auto& item : pair.second.set_value) {
                    if (!value_str.empty()) value_str += ",";
                    value_str += item;
                }
                break;
            case DataType::HASH:
                // 简单地将哈希键值对用逗号连接
                for (const auto& [k, v] : pair.second.hash_value) {
                    if (!value_str.empty()) value_str += ",";
                    value_str += k + ":" + v;
                }
                break;
        }
        
        if (!writer.write_data(pair.first, value_str)) {
            return false;
        }
    }
    
    writer.close();
    return true;
}

bool SnapshotManager::load_snapshot(std::map<std::string, std::string>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string latest_snapshot = get_latest_snapshot();
    if (latest_snapshot.empty()) {
        return false;
    }
    
    SnapshotReader reader(latest_snapshot);
    if (!reader.open()) {
        return false;
    }
    
    data.clear();
    
    int max_entries = 1000;  // 防止无限循环
    int entry_count = 0;
    
    while (entry_count < max_entries) {
        // 检查文件位置是否在文件末尾
        auto current_pos = reader.get_position();
        std::ifstream file_stream(latest_snapshot, std::ios::binary | std::ios::ate);
        auto file_size = file_stream.tellg();
        file_stream.close();
        
        if (current_pos >= file_size) {
            LOG_DEBUG("Snapshot: Reached end of file at position {}", static_cast<long long>(current_pos));
            break;
        }
        
        auto entry = reader.read_next_entry();
        if (!entry) {
            LOG_DEBUG("Snapshot: Failed to read entry at position {}", static_cast<long long>(current_pos));
            break;
        }
        
        entry_count++;
        LOG_DEBUG("Snapshot: Read entry #{} type={} key={}", entry_count, static_cast<int>(entry->type), entry->key);
        
        switch (entry->type) {
        case SnapshotEntryType::DATA:
            data[entry->key] = entry->value;
            LOG_DEBUG("Snapshot: DATA {}={}", entry->key, entry->value);
            break;
        case SnapshotEntryType::DELETED:
            data.erase(entry->key);
            LOG_DEBUG("Snapshot: DELETED {}", entry->key);
            break;
        case SnapshotEntryType::METADATA:
            LOG_DEBUG("Snapshot: METADATA entry");
            break;
        }
    }
    
    if (entry_count >= max_entries) {
        LOG_DEBUG("Snapshot: Reached maximum entry limit ({})", max_entries);
    }
    
    LOG_DEBUG("Snapshot: Read {} entries total", entry_count);
    return true;
}

std::string SnapshotManager::get_latest_snapshot() const {
    auto files = list_snapshot_files();
    if (files.empty()) {
        return "";
    }
    
    return files.back();
}

void SnapshotManager::cleanup_old_snapshots(size_t keep_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto files = list_snapshot_files();
    if (files.size() <= keep_count) {
        return;
    }
    
    for (size_t i = 0; i < files.size() - keep_count; ++i) {
        std::filesystem::remove(files[i]);
    }
}

SnapshotManager::SnapshotStats SnapshotManager::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto files = list_snapshot_files();
    size_t total_size = 0;
    
    for (const auto& file : files) {
        if (std::filesystem::exists(file)) {
            total_size += std::filesystem::file_size(file);
        }
    }
    
    std::string latest = get_latest_snapshot();
    size_t latest_size = 0;
    if (!latest.empty() && std::filesystem::exists(latest)) {
        latest_size = std::filesystem::file_size(latest);
    }
    
    return {
        files.size(),
        total_size,
        latest_size,
        latest
    };
}

bool SnapshotManager::create_snapshot_directory() {
    std::error_code ec;
    std::filesystem::create_directories(snapshot_dir_, ec);
    return !ec;  // 如果没有错误则返回 true
}

std::vector<std::string> SnapshotManager::list_snapshot_files() const {
    std::vector<std::string> files;
    
    if (!std::filesystem::exists(snapshot_dir_)) {
        return files;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(snapshot_dir_)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            if (filename.find("snapshot_") == 0 && filename.find(".snap") != std::string::npos) {
                files.push_back(entry.path().string());
            }
        }
    }
    
    std::sort(files.begin(), files.end());
    return files;
}

std::string SnapshotManager::generate_snapshot_filename() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << "snapshot_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    ss << "_" << current_sequence_.load() << ".snap";
    
    return snapshot_dir_ + "/" + ss.str();
}

bool SnapshotManager::compress_snapshot(const std::string& input_file, const std::string& output_file) {
    return std::filesystem::copy_file(input_file, output_file);
}

bool SnapshotManager::decompress_snapshot(const std::string& input_file, const std::string& output_file) {
    return std::filesystem::copy_file(input_file, output_file);
}
