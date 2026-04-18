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
