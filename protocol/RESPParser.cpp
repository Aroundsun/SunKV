#include "RESPParser.h"
#include <climits>

RESPParser::RESPParser() 
    : state_(ParseState::START),
      bulk_size_(0),
      processed_bytes_(0) {
}

void RESPParser::reset() {
    state_ = ParseState::START;
    temp_data_.clear();
    bulk_size_ = 0;
    processed_bytes_ = 0;
    current_value_.reset();
    array_stack_.clear();
}

ParseResult RESPParser::parse(std::string_view data) {
    if (data.empty()) {
        return ParseResult::makeIncomplete(0);
    }
    
    size_t pos = 0;
    
    while (pos < data.size()) {
        ParseResult step = ParseResult::makeIncomplete(processed_bytes_);
        switch (state_) {
            case ParseState::START: {
                if (pos >= data.size()) {
                    return ParseResult::makeIncomplete(processed_bytes_);
                }
                
                char type_char = data[pos];
                pos++;
                processed_bytes_++;
                
                switch (type_char) {
                    case '+':
                        state_ = ParseState::SIMPLE_STRING;
                        temp_data_.clear();
                        break;
                    case '-':
                        state_ = ParseState::ERROR;
                        temp_data_.clear();
                        break;
                    case ':':
                        state_ = ParseState::INTEGER;
                        temp_data_.clear();
                        break;
                    case '$':
                        state_ = ParseState::BULK_STRING_SIZE;
                        temp_data_.clear();
                        break;
                    case '*':
                        state_ = ParseState::ARRAY_SIZE;
                        temp_data_.clear();
                        break;
                    default:
                        return ParseResult::makeError("Invalid RESP type: " + std::string(1, type_char));
                }
                break;
            }
            
            case ParseState::SIMPLE_STRING:
                step = parseSimpleString(data, pos);
                break;
                
            case ParseState::ERROR:
                step = parseError(data, pos);
                break;
                
            case ParseState::INTEGER:
                step = parseInteger(data, pos);
                break;
                
            case ParseState::BULK_STRING_SIZE:
                step = parseBulkStringSize(data, pos);
                break;
                
            case ParseState::BULK_STRING_DATA:
                step = parseBulkStringData(data, pos);
                break;
                
            case ParseState::ARRAY_SIZE:
                step = parseArraySize(data, pos);
                break;
                
            case ParseState::ARRAY_ELEMENT:
                step = parseArrayElement(data, pos);
                break;
        }

        if (!step.success || step.complete) {
            return step;
        }
    }
    
    // 必须返回自本次 parse() 调用起、在输入缓冲内已消费的字节数（processed_bytes_），供上层 pipeline 正确推进偏移。
    // 数组由 array_stack_ 迭代解析，非递归；processed_bytes_ 在各子解析步骤中累加，避免半包/粘包时偏移错乱。
    return ParseResult::makeIncomplete(processed_bytes_);
}

ParseResult RESPParser::parseSimpleString(std::string_view data, size_t& pos) {
    size_t crlf_pos;
    if (!findCRLF(data, pos, crlf_pos)) {
        return ParseResult::makeIncomplete(processed_bytes_);
    }
    
    std::string value{data.substr(pos, crlf_pos - pos)};
    current_value_ = makeSimpleString(value);
    
    size_t total_processed = crlf_pos + 2 - pos;
    pos = crlf_pos + 2;
    processed_bytes_ += total_processed;
    
    // 检查是否在数组中
    if (!array_stack_.empty()) {
        auto& ctx = array_stack_.back();
        ctx.elements.push_back(current_value_);
        ctx.count++;
        
        if (ctx.count >= ctx.size) {
            // 数组解析完成
            current_value_ = makeArray(ctx.elements);
            array_stack_.pop_back();
        } else {
            // 数组尚未完成，继续由 parse() 主循环推进
            state_ = ParseState::START;
            return ParseResult::makeIncomplete(processed_bytes_);
        }
    }
    
    state_ = ParseState::START;
    return ParseResult::makeSuccess(current_value_, processed_bytes_);
}

