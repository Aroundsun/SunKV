#include <iostream>
#include <sstream>
#include <vector>
int main() {
    std::string command = "SET hello world";
    std::istringstream iss(command);
    std::string token;
    std::vector<std::string> args;
    while (iss >> token) {
        args.push_back(token);
        std::cout << "Token: " << token << std::endl;
    }
    std::cout << "Total args: " << args.size() << std::endl;
    return 0;
}
