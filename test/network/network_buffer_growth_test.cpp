#include <cassert>
#include <iostream>
#include <string>
#include <unistd.h>
#include "network/Buffer.h"

static void testRetrieveDoesNotOverAdvance() {
    Buffer buf(16);
    buf.append("abcdef", 6);
    buf.retrieve(2);
    assert(buf.readableBytes() == 4);
    assert(buf.retrieveAllAsString() == "cdef");
}

static void testEnsureWritableReusePrependSpace() {
    Buffer buf(16);
    buf.append("1234567890", 10);
    buf.retrieve(8);

    const char* before = buf.peek();
    buf.append("ABCDEFGHIJKL", 12);

    // 如果复用了前置空间，数据地址应变化（搬移到 kCheapPrepend 位置）
    const char* after = buf.peek();
    if (after == before) {
        std::cerr << "expected buffer compaction to move readable region" << std::endl;
        std::abort();
    }
    assert(buf.retrieveAllAsString() == "90ABCDEFGHIJKL");
}

static void testEnsureWritableExpandWhenNecessary() {
    Buffer buf(8);
    buf.append(std::string(8, 'x'));
    if (buf.writableBytes() != 0) {
        std::cerr << "expected no writable bytes before expansion" << std::endl;
        std::abort();
    }

    buf.append(std::string(64, 'y'));
    assert(buf.readableBytes() == 72);
}

static void testReadWriteFdPath() {
    int fds[2];
    if (::pipe(fds) != 0) {
        std::cerr << "pipe(fds) failed" << std::endl;
        std::abort();
    }

    // 写入端 -> 读入 Buffer
    const std::string input(4096, 'a');
    ssize_t wn = ::write(fds[1], input.data(), input.size());
    if (wn != static_cast<ssize_t>(input.size())) {
        std::cerr << "write input failed" << std::endl;
        std::abort();
    }

    Buffer readBuf(8192);
    int err = 0;
    ssize_t rn = readBuf.readFd(fds[0], &err);
    if (rn != static_cast<ssize_t>(input.size())) {
        std::cerr << "readFd failed" << std::endl;
        std::abort();
    }
    assert(readBuf.readableBytes() == input.size());

    // Buffer -> 写出端（写到另一个 pipe）
    int outfds[2];
    if (::pipe(outfds) != 0) {
        std::cerr << "pipe(outfds) failed" << std::endl;
        std::abort();
    }

    ssize_t bn = readBuf.writeFd(outfds[1], &err);
    if (bn != static_cast<ssize_t>(input.size())) {
        std::cerr << "writeFd failed" << std::endl;
        std::abort();
    }
    assert(readBuf.readableBytes() == 0);

    std::string out(input.size(), '\0');
    ssize_t orc = ::read(outfds[0], out.data(), out.size());
    if (orc != static_cast<ssize_t>(out.size())) {
        std::cerr << "read back failed" << std::endl;
        std::abort();
    }
    assert(out == input);

    ::close(fds[0]);
    ::close(fds[1]);
    ::close(outfds[0]);
    ::close(outfds[1]);
}

int main() {
    testRetrieveDoesNotOverAdvance();
    testEnsureWritableReusePrependSpace();
    testEnsureWritableExpandWhenNecessary();
    testReadWriteFdPath();
    std::cout << "Buffer optimization tests passed." << std::endl;
    return 0;
}