ParseResult RESPParser::parseError(std::string_view data, size_t& pos) {
    size_t crlf_pos;
    if (!findCRLF(data, pos, crlf_pos)) {
        return ParseResult::makeIncomplete(processed_bytes_);
    }
    
    std::string message{data.substr(pos, crlf_pos - pos)};
    current_value_ = makeError(message);
    
    size_t total_processed = crlf_pos + 2 - pos;
    pos = crlf_pos + 2;
    processed_bytes_ += total_processed;
    
    // 检查是否在数组中
    if (!array_stack_.empty()) {
        auto& ctx = array_stack_.back();
        ctx.elements.push_back(current_value_);
        ctx.count++;
        
        if (ctx.count >= ctx.size) {
            // 数组解析完成
            current_value_ = makeArray(ctx.elements);
            array_stack_.pop_back();
        } else {
            // 数组尚未完成，继续由 parse() 主循环推进
            state_ = ParseState::START;
            return ParseResult::makeIncomplete(processed_bytes_);
        }
    }
    
    state_ = ParseState::START;
    return ParseResult::makeSuccess(current_value_, processed_bytes_);
}

ParseResult RESPParser::parseInteger(std::string_view data, size_t& pos) {
    size_t crlf_pos;
    if (!findCRLF(data, pos, crlf_pos)) {
        return ParseResult::makeIncomplete(processed_bytes_);
    }
    
    int64_t value = parseInteger(data, pos, crlf_pos);
    if (value == LLONG_MAX) {
        return ParseResult::makeError("Invalid integer format");
    }
    
    current_value_ = makeInteger(value);
    
    size_t total_processed = crlf_pos + 2 - pos;
    pos = crlf_pos + 2;
    processed_bytes_ += total_processed;
    
    // 检查是否在数组中
    if (!array_stack_.empty()) {
        auto& ctx = array_stack_.back();
        ctx.elements.push_back(current_value_);
        ctx.count++;
        
        if (ctx.count >= ctx.size) {
            // 数组解析完成
            current_value_ = makeArray(ctx.elements);
            array_stack_.pop_back();
        } else {
            // 数组尚未完成，继续由 parse() 主循环推进
            state_ = ParseState::START;
            return ParseResult::makeIncomplete(processed_bytes_);
        }
    }
    
    state_ = ParseState::START;
    return ParseResult::makeSuccess(current_value_, processed_bytes_);
}

ParseResult RESPParser::parseBulkStringSize(std::string_view data, size_t& pos) {
    size_t crlf_pos;
    if (!findCRLF(data, pos, crlf_pos)) {
        return ParseResult::makeIncomplete(processed_bytes_);
    }
    
    int64_t size = parseInteger(data, pos, crlf_pos);
    if (size == LLONG_MAX) {
        return ParseResult::makeError("Invalid bulk string size");
    }
    if (size < -1) {
        // Redis 语义：bulk string 长度只能为 -1 (null) 或 >= 0
        return ParseResult::makeError("Invalid bulk string size");
    }
    
    bulk_size_ = size;
    size_t total_processed = crlf_pos + 2 - pos;
    pos = crlf_pos + 2;
    processed_bytes_ += total_processed;
    
    if (size == -1) {
        // 空批量字符串
        current_value_ = makeNullBulkString();
        
        // 检查是否在数组中
        if (!array_stack_.empty()) {
            auto& ctx = array_stack_.back();
            ctx.elements.push_back(current_value_);
            ctx.count++;
            
            if (ctx.count >= ctx.size) {
                // 数组解析完成
                current_value_ = makeArray(ctx.elements);
                array_stack_.pop_back();
            } else {
                // 数组尚未完成，继续由 parse() 主循环推进
                state_ = ParseState::START;
                return ParseResult::makeIncomplete(processed_bytes_);
            }
        }
        
        state_ = ParseState::START;
        return ParseResult::makeSuccess(current_value_, processed_bytes_);
    } else {
        // 下一步继续解析批量字符串 body
        state_ = ParseState::BULK_STRING_DATA;
        temp_data_.clear();
        return ParseResult::makeIncomplete(processed_bytes_);
    }
}

ParseResult RESPParser::parseBulkStringData(std::string_view data, size_t& pos) {
    size_t remaining = bulk_size_ - temp_data_.length();
    size_t available = data.length() - pos;
    
    if (available < remaining) {
        // 数据不足
        temp_data_ += data.substr(pos);
        pos = data.length();
        processed_bytes_ += available;
        return ParseResult::makeIncomplete(processed_bytes_);
    } else {
        // 数据足够
        temp_data_ += data.substr(pos, remaining);
        pos += remaining;
        processed_bytes_ += remaining;
        
        // 检查是否有 CRLF
        if (pos + 2 > data.length()) {
            return ParseResult::makeIncomplete(processed_bytes_);
        }
        
        if (data[pos] != '\r' || data[pos + 1] != '\n') {
            return ParseResult::makeError("Missing CRLF after bulk string data");
        }
        
        pos += 2;
        processed_bytes_ += 2;
        
        current_value_ = makeBulkString(temp_data_);
        
        // 检查是否在数组中
        if (!array_stack_.empty()) {
            auto& ctx = array_stack_.back();
            ctx.elements.push_back(current_value_);
            ctx.count++;
            
            if (ctx.count >= ctx.size) {
                // 数组解析完成
                current_value_ = makeArray(ctx.elements);
                array_stack_.pop_back();
            } else {
                // 数组尚未完成，继续由 parse() 主循环推进
                state_ = ParseState::START;
                return ParseResult::makeIncomplete(processed_bytes_);
            }
        }
        
        state_ = ParseState::START;
        return ParseResult::makeSuccess(current_value_, processed_bytes_);
    }
}

