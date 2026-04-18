#include <cerrno>
#include <iostream>

#include "network/Buffer.h"

int main() {
    Buffer buf(128);

    int readErr = 0;
    const ssize_t n1 = buf.readFd(-1, &readErr);
    if (n1 != -1) {
        std::cerr << "readFd(-1) expected -1, got " << n1 << std::endl;
        return 1;
    }
    // 对于无效 fd，POSIX 一般为 EBADF
    if (readErr != EBADF) {
        std::cerr << "readFd(-1) expected errno EBADF=" << EBADF << ", got " << readErr << std::endl;
        return 1;
    }

    int writeErr = 0;
    const ssize_t n2 = buf.writeFd(-1, &writeErr);
    if (n2 != -1) {
        std::cerr << "writeFd(-1) expected -1, got " << n2 << std::endl;
        return 1;
    }
    if (writeErr != EBADF) {
        std::cerr << "writeFd(-1) expected errno EBADF=" << EBADF << ", got " << writeErr << std::endl;
        return 1;
    }

    std::cout << "network buffer fd error test passed." << std::endl;
    return 0;
}

