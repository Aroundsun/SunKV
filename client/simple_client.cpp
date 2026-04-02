#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>

class SimpleRedisClient {
private:
    int sock_fd;
    std::string server_host;
    int server_port;
    
public:
    SimpleRedisClient(const std::string& host, int port) 
        : sock_fd(-1), server_host(host), server_port(port) {}
    
    ~SimpleRedisClient() {
        disconnect();
    }
    
    bool connect() {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        
        if (inet_pton(AF_INET, server_host.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid server address" << std::endl;
            close(sock_fd);
            sock_fd = -1;
            return false;
        }
        
        if (::connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Failed to connect to server" << std::endl;
            close(sock_fd);
            sock_fd = -1;
            return false;
        }
        
        std::cout << "Connected to SunKV server at " << server_host << ":" << server_port << std::endl;
        return true;
    }
    
    void disconnect() {
        if (sock_fd >= 0) {
            close(sock_fd);
            sock_fd = -1;
        }
    }
    
    std::string send_command(const std::string& command) {
        if (sock_fd < 0) {
            return "ERROR: Not connected";
        }
        
        // 发送命令
        if (send(sock_fd, command.c_str(), command.length(), 0) < 0) {
            return "ERROR: Failed to send command";
        }
        
        // 接收响应
        char buffer[4096];
        std::string response;
        ssize_t bytes_received;
        
        while ((bytes_received = recv(sock_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes_received] = '\0';
            response += buffer;
            
            // 简单的响应结束检测
            if (response.find("\r\n") != std::string::npos) {
                break;
            }
        }
        
        if (bytes_received < 0) {
            return "ERROR: Failed to receive response";
        }
        
        return response;
    }
};

// 创建 RESP 命令格式
std::string create_resp_command(const std::vector<std::string>& args) {
    std::string cmd = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& arg : args) {
        cmd += "$" + std::to_string(arg.length()) + "\r\n" + arg + "\r\n";
    }
    return cmd;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <host> <port> [command]" << std::endl;
        std::cout << "Example: " << argv[0] << " 127.0.0.1 6380 PING" << std::endl;
        return 1;
    }
    
    std::string host = argv[1];
    int port = std::stoi(argv[2]);
    
    SimpleRedisClient client(host, port);
    
    if (!client.connect()) {
        return 1;
    }
    
    if (argc > 3) {
        // 执行指定命令
        std::vector<std::string> args;
        
        // 如果只有一个命令参数，需要按空格分割
        if (argc == 4) {
            std::string command = argv[3];
            std::istringstream iss(command);
            std::string token;
            while (iss >> token) {
                args.push_back(token);
            }
        } else {
            // 多个参数，直接使用
            for (int i = 3; i < argc; i++) {
                args.push_back(argv[i]);
            }
        }
        
        std::string cmd = create_resp_command(args);
        std::cout << "Sending: " << cmd << std::endl;
        
        std::string response = client.send_command(cmd);
        std::cout << "Response: " << response << std::endl;
    } else {
        // 交互模式
        std::cout << "SunKV Interactive Client" << std::endl;
        std::cout << "Type 'quit' to exit" << std::endl;
        
        std::string input;
        while (true) {
            std::cout << "SunKV> ";
            std::getline(std::cin, input);
            
            if (input == "quit" || input == "exit") {
                break;
            }
            
            if (input.empty()) {
                continue;
            }
            
            // 简单的命令解析
            std::istringstream iss(input);
            std::vector<std::string> args;
            std::string token;
            while (iss >> token) {
                args.push_back(token);
            }
            
            std::string cmd = create_resp_command(args);
            std::string response = client.send_command(cmd);
            std::cout << response << std::endl;
        }
    }
    
    std::cout << "Goodbye!" << std::endl;
    return 0;
}