ParseResult RESPParser::parseArraySize(std::string_view data, size_t& pos) {
    size_t crlf_pos;
    if (!findCRLF(data, pos, crlf_pos)) {
        return ParseResult::makeIncomplete(processed_bytes_);
    }
    
    int64_t size = parseInteger(data, pos, crlf_pos);
    if (size == LLONG_MAX) {
        return ParseResult::makeError("Invalid array size");
    }
    if (size < -1) {
        // Redis 语义：array 长度只能为 -1 (null) 或 >= 0
        return ParseResult::makeError("Invalid array size");
    }
    
    size_t total_processed = crlf_pos + 2 - pos;
    pos = crlf_pos + 2;
    processed_bytes_ += total_processed;
    
    if (size == -1) {
        // 空数组
        current_value_ = makeNullArray();
        
        // 检查是否在嵌套数组中
        if (!array_stack_.empty()) {
            auto& ctx = array_stack_.back();
            ctx.elements.push_back(current_value_);
            ctx.count++;
            
            if (ctx.count >= ctx.size) {
                // 父数组解析完成
                current_value_ = makeArray(ctx.elements);
                array_stack_.pop_back();
            } else {
                // 数组尚未完成，继续由 parse() 主循环推进
                state_ = ParseState::START;
                return ParseResult::makeIncomplete(processed_bytes_);
            }
        }
        
        state_ = ParseState::START;
        return ParseResult::makeSuccess(current_value_, processed_bytes_);
    } else if (size == 0) {
        // 空数组
        current_value_ = makeArray({});
        
        // 检查是否在嵌套数组中
        if (!array_stack_.empty()) {
            auto& ctx = array_stack_.back();
            ctx.elements.push_back(current_value_);
            ctx.count++;
            
            if (ctx.count >= ctx.size) {
                // 父数组解析完成
                current_value_ = makeArray(ctx.elements);
                array_stack_.pop_back();
            } else {
                // 数组尚未完成，继续由 parse() 主循环推进
                state_ = ParseState::START;
                return ParseResult::makeIncomplete(processed_bytes_);
            }
        }
        
        state_ = ParseState::START;
        return ParseResult::makeSuccess(current_value_, processed_bytes_);
    } else {
        // 创建新的数组上下文
        ArrayContext ctx;
        ctx.size = static_cast<size_t>(size);
        ctx.count = 0;
        ctx.elements.reserve(ctx.size);
        
        if (array_stack_.size() >= kMaxNestingDepth_) {
            return ParseResult::makeError("RESP nesting too deep");
        }
        array_stack_.push_back(ctx);
        state_ = ParseState::START;
        return ParseResult::makeIncomplete(processed_bytes_);
    }
}

ParseResult RESPParser::parseArrayElement(std::string_view data, size_t& pos) {
    (void)data;
    (void)pos;
    // 这个状态实际上不会直接使用，而是通过 START 状态来处理。
    state_ = ParseState::START;
    return ParseResult::makeIncomplete(processed_bytes_);
}

bool RESPParser::findCRLF(std::string_view data, size_t pos, size_t& crlf_pos) {
    crlf_pos = data.find("\r\n", pos);
    return crlf_pos != std::string::npos;
}

int64_t RESPParser::parseInteger(std::string_view data, size_t start, size_t end) {
    if (start >= end) {
        return LLONG_MAX;
    }
    
    bool negative = false;
    if (data[start] == '-') {
        negative = true;
        start++;
    }
    
    int64_t value = 0;
    for (size_t i = start; i < end; ++i) {
        char c = data[i];
        if (c < '0' || c > '9') {
            return LLONG_MAX;
        }
        
        int digit = c - '0';
        
        // 更安全的溢出检查
        if (value > (LLONG_MAX - digit) / 10) {
            return LLONG_MAX;
        }
        
        value = value * 10 + digit;
    }
    
    return negative ? -value : value;
}
