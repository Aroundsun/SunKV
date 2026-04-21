#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../protocol/RESPType.h"

class Server;
class TcpConnection;

bool dispatchArrayCommandsLookup(Server& server,
                                 const std::shared_ptr<TcpConnection>& conn,
                                 const std::string& cmd_name,
                                 const std::vector<RESPValue::Ptr>& cmd_array);

// 执行数组命令并返回“恰好一个 RESP 值”的原始字节串（不直接写 socket）。
// 事务 EXEC 需要用它把多条命令结果聚合为一个 RESP 数组。
std::string executeArrayCommandToResp(Server& server,
                                      const std::string& cmd_name,
                                      const std::vector<RESPValue::Ptr>& cmd_array);
